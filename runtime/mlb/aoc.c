// aoc:u: add-on content. Command ids and layouts per Switchbrew ("NS services", IAddOnContentManager); our own
// implementation. No add-on content is installed: counts are 0, lists are empty, and the events never fire.
// SDK 19.3.11's nninitInitializeSdkModule opens it (nn::aoc::detail::InitializeForHipc).
#include <string.h>

#include "host.h"
#include "ipc.h"

#define AOC_BASE_ID 0x0100AA7024095000ull   // application id 0x0100AA7024094000 + 0x1000

static uint32_t event_copy(uint32_t *w) {
    if (!*w) *w = kern_event_new();
    return kern_event_readable(*w);
}

static void purchase_event_manager(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    static uint32_t purchased;
    switch (rq->cmd) {
    case 0: return;   // SetDefaultDeliveryTarget(pid, buffer)
    case 1: return;   // SetDeliveryTarget(u64, buffer)
    case 2: rs->copy[rs->ncopy++] = event_copy(&purchased); return;   // GetPurchasedEventReadableHandle
    default: ipc_unimplemented(s, rq);
    }
}

void aoc_u_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    static uint32_t list_changed;
    switch (rq->cmd) {
    case 2: {   // CountAddOnContent(pid) -> u32
        uint32_t n = 0;
        IPC_OUT(rs, n);
        return;
    }
    case 3: {   // ListAddOnContent(u32 offset, u32 count, pid, buffer 6) -> u32 listed
        uint32_t n = 0;
        IPC_OUT(rs, n);
        return;
    }
    case 5: {   // GetAddOnContentBaseId(pid) -> u64
        uint64_t id = AOC_BASE_ID;
        IPC_OUT(rs, id);
        return;
    }
    case 8: case 10:   // GetAddOnContentListChangedEvent, ...WithProcessId(pid)
        rs->copy[rs->ncopy++] = event_copy(&list_changed);
        return;
    case 11: case 12: return;   // NotifyMountAddOnContent, NotifyUnmountAddOnContent
    case 50: return;            // CheckAddOnContentMountStatus(pid): nothing mounted, nothing lost
    case 100: case 101:         // CreateEcPurchasedEventManager, CreatePermanentEcPurchasedEventManager
        rs->move[rs->nmove++] = ipc_session_new("aoc:IPurchaseEventManager", purchase_event_manager, NULL, NULL);
        return;
    default: ipc_unimplemented(s, rq);
    }
}
