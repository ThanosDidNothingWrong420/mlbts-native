// Uniform blocks as of the submit call. NVN starts the GPU when nvnQueueSubmitCommands returns and the GPU reads the
// constants soon after; here the replay records for several milliseconds, and the GPU reads a pool's bytes when it
// runs the part that uses them (the VRAM mirror's sync copies run then, vk_memory.c "VRAM mirror"). Meanwhile the game,
// having handed the frame over, rewrites per-object uniform blocks on other threads with no fence: late draws read
// another object's matrix (phantom rocks, a flat far field; TotK 1.2.1's producer f_70008c6460, 14-24 ms after the
// submit). So at the start of a submission's recording every uniform range it binds is copied into an arena of its own,
// and the uniform bindings of the replay use the copy.
// - Only ranges of imported pools (memory the CPU writes) are copied, aligned out to 256 bytes (the largest
//   minUniformBufferOffsetAlignment) and at most 64 KiB from the bound address, overlapping ones merged.
// - A range the GPU wrote in a submission that has not completed is left out: its bytes reach guest memory only with
//   that submission's write-back, the mirror holds them until then.
// - A GPU write within the submission (UpdateUniformBuffer, a storage buffer a shader writes, a buffer copy or fill,
//   an image-to-buffer copy: vk_usnap_drop) drops the copies it touches from then on, so later draws read the result.
// - The arena goes back to a free list once the submission's fence is done (completion thread).
// The design follows MacNativeGames' TOTK_UBO_SNAPSHOT (games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m usnap_take,
// usnap_drop; MIT, Copyright (c) 2026 jhaizhou); the code is ours.
// The copy is taken where vk_exec_record starts: at the submit call when the replay runs on the submitting thread, later
// with TOTK_REPLAY_ASYNC. TOTK_UBO_SNAPSHOT=0 binds live memory as before; =ab alternates (TOTK_AB).
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "nvn_api.h"
#include "vk.h"
#include "vk_dlaa.h"
#include "platform/platform.h"

#define ALIGN 256u
#define MAX_RANGE (64u << 10)
#define ARENA_MIN (4u << 20)
#define CALL_DEPTH 16

typedef struct Arena {
    struct Arena *next;
    VkBuffer buf;
    VmaAllocation alloc;
    uint8_t *map;
    uint8_t *cpu;    // with DLSS (vk_dlaa_available), the same bytes in host memory for the CPU's reads (vk_usnap_bytes):
                     // map is often device memory through the BAR, written fast but read back at about a microsecond a
                     // cache line. Without DLSS nothing reads them back, and the bytes go into map in one copy.
    uint64_t size;
} Arena;

typedef struct { uint64_t lo, hi; } Rng;
typedef struct { uint64_t lo, hi, off; int dropped; } Reg;
typedef struct { uint64_t lo, hi, fence; } Wrote;

static struct {
    int mode;                   // rt_knob: 0 off, 1 on, 2 ab
    int on;                     // this submission has a snapshot
    uint64_t fence, gen;
    Arena *arena;
    Rng *col;                   // collected ranges
    int ncol, ccol;
    Reg *reg;                   // merged, sorted by lo, disjoint
    int nreg, creg;
    Wrote *wr;                  // GPU writes of submissions not known to be complete
    int nwr, cwr;
    // since the last summary
    uint64_t subs, regions, bytes, copy_ns, hits, drops, inflight, refused, off_subs;
} U;

static Arena *free_arenas;
static PlatMutex free_lock = PLAT_MUTEX_INIT;

static void *grow(void *p, int *cap, size_t elem) {
    int n = *cap ? *cap * 2 : 1024;
    void *q = realloc(p, (size_t)n * elem);
    if (!q) rt_fatal("vk_usnap: out of memory");
    *cap = n;
    return q;
}

