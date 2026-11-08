# Runtime port: status and layout

How `mlb.exe` is built from the game's code and TotK Recomp's runtime, and how far it boots. Updated 2026-11-08.

## Layout

| Path | What | Origin |
|---|---|---|
| `recomp/` | ARM64 → C generator (lifter, NEON/FP lowering, jump tables, flags/register caching, module layout) | TotK Recomp `ae30a61`, unmodified |
| `runtime/core/` | Generic host: CPU/SIMD headers, dispatch, host-function table, NSO loader, guest memory, kernel/threads (3 virtual cores), SVCs, IPC, platform (Win32), audio-renderer ABI header | TotK Recomp `ae30a61`; mlbts-native changes listed below |
| `runtime/mlb/` | MLB layer: `main.c` (log, crash reporter, runtime services of the generated code), `boot.c` (rtld replacement, `nn::init::Start`, NPDM service list), `cfg.c` (environment settings), `services.c` (service table; lm, apm, ectx:aw, pctl), `am.c` (appletOE), `aoc.c` (aoc:u), `account.c` (local user id), `stubs.c` (named stubs) | ours (main/boot/cfg adapted from TotK, attributed in headers) |
| `tools/build.py` | Writes `work/build/modules.c` and `build.ninja`, compiles with LLVM clang, links `work/build/mlb.exe` with lld | ours |
| `tools/data/expected-1.0.15.json` | ExeFS hashes the loader checks | ours |

The generic/game split follows AGENTS.md: `runtime/core` holds nothing MLB-specific, and `runtime/mlb` holds nothing
that another title couldn't replace.

### Changes to vendored files (all marked `mlbts-native` in the source)

- `runtime/core/ipc.c`, `ipc.h`: **CMIF domains** (ConvertToDomain, CopyFromCurrentDomain, domain requests and
  close, interfaces returned as object ids). TotK never needed them, because its symbol overrides catch service use
  first. MLB's SDK 19.3.11 converts `lm`, `appletOE` and `pctl` to domains inside `nninitInitializeSdkModule`. Plus
  the `rt_service_handler(name, &stable_name)` hook, so the game layer can add services without editing `ipc.c`'s
  table.
- `ipc.c` sm: **the NPDM's service access list is enforced** (`rt_service_allowed`, read from main.npdm's ACI0 by
  `runtime/mlb/boot.c`). Names outside it get `NotAllowed` (0x1015), as on Horizon; the SDK probes `pctl:s`,
  `pctl:a`, `pctl:r` and falls back to `pctl`. The TIPC reply keeps its move-handle slot (0) on failure, because
  SDK 19.3.11's `nn::sm::GetServiceHandle` reads the handle from word 3 and the result from word 4 without looking
  at the header **[C, disassembly]**. Without the slot, it read a failure as success with handle 0.
- `runtime/core/memory.c`: **MapMemory (svc 0x04) accepts a module's writable data as the source.** SDK 19.3.11's
  system worker thread (`nn::os::RegisterSystemWorkerHandler`, from `nn::oe::SetupGpuErrorHandler`) keeps its
  0x1000-byte stack in sdk's `.bss` and aliases it into the stack region; TotK's version allowed only heap and alias
  sources, so `MapAliasStack` failed 64 times and the SDK aborted. The pages read as no-access while moved.
- `runtime/core/kernel.c`, `host.h`: `kern_event_clear`, so am can clear its message event when the queue empties.

## Building and running

```bash
.venv/Scripts/python -I recomp/gen.py --exefs work/update/exefs --out work/gen --modules main,sdk
.venv/Scripts/python -I tools/build.py
work/build/mlb.exe
```

Generation takes about 50 s. A clean build takes about 2.5 min with 10 parallel jobs; after that, runtime-only
changes relink in seconds. Settings come from the environment: `MLB_TRACE=svc,host,ipc,fs,sched,all`,
`MLB_EXEFS`/`MLB_ROMFS`/`MLB_PROFILE`, `MLB_LANGUAGE` (default `en-US`), and every TotK `TOTK_<X>` key as `MLB_<X>`. The log goes to stderr and to
`work/build/logs/mlb.log`.

The toolchain is LLVM clang 23 (`x86_64-pc-windows-msvc`) with lld and Ninja from the venv. MSYS2 is not needed.
`clang_rt.builtins` is linked explicitly.

## Boot progress (1.0.15)

1. `mem_init` reserves the guest address space; the loader verifies the ExeFS hashes and maps `main`
   (0x7000000000) and `sdk` (0x7004c00000). It applies 122,737 + 33,849 relocations and binds `main`'s 765
   imports. ✔
2. The guest main thread (priority 44, core 0, 1 MiB stack, matching the NPDM) runs `nn::init::Start`; the rtld
   `loader_ready` callback fires. ✔
3. `nninitInitializeSdkModule` → `nn::lm::Initialize` opens **`lm`** and converts it to a domain. ✔ The SDK's
   log packets are decoded into `[lm]` lines.
4. `nn::oe::Initialize` → `ApmInitialize` opens **`apm`**. ✔
5. `nn::oe::InitializeApplet` → `nn::am::InitializeApplicationInterfaces` opens **`appletOE`** (`runtime/mlb/am.c`):
   the application proxy and its sub-interfaces. The SDK reads the applet resource user id, sets focus handling,
   takes the GPU-error event (its system worker thread starts here, see the memory.c change), and receives the
   initial `FocusStateChanged` message. ✔ We present a docked console in focus: operation mode console, performance
   mode boost, 1920x1080. A preselected user (MLB's NACP requires a startup account) comes from
   `<profile>/account.uid` (`runtime/mlb/account.c`).
6. `nn::aoc::detail::InitializeForHipc` opens **`aoc:u`**: no add-on content. ✔
7. `nn::err::InitializeContextWriterApi` opens **`ectx:aw`**. ✔
8. `nn::pctl::detail::ipc::InitializeIpcSessionIfNeeded` probes `pctl:s/a/r` (NotAllowed) and opens **`pctl`**: no
   restrictions. ✔
9. `nntimeStartup` → `nn::time::...::GetUserStaticServiceSharedPointer` opens **`time:u`**. ✘ **Current stop:**
   `sm: service "time:u" is not implemented`. This one needs kernel shared memory (svc 0x13 MapSharedMemory): since
   SDK 6 the clocks are read from the time service's shared memory page. TotK replaced `nn::time` by symbol, which
   cannot catch this call from inside sdk.

Commands each service has not been seen to use stop with a named fatal (`ipc_unimplemented`), so every new one is
found where it is first needed; commands that only pass a setting are accepted and logged under `MLB_TRACE=ipc`.

`tools/disasm.py` disassembles a module range or symbol (with PLT call targets named) for reading SDK code paths
such as the two above.

Still ahead: the rest of SDK init, module constructors (`DT_INIT` of `main`, then `sdk`), `nnMain`, then the game's
own use of fs (RomFS + save), hid, audio, time, account, NVN bootstrap and so on.

## Approach for services

TotK replaces about 160 `nn::*` functions by symbol (`HOST_OVERRIDE`), which works only for calls through a GOT,
that is, from `main` into `sdk`. MLB's SDK initialises oe/am/apm/lm itself, through direct calls inside `sdk`, so
those services are implemented at IPC level, in `runtime/mlb/services.c` and later one file per service.
Power Moon Recomp's `runtime/services/` (MIT) is the closest reference for that style. Symbol overrides remain the
tool for calls from `main` (fs, hid, audio, NVN).
