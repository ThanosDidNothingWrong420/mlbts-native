// Parts of the TotK Recomp host the runtime core calls into but MLB does not have yet. Each stub either does nothing
// on purpose (said so) or stops with a named fatal, so a missing piece is found where it is first needed.
#include <stdint.h>
#include <stdio.h>

#include "audren.h"
#include "host.h"
#include "ipc.h"
#include "nfp.h"
#include "vk.h"

// ---- graphics: no back end yet
void vk_pipeline_save(void) {}   // nothing to save without a pipeline cache

// ---- services ipc.c lists in its sm: table
void audren_manager_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    (void)rs;
    ipc_unimplemented(s, rq);
}
long audren_renderers_live(void) { return 0; }

void nfp_user_manager_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    (void)rs;
    ipc_unimplemented(s, rq);
}

// ---- memory.c: TotK grows its texture heap for HD texture packs; MLB adds nothing to the application pool
uint64_t res_heap_extra_bytes(void) { return 0; }
uint64_t res_tex_heap_extra_bytes(void) { return 0; }

// ---- svc.c ExitProcess: nothing to flush until play time and the profiler are ported
void playtime_save(void) {}
void prof_write(void) {}

// ---- kernel.c: the frame clock (TOTK_TRACE=sched lines) and the main-thread profiler mark
int64_t frame_step_world_us(void) { return -1; }   // -1: no game time yet (host.h)
void perf_main_thread(void) {}