static Arena *arena_get(uint64_t need) {
    plat_mutex_lock(&free_lock);
    Arena **pp = &free_arenas, *a = NULL;
    for (; *pp; pp = &(*pp)->next)
        if ((*pp)->size >= need) {
            a = *pp, *pp = a->next;
            break;
        }
    plat_mutex_unlock(&free_lock);
    if (a) return a;
    a = calloc(1, sizeof *a);
    if (!a) rt_fatal("vk_usnap: out of memory");
    a->size = need < ARENA_MIN ? ARENA_MIN : (need + (1u << 20) - 1) & ~(uint64_t)((1u << 20) - 1);
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = a->size,
                             // transfer source: DLAA copies the camera block out (vk_dlaa.c mv_pass)
                             .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                      // mlbts-native: small vertex and index ranges too (vk_exec_draw.c)
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    // Device-local through the BAR when the device has it (the GPU reads constants on every draw), else host memory.
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                           VMA_ALLOCATION_CREATE_MAPPED_BIT};
    VmaAllocationInfo info;
    VkResult res = vmaCreateBuffer(vk_ctx()->vma, &bi, &ai, &a->buf, &a->alloc, &info);
    if (res != VK_SUCCESS) {
        rt_log("vk_usnap: arena of %llu bytes failed (%d): this submission binds live memory",
               (unsigned long long)a->size, (int)res);
        free(a);
        return NULL;
    }
    a->map = info.pMappedData;
    rt_log("vk_usnap: new arena of %.1f MiB", a->size / 1048576.0);
    return a;
}

static void arena_done(void *arg) {   // completion thread: the GPU has read it
    Arena *a = arg;
    plat_mutex_lock(&free_lock);
    a->next = free_arenas, free_arenas = a;
    plat_mutex_unlock(&free_lock);
}

// mlbts-native: the bytes an indexed draw reads, 0: unknown
static uint64_t idx_bytes(const p_draw_idx *d) {
    static const uint32_t isize[3] = {1, 2, 4};
    return d->itype >= 0 && d->itype <= 2 && d->count > 0 ? (uint64_t)d->count * isize[d->itype] : 0;
}

static void collect(const op_hdr *h, int depth) {
    for (;;) {
        if (h->size < sizeof *h || (h->size & 7) || h->op >= OP_COUNT_)
            rt_fatal("vk_usnap: corrupt command record at %p: op %u size %u", (const void *)h, h->op, h->size);
        const void *a = nvn_op_payload(h);
        if (h->op == OP_END) return;
        if (h->op == OP_JUMP) {
            h = (const op_hdr *)(uintptr_t)((const p_jump *)a)->target;
            continue;
        }
        if (h->op == OP_CALL && depth < CALL_DEPTH) collect(nvn_op_first(((const p_jump *)a)->target), depth + 1);
        if (h->op == OP_BIND_UBO) {
            const p_bind_buf *b = a;
            if (b->addr && b->size) {
                uint64_t n = b->size < MAX_RANGE ? b->size : MAX_RANGE;
                // the vertex stage's object and bone blocks: also the game's other copy on either side, which still
                // holds last frame's (vk_mvprev.c)
                uint64_t step = vk_mvprev_step(b->stage, b->index, b->size);
                // which neighbours lie in an imported pool, per block and vk_mem_gen: the same blocks are bound by
                // many draws, and two resolves for each binding were a quarter of the snapshot's time
                static struct { uint64_t addr, size, gen; int ok; } nc[1024];
                uint32_t ci = (uint32_t)((b->addr ^ n << 48) * 0x9E3779B97F4A7C15ull >> 54);
                uint64_t gen = vk_mem_gen();
                int known = step && nc[ci].addr == b->addr && nc[ci].size == n && nc[ci].gen == gen, ok = 0;
                for (int k = step ? -1 : 0; k <= (step ? 1 : 0); k++) {
                    uint64_t at = b->addr + (uint64_t)((int64_t)k * (int64_t)step);
                    if (k < 0 && b->addr < step) continue;
                    // a neighbour past its pool would make the whole merged region fail the pool check below, and
                    // the bindings in it read live memory again
                    VkMemSpan sp;
                    uint64_t lo = at & ~(uint64_t)(ALIGN - 1);
                    if (k && known) {
                        if (!(nc[ci].ok >> (k + 1) & 1)) continue;
                    } else if (k) {
                        if (!vk_mem_resolve(lo, at + n - lo, &sp) || sp.alloc || !sp.pool || sp.avail < at + n - lo ||
                            (sp.cls != VK_MCLS_HOST_CACHED && sp.cls != VK_MCLS_HOST_UNC))
                            continue;
                        ok |= 1 << (k + 1);
                    }
                    if (U.ncol == U.ccol) U.col = grow(U.col, &U.ccol, sizeof *U.col);
                    U.col[U.ncol++] = (Rng){at & ~(uint64_t)(ALIGN - 1), at + n};
                }
                if (step && !known) nc[ci].addr = b->addr, nc[ci].size = n, nc[ci].gen = gen, nc[ci].ok = ok;
            }
        } else if (h->op == OP_BIND_VBUF) {   // mlbts-native: vertex ranges up to MAX_RANGE (bind_vertex_buffers)
            const p_bind_vbuf *b = a;
            for (int i = 0; i < b->n && i < 16; i++)
                if (b->r[i].address && b->r[i].size && b->r[i].size <= MAX_RANGE) {
                    if (U.ncol == U.ccol) U.col = grow(U.col, &U.ccol, sizeof *U.col);
                    U.col[U.ncol++] = (Rng){b->r[i].address & ~(uint64_t)(ALIGN - 1), b->r[i].address + b->r[i].size};
                }
        } else if (h->op == OP_DRAW_IDX) {    // mlbts-native: index ranges up to MAX_RANGE (op_draw_idx)
            const p_draw_idx *d = a;
            uint64_t n = idx_bytes(d);
            if (d->ibuf && n && n <= MAX_RANGE) {
                if (U.ncol == U.ccol) U.col = grow(U.col, &U.ccol, sizeof *U.col);
                U.col[U.ncol++] = (Rng){d->ibuf & ~(uint64_t)(ALIGN - 1), d->ibuf + n};
            }
        }
        h = nvn_op_next(h);
    }
}

