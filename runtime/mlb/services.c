// Horizon services the runtime core does not provide (ipc.c rt_service_handler). Command ids and layouts per
// Switchbrew; our own implementation.
//
// lm: the SDK's log manager. nn::lm::Initialize (from nninitInitializeSdkModule in SDK 19.3.11) opens it, and every
// NN_LOG / SDK log line arrives as a log packet; the text goes to our log as "[lm] ...".
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "ipc.h"

// ---- lm: ILogService / ILogger
// A log packet: a 0x18-byte header (process id, thread id, flags, level, verbosity, payload size), then chunks of
// ULEB128 key, ULEB128 size, data. Key 2 is the text, 4 the file, 5 the function, 3 the line number.
enum { LM_TEXT = 2, LM_LINE = 3, LM_FILE = 4, LM_FUNCTION = 5, LM_MODULE = 6, LM_THREAD = 7 };

static uint64_t uleb(const uint8_t **p, const uint8_t *end) {
    uint64_t v = 0;
    for (int shift = 0; *p < end && shift < 64; shift += 7) {
        uint8_t b = *(*p)++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
    }
    return v;
}

static void lm_packet(const uint8_t *d, uint64_t n) {
    if (n < 0x18) return;
    uint32_t payload;
    memcpy(&payload, d + 0x14, 4);
    uint8_t level = d[0x11];
    const uint8_t *p = d + 0x18, *end = d + (0x18 + (uint64_t)payload <= n ? 0x18 + payload : n);
    const char *text = NULL, *file = NULL, *fn = NULL;
    int text_n = 0, file_n = 0, fn_n = 0;
    while (p < end) {
        uint64_t key = uleb(&p, end), size = uleb(&p, end);
        if (size > (uint64_t)(end - p)) break;
        if (key == LM_TEXT) text = (const char *)p, text_n = (int)size;
        else if (key == LM_FILE) file = (const char *)p, file_n = (int)size;
        else if (key == LM_FUNCTION) fn = (const char *)p, fn_n = (int)size;
        p += size;
    }
    if (!text) return;
    while (text_n > 0 && (text[text_n - 1] == '\n' || text[text_n - 1] == '\r' || !text[text_n - 1])) text_n--;
    if (file && (rt_trace & RT_TRACE_IPC))
        rt_logf("[lm %u] %.*s (%.*s %.*s)\n", level, text_n, text, file_n, file, fn_n, fn ? fn : "");
    else
        rt_logf("[lm %u] %.*s\n", level, text_n, text);
}

static void lm_logger_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    (void)rs;
    switch (rq->cmd) {
    case 0: {   // Log(buffer 0x21)
        const IpcBuffer *b = rq->na && rq->a[0].size ? &rq->a[0] : rq->nx ? &rq->x[0] : NULL;
        if (b && b->size) lm_packet(ipc_guest(b->addr, b->size), b->size);
        return;
    }
    case 1: return;   // SetDestination(u32)
    default: ipc_unimplemented(s, rq);
    }
}

static void lm_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    switch (rq->cmd) {
    case 0:   // OpenLogger(pid) -> ILogger
        rs->move[rs->nmove++] = ipc_session_new("lm:ILogger", lm_logger_handler, NULL, NULL);
        return;
    default: ipc_unimplemented(s, rq);
    }
}

// ---- apm: performance mode. The game runs as docked ("boost" configuration 0x00020003 is the docked default).
#define APM_CONFIG_DOCKED 0x00020003u

static void apm_session_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    switch (rq->cmd) {
    case 0: return;   // SetPerformanceConfiguration(mode, config)
    case 1: {         // GetPerformanceConfiguration(mode) -> config
        uint32_t c = APM_CONFIG_DOCKED;
        IPC_OUT(rs, c);
        return;
    }
    case 2: return;   // SetCpuOverclockEnabled(bool)
    default: ipc_unimplemented(s, rq);
    }
}

static void apm_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    switch (rq->cmd) {
    case 0:   // OpenSession -> ISession
        rs->move[rs->nmove++] = ipc_session_new("apm:ISession", apm_session_handler, NULL, NULL);
        return;
    case 1: {   // GetPerformanceMode -> 1 (boost / docked)
        uint32_t mode = 1;
        IPC_OUT(rs, mode);
        return;
    }
    case 6: {   // IsCpuOverclockEnabled
        uint8_t no = 0;
        IPC_OUT(rs, no);
        return;
    }
    default: ipc_unimplemented(s, rq);
    }
}

// ---- table
static const struct { const char *name; IpcHandler handler; } services[] = {
    {"lm", lm_handler},
    {"apm", apm_handler},
};

IpcHandler rt_service_handler(const char *name) {
    for (unsigned i = 0; i < sizeof services / sizeof *services; i++)
        if (!strcmp(services[i].name, name)) return services[i].handler;
    return NULL;
}
