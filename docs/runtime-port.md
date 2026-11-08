# Runtime port: status and layout

How `mlb.exe` is built from the game's code and TotK Recomp's runtime, and how far it boots. Updated 2026-11-08.

## Layout

| Path | What | Origin |
|---|---|---|
| `recomp/` | ARM64 → C generator (lifter, NEON/FP lowering, jump tables, flags/register caching, module layout) | TotK Recomp `ae30a61`, unmodified |
| `runtime/core/` | Generic host: CPU/SIMD headers, dispatch, host-function table, NSO loader, guest memory, kernel/threads (3 virtual cores), SVCs, IPC, platform (Win32), audio-renderer ABI header | TotK Recomp `ae30a61`; mlbts-native changes listed below |
| `runtime/mlb/` | MLB layer: `main.c` (log, crash reporter, runtime services of the generated code), `boot.c` (rtld replacement, `nn::init::Start`, NPDM service list), `cfg.c` (environment settings), `services.c` (service table; lm, apm, ectx:aw, pctl), `am.c` (appletOE), `aoc.c` (aoc:u), `time.c` (time:u), `game.c` (NACP figures for fs), `account.c` (local user id), `stubs.c` (named stubs) | ours (main/boot/cfg adapted from TotK, attributed in headers) |
| `runtime/gfx/` | TotK's graphics stack: NVN front end, Vulkan back end, present and Win32 window, shader client (docs/graphics-plan.md); `gfx_stubs.c` switches off the TotK-only features; `nvn_api_more.c` has NVN procs TotK never implemented | TotK Recomp `ae30a61`; changes marked `mlbts-native` |
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
- `memory.c`, `kernel.c`, `svc.c`, `host.h`: **shared memory** (svc 0x13 MapSharedMemory, 0x14 UnmapSharedMemory,
  handle kind `KERN_SHMEM`). A host service owns each block (`mem_shmem_new`). Guest addresses are host addresses
  and the guest regions are plain reservations, so a mapping cannot alias the block's pages: it is committed in the
  stack/heap block or the alias region and filled with a copy, and `mem_shmem_write` updates the block and every
  mapping. That fits blocks the host writes and the guest reads (time now; hid and applet resources later). Guest
  writes stay in their own mapping (`mem_shmem_read` reads the first one back). QueryMemory reports mappings as
  Shared, and heap growth, stack moves and MapPhysicalMemory refuse to overlap them.
- `memory.c` QueryMemory: **host-owned gaps read Inaccessible**, not TotK's Code/RX. SDK 19.3.11's
  `nn::ro::detail::GetExceptionInfo` (the process name for log observers, found from `nnMain`'s address) walks back
  from a code address while the regions read Code with R or X **[C, disassembly]**. The Code gap below `main` made
  `main`'s "module" start at the gap's base, and `FindModuleHeader` read unmapped host memory there.
- `runtime/core/fs_host.c` (new, from TotK `fs_host.c`) and `fs.h`: **nn::fs by symbol override**, the TotK way:
  `main` imports 29 `nn::fs` functions and calls them through its GOT. Save, temporary and cache storage are TotK's
  host directories with the journaled save commit. Changes:
  - the RomFS is a read-only mount kind of the extracted directory instead of TotK's indexed `romfs.bin`
    (`romfs.c` is not vendored), so `OpenDirectory` works on it too;
  - the title's id and save/temporary sizes come from `fs_title` (`runtime/mlb/game.c`, from the NACP);
  - MLB's extra overloads are added: `GetFreeSpaceSize`, `ExtendSaveData`/`GetSaveDataSize` (sizes kept in
    `<profile>/save-size`), `MountCacheStorage(name)`, `DeleteDirectory`, `GetDirectoryEntryCount`,
    `QueryMountRomCacheSize(size_t *, int)`, `EnableGlobalFileDataCache`;
  - the probe checks the RomFS mount against `romfs_list.tsv`.

## Building and running

```bash
.venv/Scripts/python -I recomp/gen.py --exefs work/update/exefs --out work/gen --modules main,sdk
.venv/Scripts/python -I tools/build.py
work/build/mlb.exe
```

The game's RomFS must be extracted first (37.4 GB, 84,634 files; about 3.5 minutes at 190 MiB/s). Extraction is
resumable, and it writes `work/romfs/romfs.complete` only once every listed file is there at its size; `MountRom`
refuses to start without that marker:

```bash
.venv/Scripts/python -I tools/romfs_dump.py --keys "$APPDATA/Ryujinx/system/prod.keys" --titlekeys "$APPDATA/Ryujinx/system/title.keys" --extract-all "gamefiles/<base>.nsp" "gamefiles/<update>.nsp" work/romfs
```

