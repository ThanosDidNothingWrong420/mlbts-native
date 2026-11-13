// Adapted from TotK Recomp runtime/main.c @ ae30a61 (MIT, Copyright (c) 2026 Valiantsin Dzerakh), which adapts LAS
// Recomp runtime/main.c @ da35b83 (MIT, Copyright (c) 2026 Peiikai). Kept: logging, the runtime services the
// generated code calls (cpu.h), the crash reporter. Dropped: TotK's settings file, probes, Discord, play time,
// resolution hooks and the graphics modes.
//
// mlb.exe [--exefs DIR] [--romfs DIR] [--profile DIR] [--boot | --probe-fs]
// Without arguments the paths come from MLB_EXEFS / MLB_ROMFS / MLB_PROFILE, else work/update/exefs, work/romfs/romfs
// and work/profile under the current directory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "fs.h"
#include "host.h"
#include "platform/platform.h"
#include "sha256.h"

// ---- log: stderr, plus logs/mlb.log next to mlb.exe once log_open ran. Lines before log_open (mem_init runs first,
// so the CRT heap does not land in the guest address space) wait in a static buffer.
#define LOG_CAP (64ull << 20)
enum { LOG_EARLY, LOG_FILE, LOG_OFF };
static int g_log_state;
static PlatAppend *g_log_file;
static char g_log_path[PLAT_PATH_MAX * 3];
static char g_log_early[32768];
static size_t g_log_early_n;
static uint64_t g_log_bytes;
unsigned rt_trace;

void rt_log_raw(const char *p, size_t n) {
    fwrite(p, 1, n, stderr);
    int state = __atomic_load_n(&g_log_state, __ATOMIC_ACQUIRE);
    if (state == LOG_EARLY) {
        size_t at = __atomic_fetch_add(&g_log_early_n, n, __ATOMIC_RELAXED);
        if (at + n <= sizeof g_log_early) memcpy(g_log_early + at, p, n);
    } else if (state == LOG_FILE) {
        uint64_t at = __atomic_fetch_add(&g_log_bytes, n, __ATOMIC_RELAXED);
        if (at + n <= LOG_CAP) plat_append_write(g_log_file, p, n);
    }
}

void rt_vlogf(const char *fmt, va_list ap) {
    char buf[2048];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n > 0) rt_log_raw(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
}

void rt_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    rt_vlogf(fmt, ap);
    va_end(ap);
}

void rt_log(const char *fmt, ...) {
    char buf[2048];
    int n = snprintf(buf, sizeof buf, "[host] ");
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
    va_end(ap);
    n = k < 0 ? n : n + k < (int)sizeof buf - 1 ? n + k : (int)sizeof buf - 2;
    buf[n++] = '\n';
    rt_log_raw(buf, n);
}

const char *rt_log_path(void) {
    return __atomic_load_n(&g_log_state, __ATOMIC_ACQUIRE) == LOG_FILE ? g_log_path : NULL;
}

static void log_open(void) {
    char dir[PLAT_PATH_MAX * 3];
    char *cut = NULL;
    if (plat_exe_path(dir, sizeof dir)) {
        cut = plat_last_sep(dir);
        char *f = strrchr(dir, '/');
        if (f > cut) cut = f;
    }
    if (cut && (size_t)(cut - dir) + sizeof "/logs" <= sizeof dir) {
        strcpy(cut, "/logs");
        plat_file_mkdir_p(dir);
        snprintf(g_log_path, sizeof g_log_path, "%s/mlb.log", dir);
        g_log_file = plat_append_open(g_log_path);
    }
    if (!g_log_file) {
        __atomic_store_n(&g_log_state, LOG_OFF, __ATOMIC_RELEASE);
        return;
    }
    size_t n = __atomic_load_n(&g_log_early_n, __ATOMIC_RELAXED);
    if (n > sizeof g_log_early) n = sizeof g_log_early;
    plat_append_write(g_log_file, g_log_early, n);
    g_log_bytes = n;
    __atomic_store_n(&g_log_state, LOG_FILE, __ATOMIC_RELEASE);
    rt_log("log: %s", g_log_path);
}

