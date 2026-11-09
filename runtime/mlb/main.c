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
volatile int g_fnprof = 0;
void fnprof_hit(uint64_t addr, uint64_t lr, uint64_t x0) { (void)addr, (void)lr, (void)x0; }

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
    fprintf(stderr, "usage: %s [--exefs DIR] [--romfs DIR] [--profile DIR] [--boot | --probe-fs]\n", argv0);
    return 2;
}

int main(int argc, char **argv) {
    mem_init();   // first: claim the guest address space before anything else can land there
    rt_log("version: mlbts-native %s, game %s", rt_port_version, "MLB The Show 26 1.0.15");
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    plat_crash_hook(crash_handler);
    parse_trace();
    cfg = (RtConfig){.exefs = cfg_get("MLB_EXEFS") ? cfg_get("MLB_EXEFS") : rt_default_exefs,
                     .romfs = cfg_get("MLB_ROMFS") ? cfg_get("MLB_ROMFS") : rt_default_romfs,
                     .profile = cfg_get("MLB_PROFILE") ? cfg_get("MLB_PROFILE") : rt_default_profile,
                     .gpu = cfg_value("MLB_GPU")};
    int probe = 0;
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
        } else {
            return usage(argv[0]);
        }
    }
    log_open();
    if (probe) return probe_fs();
    rt_boot();
    return 0;
}
