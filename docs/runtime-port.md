# Runtime port: status and layout

How `mlb.exe` is built from the game's code and TotK Recomp's runtime, and how far it boots. Updated 2026-11-08.

## Layout

| Path | What | Origin |
|---|---|---|
| `recomp/` | ARM64 → C generator (lifter, NEON/FP lowering, jump tables, flags/register caching, module layout) | TotK Recomp `ae30a61`, unmodified |
| `runtime/core/` | Generic host: CPU/SIMD headers, dispatch, host-function table, NSO loader, guest memory, kernel/threads (3 virtual cores), SVCs, IPC, platform (Win32), audio-renderer ABI header | TotK Recomp `ae30a61`; mlbts-native changes listed below |
| `runtime/mlb/` | MLB layer: `main.c` (log, crash reporter, runtime services of the generated code), `boot.c` (rtld replacement, `nn::init::Start`), `cfg.c` (environment settings), `services.c` (IPC services the core lacks), `stubs.c` (named stubs) | ours (main/boot/cfg adapted from TotK, attributed in headers) |
| `tools/build.py` | Writes `work/build/modules.c` and `build.ninja`, compiles with LLVM clang, links `work/build/mlb.exe` with lld | ours |
| `tools/data/expected-1.0.15.json` | ExeFS hashes the loader checks | ours |

The generic/game split follows AGENTS.md: `runtime/core` holds nothing MLB-specific, and `runtime/mlb` holds nothing
that another title couldn't replace.

### Changes to vendored files (all marked `mlbts-native` in the source)

- `runtime/core/ipc.c`, `ipc.h`: **CMIF domains** (ConvertToDomain, CopyFromCurrentDomain, domain requests and
  close, interfaces returned as object ids). TotK never needed them, because its symbol overrides catch service use
  first. MLB's SDK 19.3.11 converts `lm` to a domain inside `nninitInitializeSdkModule`. Plus the
  `rt_service_handler(name)` hook, so the game layer can add services without editing `ipc.c`'s table.

## Building and running

```bash
.venv/Scripts/python -I recomp/gen.py --exefs work/update/exefs --out work/gen --modules main,sdk
.venv/Scripts/python -I tools/build.py
work/build/mlb.exe
```

Generation takes about 50 s. A clean build takes about 2.5 min with 10 parallel jobs; after that, runtime-only
changes relink in seconds. Settings come from the environment: `MLB_TRACE=svc,host,ipc,fs,sched,all`,
`MLB_EXEFS`/`MLB_ROMFS`/`MLB_PROFILE`, and every TotK `TOTK_<X>` key as `MLB_<X>`. The log goes to stderr and to
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
5. `nn::oe::InitializeApplet` → `nn::am::InitializeApplicationInterfaces` opens **`appletOE`**. ✘ **Current stop:**
   `sm: service "appletOE" is not implemented`.

Still ahead: the rest of SDK init, module constructors (`DT_INIT` of `main`, then `sdk`), `nnMain`, then the game's
own use of fs (RomFS + save), hid, audio, time, account, NVN bootstrap and so on.

## Approach for services

TotK replaces about 160 `nn::*` functions by symbol (`HOST_OVERRIDE`), which works only for calls through a GOT,
that is, from `main` into `sdk`. MLB's SDK initialises oe/am/apm/lm itself, through direct calls inside `sdk`, so
those services are implemented at IPC level, in `runtime/mlb/services.c` and later one file per service.
Power Moon Recomp's `runtime/services/` (MIT) is the closest reference for that style. Symbol overrides remain the
tool for calls from `main` (fs, hid, audio, NVN).