static int by_lo(const void *a, const void *b) {
    uint64_t x = ((const Rng *)a)->lo, y = ((const Rng *)b)->lo;
    return x < y ? -1 : x > y;
}
static int wr_by_lo(const void *a, const void *b) {
    uint64_t x = ((const Wrote *)a)->lo, y = ((const Wrote *)b)->lo;
    return x < y ? -1 : x > y;
}

// Sorts the collected ranges by lo. A town frame binds about 10,000 uniform ranges and qsort took half a millisecond
// of the copy's 0.96 ms on the game thread, so they go through an LSD radix sort on (lo - lowest) / ALIGN, three
// passes of 11 bits; qsort is left for a span that does not fit in 33 bits (2 TiB).
#define RADIX_BITS 11
static void sort_col(void) {
    static Rng *tmp;
    static int ctmp;
    int n = U.ncol;
    if (n < 2) return;
    uint64_t lo = UINT64_MAX, hi = 0;
    for (int i = 0; i < n; i++) {
        if (U.col[i].lo < lo) lo = U.col[i].lo;
        if (U.col[i].lo > hi) hi = U.col[i].lo;
    }
    if ((hi - lo) / ALIGN >> (3 * RADIX_BITS)) {
        qsort(U.col, (size_t)n, sizeof *U.col, by_lo);
        return;
    }
    if (ctmp < n) {
        free(tmp);
        ctmp = U.ccol;
        if (!(tmp = malloc((size_t)ctmp * sizeof *tmp))) rt_fatal("vk_usnap: out of memory");
    }
    Rng *src = U.col, *dst = tmp;
    for (int pass = 0; pass < 3; pass++) {
        int shift = pass * RADIX_BITS, count[1 << RADIX_BITS] = {0};
        for (int i = 0; i < n; i++) count[(src[i].lo - lo) / ALIGN >> shift & ((1 << RADIX_BITS) - 1)]++;
        for (int k = 0, sum = 0; k < 1 << RADIX_BITS; k++) {
            int c = count[k];
            count[k] = sum, sum += c;
        }
        for (int i = 0; i < n; i++) dst[count[(src[i].lo - lo) / ALIGN >> shift & ((1 << RADIX_BITS) - 1)]++] = src[i];
        Rng *t = src;
        src = dst, dst = t;
    }
    if (src != U.col) memcpy(U.col, src, (size_t)n * sizeof *src);   // three passes end in tmp
}