`work/build/mlb.exe --probe-fs` checks the `nn::fs` overrides without running guest code. It covers all 84,634
RomFS entries, 64 sampled files read whole through each `ReadFile` overload and compared with an independent read, a
RomFS listing, refused RomFS writes, and TotK's save-transaction and storage checks in a temporary profile (including
a crash at each step of a Commit). Status: 108 of 108 checks pass.

Generation takes about 50 s. A clean build takes about 2.5 min with 10 parallel jobs; after that, runtime-only
changes relink in seconds. Settings come from the environment: `MLB_TRACE=svc,host,ipc,fs,sched,all`,
`MLB_EXEFS`/`MLB_ROMFS`/`MLB_PROFILE`, `MLB_LANGUAGE` (default `en-US`), `MLB_TIMEZONE` (`local`, the default, or `utc`), and every TotK `TOTK_<X>` key as `MLB_<X>`. The log goes to stderr and to
`work/build/logs/mlb.log`.

The toolchain is LLVM clang 23 (`x86_64-pc-windows-msvc`) with lld and Ninja from the venv. MSYS2 is not needed.
`clang_rt.builtins` is linked explicitly. Graphics needs two more steps, once each:

```bash
.venv/Scripts/python -I tools/fetch_deps.py
.venv/Scripts/python -I tools/build_xlate.py
```

The first fetches the pinned Vulkan headers and VMA into `work/deps` and generates `vulkan-1.lib` from the driver's
loader. The second builds the shader translator `work/xlate/totk_xlate.exe` with .NET 10 NativeAOT; it needs VS 2022's
linker. `tools/shader_smoke.py` translates a sample of MLB's FXB shaders offline: 66 of 66 translate.

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
9. `nntimeStartup` opens **`time:u`** (`runtime/mlb/time.c`), takes the time zone service and the service's shared
   memory, and maps it read-only. ✔ From then on the SDK reads the clocks from that page without IPC. Its layout is
   taken from SDK 19.3.11's `nn::time::detail::SharedMemoryClient` **[C, disassembly]**:
   - local (user) and network clock contexts at 0x38 and 0x80, each a lock-free pair of 0x20-byte
     `SystemClockContext`;
   - the steady clock at 0xD0, a pair of 0x38-byte adjustment points {base, scale, shift, offset, limit, source id}.
     The steady time point is `(now_ns + clamp(((now_ns - base) * scale >> shift) + offset, limit)) / 1e9`.

   We write a steady clock equal to the guest tick (a new source id each run, as after a reboot) and system clocks
   equal to the host's UTC time. The time zone is the host's or UTC (`MLB_TIMEZONE`). `ToCalendarTimeWithMyRule`
   and `ToPosixTimeWithMyRule` are served; explicit time-zone rules (`LoadTimeZoneRule`) are not yet.
10. `nn::init::Start` runs **module constructors**: `main` DT_INIT (1,025 init_array entries), then `sdk` (87). Both
    leave their constructor flag at 2. ✔
11. **`nnMain` runs.** ✔ Its first SDK call was `nn::fs::QueryMountRomCacheSize`, which went to `fsp-srv` until
    the `nn::fs` overrides took it.
12. **fs**: the game mounts the RomFS as `FS_Rom` and temporary storage as `FS_Temp`, reads
    `FS_Rom:/BOOT/CONFIG_MAIN.TXT`, and logs its own start-up lines (`[lm 0] Compiler: ...`). ✔
13. **Graphics bring-up** (docs/graphics-plan.md). The `nv::` and `nn::vi::` overrides take the place of `nvdrv`, and
    `nvnBootstrapLoader` hands out TotK's NVN procs. The game creates its device, records command buffers and **presents
    its first frame** through Vulkan on the RTX 3060 Ti. ✔ Two procs TotK never needed were added:
    `nvnSamplerPoolRegisterSamplerBuilder` and `nvnQueueWaitSync`.
14. Network setup opens **`nifm:u`** (`nn::nifm`, from `main+0x151db04`). ✘ **Current stop:**
    `sm: service "nifm:u" is not implemented`.

Commands each service has not been seen to use stop with a named fatal (`ipc_unimplemented`), so every new one is
found where it is first needed; commands that only pass a setting are accepted and logged under `MLB_TRACE=ipc`.

`tools/disasm.py` disassembles a module range or symbol (with PLT call targets named) for reading SDK code paths
such as the ones above.

Still ahead: nifm (offline), more of the game's frames and their correctness, hid, audio, account, and the save path as the game first uses it.

## Approach for services

TotK replaces about 160 `nn::*` functions by symbol (`HOST_OVERRIDE`), which works only for calls through a GOT,
that is, from `main` into `sdk`. MLB's SDK initialises oe/am/apm/lm itself, through direct calls inside `sdk`, so
those services are implemented at IPC level, in `runtime/mlb/services.c` and later one file per service.
Power Moon Recomp's `runtime/services/` (MIT) is the closest reference for that style. Symbol overrides remain the
tool for calls from `main` (fs, hid, audio, NVN).
