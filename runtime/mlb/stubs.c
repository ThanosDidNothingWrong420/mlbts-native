// Parts of the TotK Recomp host the runtime core calls into but MLB does not have yet. Each stub either does nothing
// on purpose (said so) or stops with a named fatal, so a missing piece is found where it is first needed.
#include <stdint.h>
#include <stdio.h>

#include "host.h"
#include "ipc.h"
#include "nfp.h"


// ---- services ipc.c lists in its sm: table
void nfp_user_manager_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    (void)rs;
    ipc_unimplemented(s, rq);
}

// ---- memory.c: TotK grows its texture heap for HD texture packs; MLB adds nothing to the application pool
uint64_t res_heap_extra_bytes(void) { return 0; }
uint64_t res_tex_heap_extra_bytes(void) { return 0; }

// ---- svc.c ExitProcess: nothing to flush until play time is ported (prof_write: core/perf.c)
void playtime_save(void) {}

// ---- kernel.c: the frame clock (TOTK_TRACE=sched lines); the main-thread profiler mark is core/perf.c
int64_t frame_step_world_us(void) { return -1; }   // -1: no game time yet (host.h)