// Keeps the GPU writes of submissions that are not done, merged into disjoint ranges sorted by lo (the terrain rewrites
// one block before every draw: hundreds of equal entries), each with the newest fence among its parts.
static void wr_prune(uint64_t done) {   // mlbts-native: keeps the writes above done
    int n = 0;
    for (int i = 0; i < U.nwr; i++)
        if (U.wr[i].fence > done) U.wr[n++] = U.wr[i];
    qsort(U.wr, (size_t)n, sizeof *U.wr, wr_by_lo);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m && U.wr[i].lo <= U.wr[m - 1].hi) {
            if (U.wr[i].hi > U.wr[m - 1].hi) U.wr[m - 1].hi = U.wr[i].hi;
            if (U.wr[i].fence > U.wr[m - 1].fence) U.wr[m - 1].fence = U.wr[i].fence;
        } else {
            U.wr[m++] = U.wr[i];
        }
    }
    U.nwr = m;
}
// wr_prune ran: disjoint, sorted. mlbts-native: only writes whose fence is above `above` count (a merged entry carries
// the newest fence of its parts).
static int wr_hits(uint64_t lo, uint64_t hi, uint64_t above) {
    int a = 0, b = U.nwr;
    while (a < b) {
        int k = (a + b) / 2;
        if (U.wr[k].hi <= lo) a = k + 1;
        else b = k;
    }
    for (; a < U.nwr && U.wr[a].lo < hi; a++)
        if (U.wr[a].fence > above) return 1;
    return 0;
}

// ---- mlbts-native: the bytes as of the submit call, with the asynchronous replay
// The copy above is taken where the replay starts recording a submission. With TOTK_REPLAY_ASYNC that is up to a frame
// after the game's submit call, and the game may have rewritten the bytes by then: MLB's menus build their quads
// (vertices, constants) in transient memory each frame, and menu images and HUD pieces showed cut and sliced, now and
// then, while every texture they sampled matched the game's memory (vk_texture_verify). So at the submit call, on the
// game thread, the uniform, vertex and index ranges the submission binds (up to MAX_RANGE each) are copied into host
// memory (vk_usnap_submit), and the replay's copy takes those bytes over the live ones (fz_overlay), except where a GPU
// write that completed after the submit call lies (its result is in the live bytes, as the GPU would have seen it).
// The summary counts the bytes that changed between the submit call and the replay. MLB_SUBMIT_COPY=0: off.
#define FZ_QUEUE 16
typedef struct { uint64_t lo, hi; uint32_t kind; } FRng;   // kind: 1 uniform, 2 vertex, 4 index
typedef struct Frozen {
    struct Frozen *next;
    uint64_t h[8];          // the first handles, to match the replay's submission
    int n, nr, cr;
    uint64_t done;          // vk_sync_fence_done at the copy
    FRng *r;                // merged, sorted, disjoint
    uint64_t *off;          // each range's offset in bytes
    uint8_t *bytes;
    uint64_t cap;
} Frozen;
static PlatMutex fz_lock = PLAT_MUTEX_INIT;
static Frozen *fz_head, *fz_tail, *fz_free;
static int fz_n;
static FRng *gcol;          // game thread (under the front end's submit lock)
static int gncol, gccol;
static struct {             // atomics: game thread and replay
    uint64_t subs, ranges, bytes, ns, full, unreadable;
    uint64_t used, changed_ranges, changed_bytes, changed_kind[8], gpu_after, mismatch;
} F;

static void fz_add(uint64_t lo, uint64_t hi, uint32_t kind) {
    if (gncol == gccol) gcol = grow(gcol, &gccol, sizeof *gcol);
    gcol[gncol++] = (FRng){lo & ~(uint64_t)(ALIGN - 1), hi, kind};
}
static void fz_collect(const op_hdr *h, int depth) {
    for (;;) {
        if (h->size < sizeof *h || (h->size & 7) || h->op >= OP_COUNT_) return;   // the replay reports it
        const void *a = nvn_op_payload(h);
        if (h->op == OP_END) return;
        if (h->op == OP_JUMP) {
            h = (const op_hdr *)(uintptr_t)((const p_jump *)a)->target;
            continue;
        }
        if (h->op == OP_CALL && depth < CALL_DEPTH) fz_collect(nvn_op_first(((const p_jump *)a)->target), depth + 1);
        if (h->op == OP_BIND_UBO) {
            const p_bind_buf *b = a;
            if (b->addr && b->size) fz_add(b->addr, b->addr + (b->size < MAX_RANGE ? b->size : MAX_RANGE), 1);
        } else if (h->op == OP_BIND_VBUF) {
            const p_bind_vbuf *b = a;
            for (int i = 0; i < b->n && i < 16; i++)
                if (b->r[i].address && b->r[i].size && b->r[i].size <= MAX_RANGE)
                    fz_add(b->r[i].address, b->r[i].address + b->r[i].size, 2);
        } else if (h->op == OP_DRAW_IDX) {
            const p_draw_idx *d = a;
            uint64_t n = idx_bytes(d);
            if (d->ibuf && n && n <= MAX_RANGE) fz_add(d->ibuf, d->ibuf + n, 4);
        }
        h = nvn_op_next(h);
    }
}
// [lo, lo+n) can be read: the heap and module images answer from the layout (mem_guest_readable); other memory (pools
// the game placed elsewhere) asks the OS, tens of microseconds, so the runs of committed pages found are kept (8, for
// 4096 submissions: the game binds memory it keeps mapped).
static int fz_readable(uint64_t lo, uint64_t n) {
    static struct { uint64_t lo, hi; } c[8];
    static int ci;
    static uint64_t calls;
    if (++calls % (4096 * 256) == 0) memset(c, 0, sizeof c);
    for (int i = 0; i < 8; i++)
        if (lo >= c[i].lo && lo + n <= c[i].hi) return 1;
    if (!mem_guest_readable(lo, n)) return 0;
    uint64_t end = 0;
    int st = plat_mem_state(lo, lo + (256ull << 20), &end);
    if ((st == PLAT_PROT_READ || st == PLAT_PROT_RW) && end >= lo + n) c[ci++ & 7].lo = lo, c[(ci - 1) & 7].hi = end;
    return 1;
}
static int frng_by_lo(const void *a, const void *b) {
    uint64_t x = ((const FRng *)a)->lo, y = ((const FRng *)b)->lo;
    return x < y ? -1 : x > y;
}