// MLB_TRACE (or TOTK_TRACE) = svc,host,ipc,fs,gfx,perf,sched,audio,present,all
static void parse_trace(void) {
    const char *e = cfg_get("TOTK_TRACE");
    if (!e) return;
    static const struct { const char *name; unsigned bit; } names[] = {
        {"svc", RT_TRACE_SVC}, {"host", RT_TRACE_HOSTCALL}, {"ipc", RT_TRACE_IPC}, {"fs", RT_TRACE_FS},
        {"gfx", RT_TRACE_GFX}, {"perf", RT_TRACE_PERF}, {"sched", RT_TRACE_SCHED}, {"audio", RT_TRACE_AUDIO},
        {"present", RT_TRACE_PRESENT}, {"all", ~0u}};
    for (const char *p = e; *p;) {
        size_t n = strcspn(p, ",");
        int known = 0;
        for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
            if (strlen(names[i].name) == n && !strncmp(p, names[i].name, n)) rt_trace |= names[i].bit, known = 1;
        if (!known && n) rt_log("MLB_TRACE: unknown item '%.*s'", (int)n, p);
        p += n + (p[n] == ',');
    }
}

void rt_sha256(const void *p, size_t n, uint8_t out[32]) { sha256(p, n, out); }

static RtConfig cfg;
const RtConfig *rt_config(void) { return &cfg; }

