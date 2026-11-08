// NVN procs MLB calls that TotK Recomp's front end leaves to nvn_procs.c's "not implemented" defaults. Our own, on
// TotK's object layouts (nvn_obj.h) and back end interface (gfx.h); NVN semantics per the NVN headers' documented
// behaviour as TotK's nvn.h records it.
#include <stdint.h>

#include "gfx.h"
#include "host.h"
#include "nvn_api.h"

// void nvnSamplerPoolRegisterSamplerBuilder(const NVNsamplerPool *sp, int id, const NVNsamplerBuilder *b): registers
// the sampler the builder describes, without an NVNsampler object in between
NVN_API(nvnSamplerPoolRegisterSamplerBuilder) {
    gfx_backend()->sampler_register((int)arg_x(c, 1), &((const samp_builder_t *)arg_p(c, 2))->p);
}

// void nvnQueueWaitSync(NVNqueue *q, const NVNsync *s): later commands on q wait until s is signalled. The back end
// replays every queue's submissions in submission order on one Vulkan queue (vk_backend.c), so work fenced before
// this call is already ordered before what follows; as TotK's nvnCommandBufferWaitSync, nothing to record. Not
// covered: a wait for a sync another queue only fences later (the console would stall q until then; here q's later
// work runs first) [H: not seen yet].
NVN_API(nvnQueueWaitSync) {
    const sync_t *s = arg_p(c, 1);
    if (rt_trace & RT_TRACE_GFX) rt_log("nvn: QueueWaitSync on a %s sync", s && s->fenced ? "fenced" : "not yet fenced");
}