// Game thread, at an asynchronous submit (nvn_api_core.c), with the handles the replay will record.
void vk_usnap_submit(const uint64_t *handles, int n) {
    static int on = -1;
    if (on < 0) {
        const char *e = cfg_get("MLB_SUBMIT_COPY");
        on = !e || strcmp(e, "0");
        rt_log("vk_usnap: uniform, vertex and index ranges are copied %s (MLB_SUBMIT_COPY)",
               on ? "at the submit call" : "when the replay records (MLB_SUBMIT_COPY=0)");
    }
    if (!on || n <= 0) return;
    uint64_t t0 = kern_now_ns();
    gncol = 0;
    for (int i = 0; i < n; i++)
        if (handles[i]) fz_collect(nvn_op_first(handles[i]), 0);
    qsort(gcol, (size_t)gncol, sizeof *gcol, frng_by_lo);
    plat_mutex_lock(&fz_lock);
    Frozen *f = fz_free;
    if (f) fz_free = f->next;
    int full = fz_n >= FZ_QUEUE;
    plat_mutex_unlock(&fz_lock);
    if (full) {
        __atomic_add_fetch(&F.full, 1, __ATOMIC_RELAXED);
        if (f) {
            plat_mutex_lock(&fz_lock);
            f->next = fz_free, fz_free = f;
            plat_mutex_unlock(&fz_lock);
        }
        return;
    }
    if (!f && !(f = calloc(1, sizeof *f))) rt_fatal("vk_usnap: out of memory");
    f->next = NULL;
    f->n = n;
    for (int i = 0; i < 8; i++) f->h[i] = i < n ? handles[i] : 0;
    f->done = vk_sync_fence_done();
    f->nr = 0;
    uint64_t total = 0;
    for (int i = 0; i < gncol;) {
        uint64_t lo = gcol[i].lo, hi = gcol[i].hi;
        uint32_t kind = gcol[i].kind;
        for (i++; i < gncol && gcol[i].lo <= hi; i++) {
            if (gcol[i].hi > hi) hi = gcol[i].hi;
            kind |= gcol[i].kind;
        }
        if (!fz_readable(lo, hi - lo)) {
            __atomic_add_fetch(&F.unreadable, 1, __ATOMIC_RELAXED);
            continue;
        }
        if (f->nr == f->cr) {
            int c = f->cr ? f->cr * 2 : 1024;
            FRng *r = realloc(f->r, sizeof *r * (size_t)c);
            uint64_t *o = realloc(f->off, sizeof *o * (size_t)c);
            if (!r || !o) rt_fatal("vk_usnap: out of memory");
            f->r = r, f->off = o, f->cr = c;
        }
        f->r[f->nr] = (FRng){lo, hi, kind};
        f->off[f->nr++] = total;
        total += hi - lo;
    }
    if (total > f->cap) {
        free(f->bytes);
        f->cap = total + (total >> 1);
        if (!(f->bytes = malloc(f->cap))) rt_fatal("vk_usnap: out of memory");
    }
    for (int i = 0; i < f->nr; i++)
        memcpy(f->bytes + f->off[i], (const void *)(uintptr_t)f->r[i].lo, f->r[i].hi - f->r[i].lo);
    plat_mutex_lock(&fz_lock);
    if (fz_tail) fz_tail->next = f;
    else fz_head = f;
    fz_tail = f, fz_n++;
    plat_mutex_unlock(&fz_lock);
    __atomic_add_fetch(&F.subs, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&F.ranges, (uint64_t)f->nr, __ATOMIC_RELAXED);
    __atomic_add_fetch(&F.bytes, total, __ATOMIC_RELAXED);
    __atomic_add_fetch(&F.ns, kern_now_ns() - t0, __ATOMIC_RELAXED);
}