// ---- runtime services the generated code calls (cpu.h)
// MLB_FNTRACE=<guest addr>,... (diagnostic): logs the first calls of these game functions, then every 1000th, with the
// caller and x0. Every generated function checks g_fnprof on entry (FN_ENTER), so it stays 0 unless this is set: the
// game runs measurably slower while it is on. Used to follow the audio engine's stream threads (Scream snd_stream).
volatile int g_fnprof = 0;
static uint64_t fn_addr[16];
static uint64_t fn_hits[16];
static int fn_n;
static uint64_t fn_lo, fn_hi;
// MLB_FNTRACE_PEEK=<off>,...: also a 32-bit value behind x0 (x0 + off, read as a pointer for every offset but the last)
static uint64_t fn_peek[4];
static int fn_npeek;
static int fn_peek_value(uint64_t x0, uint32_t *out) {
    uint64_t p = x0;
    for (int i = 0; i < fn_npeek; i++) {
        uint64_t a = p + fn_peek[i];
        if (!plat_mem_readable(a, 8)) return 0;
        if (i == fn_npeek - 1) {
            *out = *(const uint32_t *)(uintptr_t)a;
            return 1;
        }
        p = *(const uint64_t *)(uintptr_t)a;
    }
    return 0;
}   // MLB_FNTRACE=<lo>-<hi>: the first call of every function in [lo, hi)
void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0) {
    if (addr >= fn_lo && addr < fn_hi) {
        static uint64_t first[65536];
        for (unsigned j = 0, h = (unsigned)((addr * 0x9e3779b97f4a7c15ull) >> 48); j < 65536; j++, h = (h + 1) & 65535) {
            uint64_t v = __atomic_load_n(&first[h], __ATOMIC_RELAXED);
            if (v == addr) return;
            if (!v) {
                if (!__atomic_compare_exchange_n(&first[h], &v, addr, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
                char from[128];
                rt_log("fntrace: first call of %#llx from %s, x0 %#llx", (unsigned long long)addr,
                       ldr_describe(lr, from, sizeof from), (unsigned long long)x0);
                return;
            }
        }
        return;
    }
    for (int i = 0; i < fn_n; i++) {
        if (fn_addr[i] != addr) continue;
        uint64_t k = __atomic_add_fetch(&fn_hits[i], 1, __ATOMIC_RELAXED);
        uint32_t peek = 0;
        int peeked = fn_npeek && fn_peek_value(x0, &peek);
        if (peeked) {   // the value behind x0 changed since this function's last call with this x0
            static uint64_t last_x0[64];
            static uint32_t last_v[64];
            unsigned h = (unsigned)((x0 * 0x9e3779b97f4a7c15ull) >> 58);
            if (last_x0[h] != x0 || last_v[h] != peek) {
                last_x0[h] = x0, last_v[h] = peek;
                rt_log("fntrace: %#llx call %llu, x0 %#llx: peek %u (%#x)", (unsigned long long)addr,
                       (unsigned long long)k, (unsigned long long)x0, peek, peek);
            }
            return;
        }
        // a call with an x0 not seen before is logged too (each distinct message of a print function, once)
        static uint64_t seen[4096];
        uint64_t key = (addr * 0x9e3779b97f4a7c15ull) ^ x0 ^ (x0 >> 29), fresh = 0;
        for (unsigned j = 0, h = (unsigned)(key >> 52); j < 4096; j++, h = (h + 1) & 4095) {
            uint64_t v = __atomic_load_n(&seen[h], __ATOMIC_RELAXED);
            if (v == key) break;
            if (!v) {
                fresh = __atomic_compare_exchange_n(&seen[h], &v, key, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
                break;
            }
        }
        if (k <= 8 || k % 1000 == 0 || fresh) {
            char from[128];
            // x0 into main's read-only data (0x7001c22000 + 0x539000, docs/recon-report.md): a message, shown as text
            int text = x0 >= 0x7001c22000ull && x0 < 0x700215b000ull;
            rt_log("fntrace: %#llx call %llu from %s, x0 %#llx%s%.160s%s", (unsigned long long)addr, (unsigned long long)k,
                   ldr_describe(lr, from, sizeof from), (unsigned long long)x0, text ? " \"" : "",
                   text ? (const char *)(uintptr_t)x0 : "", text ? "\"" : "");
        }
        return;
    }
}

static void parse_fntrace(void) {
    const char *pk = getenv("MLB_FNTRACE_PEEK");
    for (const char *p = pk; p && *p && fn_npeek < 4;) {
        char *end;
        fn_peek[fn_npeek] = strtoull(p, &end, 16);
        if (end == p) break;
        fn_npeek++;
        p = end + (*end == ',');
    }
    const char *e = cfg_get("TOTK_FNTRACE");
    if (!e || !*e) return;
    for (const char *p = e; *p && fn_n < 16;) {
        char *end;
        uint64_t a = strtoull(p, &end, 16);
        if (end == p) break;
        if (*end == '-') {
            const char *q = end + 1;
            fn_lo = a, fn_hi = strtoull(q, &end, 16);
            if (end == q) break;
            p = end + (*end == ',');
            continue;
        }
        fn_addr[fn_n++] = a;
        p = end + (*end == ',');
    }
    if (fn_n || fn_hi > fn_lo) {
        rt_log("MLB_FNTRACE: following %d game functions and the range %#llx-%#llx (the game runs slower)", fn_n,
               (unsigned long long)fn_lo, (unsigned long long)fn_hi);
        g_fnprof = 1;
    }
}

void rt_brk(Cpu *c, uint32_t imm, uint64_t pc) {
    char at[128], lr[128];
    rt_fatal("brk #%#x at %s (lr %s)", imm, ldr_describe(pc, at, sizeof at), ldr_describe(c->x[30], lr, sizeof lr));
}

void rt_udf(Cpu *c, uint64_t pc) {
    char at[128], lr[128];
    rt_fatal("undefined instruction at %s (lr %s)", ldr_describe(pc, at, sizeof at),
             ldr_describe(c->x[30], lr, sizeof lr));
}

// CNTPCT_EL0: the Switch counter runs at 19.2 MHz.
static PlatScale tick_scale;
__attribute__((constructor)) static void tick_scale_init(void) { tick_scale = plat_scale(19200000u, plat_counter_hz()); }
uint64_t rt_ticks(void) { return plat_scaled(tick_scale, plat_counter()); }

// ---- crash reporter
typedef struct { uintptr_t host; uint64_t guest; } FnRev;
static FnRev *fn_rev;
static uint64_t fn_rev_n;

static int by_host(const void *a, const void *b) {
    uintptr_t x = ((const FnRev *)a)->host, y = ((const FnRev *)b)->host;
    return x < y ? -1 : x > y;
}

#define MAX_MODULES 16
static uint64_t lifted_function(uintptr_t rip) {
    if (!fn_rev) {
        static RtModule mods[MAX_MODULES];
        int n = rt_linked_modules(mods, MAX_MODULES);
        uint64_t total = 0;
        for (int i = 0; i < n; i++) total += mods[i].count;
        fn_rev = malloc((total ? total : 1) * sizeof *fn_rev);
        if (!fn_rev) return 0;
        for (int i = 0; i < n; i++)
            for (uint64_t k = 0; k < mods[i].count; k++)
                fn_rev[fn_rev_n++] = (FnRev){(uintptr_t)mods[i].fns[k], mods[i].addrs[k]};
        qsort(fn_rev, fn_rev_n, sizeof *fn_rev, by_host);
    }
    if (!fn_rev_n || rip > fn_rev[fn_rev_n - 1].host) return 0;
    uint64_t lo = 0, hi = fn_rev_n;
    while (lo < hi) {
        uint64_t mid = (lo + hi) / 2;
        if (fn_rev[mid].host <= rip) lo = mid + 1;
        else hi = mid;
    }
    return lo ? fn_rev[lo - 1].guest : 0;
}

static void crash_handler(const PlatCrash *x) {
    static volatile int32_t busy;
    if (plat_xchg32(&busy, 1)) plat_exit(71);
    char d[160];
    uintptr_t rip = x->rip;
    rt_logf("FATAL: %s (%#x) at host %p (", x->what, x->code, (void *)rip);
    if (x->module && !*x->module) rt_logf("outside every module)");
    else rt_logf("%s+%#llx)", x->module ? x->module : "mlb", (unsigned long long)x->offset);
    if (x->access >= 0)
        rt_logf(", %s address %#llx", x->access == 1 ? "writing" : x->access == 8 ? "executing" : "reading",
                (unsigned long long)x->addr);
    rt_log_raw("\n", 1);
    uint64_t f = x->module ? 0 : lifted_function(rip);
    if (f) rt_logf("  in lifted function f_%llx = %s\n", (unsigned long long)f, ldr_describe(f, d, sizeof d));
    uintptr_t exe = plat_exe_base();
    for (int i = 0; i < x->nframes; i++) {
        const char *m = x->frame_module[i];
        if (m) {
            rt_logf("  host frame %2d: %s+%#llx\n", i, *m ? m : "outside every module",
                    (unsigned long long)x->frame_offset[i]);
            continue;
        }
        uint64_t g = lifted_function(x->frames[i]);
        rt_logf("  host frame %2d: mlb+%#llx", i, (unsigned long long)(x->frames[i] - exe));
        if (g) rt_logf(" in lifted function f_%llx", (unsigned long long)g);
        rt_log_raw("\n", 1);
    }
    Cpu *c = kern_cpu();
    if (c) {
        for (int i = 0; i < 31; i++)
            rt_logf("  x%-2d %016llx%s", i, (unsigned long long)c->x[i], i % 4 == 3 ? "\n" : "");
        rt_logf("  sp  %016llx\n  pc  %016llx", (unsigned long long)c->sp, (unsigned long long)c->pc);
        if (hostfn_name(c->pc)) rt_logf(" (%s)", ldr_describe(c->pc, d, sizeof d));
        rt_logf("  lr %s\n", ldr_describe(c->x[30], d, sizeof d));
        uint64_t fp = c->x[29];
        for (int n = 0; n < 32 && fp && !(fp & 7) && plat_mem_readable(fp, 16); n++) {
            uint64_t next = LD64(fp), lr = LD64(fp + 8);
            rt_logf("  guest frame %2d: %s\n", n, ldr_describe(lr, d, sizeof d));
            if (next <= fp || next - fp > (64u << 20)) break;
            fp = next;
        }
    } else {
        rt_logf("  not on a guest thread\n");
    }
    fflush(stderr);
    plat_exit(71);
}

static int usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--exefs DIR] [--romfs DIR] [--profile DIR] [--boot | --probe-fs | --import-save "
            "[ryujinx | FOLDER]]\n", argv0);
    return 2;
}

int main(int argc, char **argv) {
    mem_init();   // first: claim the guest address space before anything else can land there
    rt_log("version: mlbts-native %s, game %s", rt_port_version, "MLB The Show 26 1.0.15");
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    plat_crash_hook(crash_handler);
    cfg_load_file();   // mlb.cfg beside the exe: before any setting is read (the defaults below leave its values alone)
    // mlbts-native, for now (the batting frame rate): the per-second perf lines are on unless MLB_TRACE says otherwise
    // and the guest scheduler's report every 120 presents (per thread: run, block, sleep and core-wait shares, wake and
    // core-grant latency, sleep asked against taken): the uneven frames at 60 fps
    if (!cfg_get("MLB_TRACE")) cfg_set("TRACE", "perf,sched");
    // REPLAY_ASYNC and FRAME_QUEUE are on by default: their rows in cfg.c say why (the settings menu's reset needs the
    // defaults in the table)
    parse_trace();
    parse_fntrace();
    cfg = (RtConfig){.exefs = cfg_get("MLB_EXEFS") ? cfg_get("MLB_EXEFS") : rt_default_exefs,
                     .romfs = cfg_get("MLB_ROMFS") ? cfg_get("MLB_ROMFS") : rt_default_romfs,
                     .profile = cfg_get("MLB_PROFILE") ? cfg_get("MLB_PROFILE") : rt_default_profile,
                     .gpu = cfg_value("MLB_GPU")};
    int probe = 0, import = 0;
    const char *import_from = NULL;
    for (int i = 1; i < argc; i++) {
        const char **path = !strcmp(argv[i], "--exefs") ? &cfg.exefs : !strcmp(argv[i], "--romfs") ? &cfg.romfs
                          : !strcmp(argv[i], "--profile") ? &cfg.profile : NULL;
        if (path) {
            if (++i == argc) return usage(argv[0]);
            *path = argv[i];
        } else if (!strcmp(argv[i], "--boot")) {
            cfg.boot = 1;
        } else if (!strcmp(argv[i], "--probe-fs")) {   // fs_host.c: the nn::fs overrides on the RomFS and in a
            probe = 1;                                 // temporary profile; no guest code runs
        } else if (!strcmp(argv[i], "--import-save")) {   // save_import.c: another host's save of the game (Ryujinx's
            import = 1;                                    // by default) becomes the profile's save; no guest code runs
            if (i + 1 < argc && strncmp(argv[i + 1], "--", 2)) import_from = argv[++i];
        } else {
            return usage(argv[0]);
        }
    }
    // --import-save reports on the console only: opening the log would replace mlb.log, a running game's too
    if (import) return save_import_main(import_from);
    log_open();
    if (probe) return probe_fs();
    // mlbts-native, for now (the batting frame rate): the sampling profiler, with every thread's call chains for each
    // frame of 19 ms or more (one every 3 s at most) in <profile>/prof.txt.f<frame> (MLB_SAMPLER=off: none)
    if (!cfg_get("MLB_SAMPLER")) {
        static char sp[1100];
        snprintf(sp, sizeof sp, "%s/prof.txt", cfg.profile);
        cfg_set("SAMPLER", sp);
        if (!cfg_get("MLB_SAMPLER_SLOW")) cfg_set("SAMPLER_SLOW", "19");
    } else if (!strcmp(cfg_get("MLB_SAMPLER"), "off")) cfg_set("SAMPLER", "");
    prof_start();   // perf.c: the frame-time summary, MLB_TRACE=perf, MLB_SAMPLER
    playtime_init();   // playtime.c: the settings menu's play time, in <profile>/playtime.txt
    rt_boot();
    return 0;
}