// Replay: the copy of the submission being recorded (the oldest queued, when its handles match), NULL: none.
static Frozen *fz_take(const uint64_t *handles, int n) {
    plat_mutex_lock(&fz_lock);
    Frozen *f = fz_head;
    int match = f && f->n == n;
    for (int i = 0; match && i < 8 && i < n; i++) match = f->h[i] == handles[i];
    if (match) {
        fz_head = f->next;
        if (!fz_head) fz_tail = NULL;
        fz_n--;
    }
    plat_mutex_unlock(&fz_lock);
    if (f && !match) {
        static volatile int32_t n_logged;
        if (plat_inc32(&n_logged) <= 10)
            rt_log("vk_usnap: the oldest submit-call copy is not this submission's (%d handles, first %#llx; this one %d, "
                   "%#llx): the replay reads live memory", f->n, (unsigned long long)f->h[0], n,
                   (unsigned long long)(n ? handles[0] : 0));
        F.mismatch++;
        return NULL;
    }
    return f;
}
static void fz_release(Frozen *f) {
    if (!f) return;
    plat_mutex_lock(&fz_lock);
    f->next = fz_free, fz_free = f;
    plat_mutex_unlock(&fz_lock);
}
static const char *kind_name(uint32_t k) {
    static const char *const names[8] = {"?", "uniform", "vertex", "uniform+vertex", "index", "uniform+index",
                                         "vertex+index", "uniform+vertex+index"};
    return names[k & 7];
}
// The submit-call bytes over the replay's copy of the regions (U.reg, at dst). dst is only written: it is often device
// memory through the BAR, read back at about a microsecond a cache line (comparing a frame's 4 MiB there took 70-150
// ms); the comparison reads the live guest bytes the copy came from.
static void fz_overlay(const Frozen *f, uint8_t *dst) {
    int j = 0;
    for (int i = 0; i < U.nreg; i++) {
        const Reg *g = &U.reg[i];
        while (j < f->nr && f->r[j].hi <= g->lo) j++;
        for (int k = j; k < f->nr && f->r[k].lo < g->hi; k++) {
            uint64_t a = g->lo > f->r[k].lo ? g->lo : f->r[k].lo, b = g->hi < f->r[k].hi ? g->hi : f->r[k].hi;
            if (a >= b) continue;
            if (wr_hits(a, b, f->done)) {   // a GPU write completed since: its result is in the live bytes
                F.gpu_after++;
                continue;
            }
            uint8_t *d = dst + g->off + (a - g->lo);
            const uint8_t *live = (const uint8_t *)(uintptr_t)a;
            const uint8_t *s = f->bytes + f->off[k] + (a - f->r[k].lo);
            if (memcmp(live, s, b - a)) {
                uint64_t nd = 0;
                for (uint64_t x = 0; x < b - a; x++) nd += live[x] != s[x];
                F.changed_ranges++, F.changed_bytes += nd, F.changed_kind[f->r[k].kind & 7]++;
                static volatile int32_t n_logged;
                if (plat_inc32(&n_logged) <= 40)
                    rt_log("vk_usnap: %llu of %llu %s bytes at %#llx changed between the submit call and the replay; "
                           "the replay uses the submit call's", (unsigned long long)nd, (unsigned long long)(b - a),
                           kind_name(f->r[k].kind), (unsigned long long)a);
                memcpy(d, s, b - a);
            }
            F.used += b - a;
        }
    }
}

static void summary(void) {
    if (U.subs + U.off_subs < 3600) return;
    double s = U.subs ? (double)U.subs : 1.0;
    rt_log("vk_usnap: %llu submissions with a snapshot, %llu without; per submission %.0f ranges, %.1f KiB copied in "
           "%.3f ms, %.0f uniform bindings from the copy, %.1f dropped by GPU writes, %.1f left out (GPU writes in "
           "flight), %.1f refused (not an imported pool)",
           (unsigned long long)U.subs, (unsigned long long)U.off_subs, U.regions / s, U.bytes / s / 1024.0,
           U.copy_ns / s / 1e6, U.hits / s, U.drops / s, U.inflight / s, U.refused / s);
    U.subs = U.off_subs = U.regions = U.bytes = U.copy_ns = U.hits = U.drops = U.inflight = U.refused = 0;
}
static void fz_summary(void) {   // mlbts-native: replay, every 600 submissions with a submit-call copy
    if (__atomic_load_n(&F.subs, __ATOMIC_RELAXED) < 600) return;
    uint64_t fs = __atomic_exchange_n(&F.subs, 0, __ATOMIC_RELAXED), fr = __atomic_exchange_n(&F.ranges, 0, __ATOMIC_RELAXED),
             fb = __atomic_exchange_n(&F.bytes, 0, __ATOMIC_RELAXED), fns = __atomic_exchange_n(&F.ns, 0, __ATOMIC_RELAXED),
             ffull = __atomic_exchange_n(&F.full, 0, __ATOMIC_RELAXED),
             fun = __atomic_exchange_n(&F.unreadable, 0, __ATOMIC_RELAXED);
    double d = fs ? (double)fs : 1.0;
    rt_log("vk_usnap: submit-call copies (mlbts-native): %llu submissions, per submission %.0f ranges, %.1f KiB in %.3f ms "
           "on the game thread; %.1f KiB used per submission; changed between the submit call and the replay: %llu "
           "ranges, %llu bytes (uniform %llu, vertex %llu, index %llu, mixed %llu); %llu left to GPU writes after the "
           "submit, %llu queue full, %llu unreadable, %llu not matched",
           (unsigned long long)fs, fr / d, fb / d / 1024.0, fns / d / 1e6, F.used / d / 1024.0,
           (unsigned long long)F.changed_ranges, (unsigned long long)F.changed_bytes,
           (unsigned long long)F.changed_kind[1], (unsigned long long)F.changed_kind[2],
           (unsigned long long)F.changed_kind[4],
           (unsigned long long)(F.changed_kind[3] + F.changed_kind[5] + F.changed_kind[6] + F.changed_kind[7]),
           (unsigned long long)F.gpu_after, (unsigned long long)ffull, (unsigned long long)fun,
           (unsigned long long)F.mismatch);
    F.used = F.changed_ranges = F.changed_bytes = F.gpu_after = F.mismatch = 0;
    memset(F.changed_kind, 0, sizeof F.changed_kind);
}

static void begin_(const uint64_t *handles, int n, uint64_t fence, const Frozen *fz);
void vk_usnap_begin(const uint64_t *handles, int n, uint64_t fence) {
    Frozen *fz = fz_take(handles, n);   // mlbts-native
    fz_summary();
    begin_(handles, n, fence, fz);
    fz_release(fz);
}
static void begin_(const uint64_t *handles, int n, uint64_t fence, const Frozen *fz) {
    if (!U.mode) U.mode = rt_knob("UBO_SNAPSHOT", "1") + 1;   // stored +1: 0 is "not read yet"
    U.on = 0, U.fence = fence, U.gen++;
    summary();
    if (!RT_KNOB_ON(U.mode - 1)) {
        U.off_subs++;
        return;
    }
    uint64_t t0 = kern_now_ns();
    U.ncol = 0;
    for (int i = 0; i < n; i++)
        if (handles[i]) collect(nvn_op_first(handles[i]), 0);
    sort_col();
    uint64_t done = vk_sync_fence_done();
    wr_prune(fz && fz->done < done ? fz->done : done);
    // merge, keeping only what lies wholly in one imported pool and no GPU write in flight touches
    U.nreg = 0;
    uint64_t total = 0;
    for (int i = 0; i < U.ncol;) {
        uint64_t lo = U.col[i].lo, hi = U.col[i].hi;
        for (i++; i < U.ncol && U.col[i].lo <= hi; i++)
            if (U.col[i].hi > hi) hi = U.col[i].hi;
        VkMemSpan sp;
        if (!vk_mem_resolve(lo, hi - lo, &sp) || sp.alloc || !sp.pool || sp.avail < hi - lo ||
            (sp.cls != VK_MCLS_HOST_CACHED && sp.cls != VK_MCLS_HOST_UNC)) {
            U.refused++;
            continue;
        }
        if (wr_hits(lo, hi, done)) {
            U.inflight++;
            continue;
        }
        if (U.nreg == U.creg) U.reg = grow(U.reg, &U.creg, sizeof *U.reg);
        U.reg[U.nreg++] = (Reg){lo, hi, total, 0};
        total += (hi - lo + ALIGN - 1) & ~(uint64_t)(ALIGN - 1);
    }
    if (!U.nreg) return;
    if (!(U.arena = arena_get(total))) return;
    uint8_t *dst = U.arena->map;
    if (vk_dlaa_available()) {
        if (!U.arena->cpu && !(U.arena->cpu = malloc(U.arena->size))) rt_fatal("vk_usnap: out of memory");
        dst = U.arena->cpu;
    }
    for (int i = 0; i < U.nreg; i++)
        memcpy(dst + U.reg[i].off, (const void *)(uintptr_t)U.reg[i].lo, U.reg[i].hi - U.reg[i].lo);
    if (fz) fz_overlay(fz, dst);   // mlbts-native
    if (dst != U.arena->map) memcpy(U.arena->map, dst, total);   // one sequential write into the device copy
    vmaFlushAllocation(vk_ctx()->vma, U.arena->alloc, 0, total);   // a no-op on coherent memory
    U.on = 1;
    U.subs++, U.regions += (uint64_t)U.nreg, U.bytes += total, U.copy_ns += kern_now_ns() - t0;
}

void vk_usnap_end(uint64_t fence) {
    if (U.arena) vk_completion_add(fence, arena_done, U.arena), U.arena = NULL;
    U.on = 0, U.gen++;
}

// First region whose end is above a.
static int first_above(uint64_t a) {
    int lo = 0, hi = U.nreg;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (U.reg[m].hi <= a) lo = m + 1;
        else hi = m;
    }
    return lo;
}

int vk_usnap_find(uint64_t addr, uint64_t n, uint64_t align, VkDescriptorBufferInfo *out) {
    if (!U.on || !n) return 0;
    int i = first_above(addr);
    if (i >= U.nreg) return 0;
    const Reg *r = &U.reg[i];
    if (r->dropped || addr < r->lo || addr + n > r->hi) return 0;
    uint64_t off = r->off + (addr - r->lo);
    if (off % align) return 0;
    *out = (VkDescriptorBufferInfo){U.arena->buf, off, n};
    U.hits++;
    return 1;
}

void vk_usnap_drop(uint64_t addr, uint64_t n) {
    if (!U.mode || !n) return;
    if (U.nwr == U.cwr) U.wr = grow(U.wr, &U.cwr, sizeof *U.wr);
    U.wr[U.nwr++] = (Wrote){addr, addr + n, U.fence};
    if (!U.on) return;
    for (int i = first_above(addr); i < U.nreg && U.reg[i].lo < addr + n; i++)
        if (!U.reg[i].dropped) U.reg[i].dropped = 1, U.drops++, U.gen++;
}

uint64_t vk_usnap_gen(void) { return U.gen; }

// the host copy where there is one (DLSS), else the device copy
static uint8_t *host_copy(void) { return U.arena->cpu ? U.arena->cpu : U.arena->map; }

const uint8_t *vk_usnap_map(void) { return U.arena ? host_copy() : NULL; }

const uint8_t *vk_usnap_bytes(uint64_t addr, uint64_t n) {
    VkDescriptorBufferInfo bi;
    return vk_usnap_find(addr, n, 1, &bi) ? host_copy() + bi.offset : NULL;
}

// The arenas back on the free list (vk_exec_draw_shutdown: the device is idle, every completion ran).
void vk_usnap_shutdown(void) {
    plat_mutex_lock(&free_lock);
    for (Arena *a = free_arenas, *next; a; a = next) next = a->next, vmaDestroyBuffer(vk_ctx()->vma, a->buf, a->alloc), free(a->cpu), free(a);
    free_arenas = NULL;
    plat_mutex_unlock(&free_lock);
}
