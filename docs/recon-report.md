# MLB The Show 26 (Switch) — Initial Technical Reconnaissance

Date: 2026-11-08. Target: **1.0.15** (update v983040). All findings come from the local NSPs, analysed with
the scripts in `tools/` (see [Reproducing](#reproducing)). Each finding is tagged:

- **[C]** confirmed: read directly from the binaries or metadata
- **[I]** inferred: strong evidence, but not verified end to end
- **[H]** hypothesis: needs verification

---

## 1. Title, version and packaging

| Item | Value |
|---|---|
| Application ID | `0100AA7024094000` **[C]** |
| Patch ID | `0100AA7024094800` **[C]** |
| Base CNMT version | 65536 (v1); NACP display version `1.0.1` **[C]** |
| Update CNMT version | 983040; NACP display version **`1.0.15`** **[C]** |
| Publisher (NACP) | MLB Advanced Media; languages: AmericanEnglish only (`supported_language_flag=0x1`) **[C]** |
| Key generation | 21 (all NCAs) **[C]** |
| NCA SDK addon version | 20.5.19.0 (content NCAs), 22.2.0.0 (meta NCAs) **[C]** |
| Required system version | base `0x54200000` (21.2.0), update `0x58500000` (22.5.0) **[C]** |
| Add-on content base ID | `0100AA7024095000` (DLC: out of scope) **[C]** |

Save and storage sizes from the NACP **[C]**: user-account save 62 MiB (journal 5 MiB, max 134 MiB), no device
save, no cache storage, temporary storage 276 MiB (`0x11400000`). Startup user account is required (`1`).

### Base vs. update: the base is a sparse NCA

This is the most important packaging finding, and the reference projects do not handle it.

- The base Program NCA (`6fb4c1ca…`, 3.87 GiB) is **sparse** **[C]**:
  - ExeFS section: the sparse table has **0 entries**, so the base contains **no code at all**.
  - RomFS section: sparse (265 entries) **and** compressed storage (102,826 bucket entries). Its virtual range is
    6.35 GiB, but only 3.87 GiB is physically present.
- The update Program NCA (`fd9d3dbb…`, 26.8 GiB) has a full ExeFS **[C]**. Its RomFS layers an IndirectStorage
  (29,139 entries) and AesCtrEx (18,974 entries) under the IVFC tree, with compressed storage (469,420 entries:
  430,896 LZ4, 38,524 stored) on top **[C]**.
- Consequence: the ExeFS comes from the update alone. The RomFS needs the whole chain. In Atmosphère's
  `NcaFileSystemDriver` order: patch body → AesCtrEx; base body → Sparse → AesCtr as the "original"; → Indirect →
  IVFC data level → Compressed → RomFS.
- **hactool cannot do this [C].** `SciresM/hactool` at its latest commit `1d64a83` (Oct 2023, also the commit TotK
  pins) has BKTR (indirect + AesCtrEx) but no sparse or compressed NCA storage. TotK's hactool patches don't add
  them. Their pipeline cannot extract this title.
- **Resolved:** `tools/ncastorage.py` + `tools/romfs_dump.py` implement the chain (behaviour taken from Atmosphère's
  fssystem, read only; independent code) **[C]**. Verification: 64 random IVFC data blocks plus the first and last
  verify all the way to the master hash, and 200 more blocks taken from regions mapped to the sparse base also
  verify. Of the 30.80 GiB indirect space, 3.99 GiB comes from the base and 26.81 GiB from the patch.
- The update CNMT lists 6 DeltaFragment NCAs that are not in the NSP. This is normal for dumps and not needed **[C]**.

## 2. Executable modules (1.0.15 ExeFS)

| Module | NSO size | Mapped image | .text | SHA-256 (NSO file) |
|---|---|---|---|---|
| `main` | 23,495,263 | 0x4B32000 (75.2 MiB; bss 34.2 MiB) | 0x1C21340 (28.1 MiB) | `9bbdacf9…f1270b` |
| `sdk` | 6,247,676 | 0xC5E000 (12.4 MiB) | 0x64E230 (6.3 MiB) | `0449487b…1afbab` |
| `rtld` | 8,550 | 0x5000 | | `714ead44…e9d523` |
| `main.npdm` | 1,604 | | | `1de40581…f762a4` |

Full hashes and module IDs are in `work/update/summary.json` and `work/analysis/*.json`.

- **No `subsdk*` modules** **[C]**. The game is a single `main` that links only `nnSdk.nss` (DT_NEEDED). That is
  simpler than TotK (main + subsdk0 + sdk).
- `main` module ID: `0e2cbf49797b3e80ae82965e0917165f0fc1e59c` **[C]**.
- `main`: 890 dynamic symbols, 765 of them undefined (imported from sdk), 124 exported. Relocations: RELA
  (ABS64 482, GLOB_DAT 42), JMPREL (JUMP_SLOT 724), RELR (3,017 words) **[C]**. TotK's lifter already handles RELR,
  which arrived with its 1.4.3 SDK.
- `.eh_frame_hdr` lists 93,330 FDEs in `main` and 32,097 in `sdk`. That is a good seed for function boundaries **[C]**.
- Build path strings show `c:/Jenkins/workspace/MLB26PATCH/BRANCHES/MLB26_PATCH/MLBPS3/SRC/...` **[C]**.

## 3. CPU / ARM64 profile

Capstone census over every `.text` word (`work/analysis/*.isa.json`) **[C]**:

| | `main` | `sdk` |
|---|---|---|
| Instruction words | 7,374,032 | 1,652,876 |
| Distinct encodings | 1,205,725 | 337,927 |
| Distinct mnemonics | 318 | 309 |
| Scalar FP | 499,842 | 31,299 |
| NEON vector | 25,605 | 15,263 |
| Exclusives (ldxr/stxr family) | ~3,600 | ~1,800 |
| LSE atomics (`cas`, `ldadd`, `swp`…) | **0** | **0** |
| Crypto (AES/SHA/PMULL) | **0** | ~1,500 |
| CRC32 | 22 | 48 |
| `svc` | **0** | 58 |
| `br` (jump tables / tail calls) / `blr` | 8,104 / 38,320 | 10,384 / 12,715 |
| FMA (`fmadd`/`fmsub`/`fnm*`) | 30,282 | 2,225 |
| System registers | `TPIDR_EL0` ×2543, `FPCR` ×37, `NZCV` ×14 | `TPIDRRO_EL0` ×123, `CNTPCT_EL0` ×40, FPCR/FPSR |

What this means:

- The profile is plain **ARMv8.0-A (Cortex-A57)**: no LSE, no SVE, no v8.1+ features in `main` **[C]**. The only
  later-architecture encodings are a few PAC hint-space instructions in sdk (`autia1716`, `autib1716`,
  `xpaclri`), which execute as NOPs on v8.0, and one `stg` decode that is almost certainly data. TotK's lifter targets the same
  class (it supports `LDXP/STXP` and `CASP`), so the instruction surface should be similar **[I]**.
- `main` makes **no SVC calls**. Every OS interaction goes through `sdk` **[C]**.
- Heavy `thread_local` use (2,543 `TPIDR_EL0` reads, `_ZTH*` TLS wrappers such as `g_isMLBThread` and
  `Engine::NVN::EngineGraphicsContext::s_pEngineGraphicsContext`) **[C]**. The TLS model must be right from day one.
- FMA is common (~30k), so fused rounding must be exact, as in TotK `runtime/fma.c` **[C]**.
- **TotK lifter coverage oracle: 100% [C].** TotK Recomp's `recomp/lifter.py` (commit `ae30a61`, `simd='c'`)
  lifts every decodable word: 7,374,030 in `main` and 1,652,874 in `sdk`, with 0 `Unsupported`. A second pass
  (`work/scratch/oracle_check.py`) confirms the only `trap` results are the real `udf`/`brk` words (40,395 and
  26,108) and that no instruction falls back to inline asm. Caveats: this checks per-instruction lifting only. It
  does not cover semantic correctness (TotK's Unicorn differential tests), function discovery, or jump-table recovery
  on MLB's code.

### 3a. Full lifter pipeline on MLB (TotK `recomp/gen.py`, unmodified) **[C]**

`gen.py --exefs work/update/exefs --out work/gen --modules main,sdk` takes 51 s on the 5600X (8 workers):

| | `main` | `sdk` |
|---|---|---|
| Functions discovered (seeds: `.eh_frame_hdr`, exports, RELATIVE targets, BL targets) | 94,980 (93,415 seeds, 2 rounds) | 40,069 (32,105 seeds) |
| Generated C | 497 MB in 198 chunks | 115 MB in 40 chunks |
| Jump tables resolved (targets) | 3,659 (34,904) | 574 (4,079) |
| All-label fallbacks (correct but slower) | 100 | 30 |
| `br` through `rt_dispatch` | 4,345 | 9,780 |
| Unsupported / external branches / external calls | 0 / 0 / 0 | 0 / 0 / 0 |

- The dispatched `br` sites look legitimate. In `main`: PLT stubs (`br x17`, 735), indirect tail calls after an
  epilogue (`ldp; br`, about 1,528) and `ldr; br` virtual tail calls. In `sdk`, almost all are PLT stubs (7,973
  `br x17`). The `add; br` jump-table pattern (3,779 in `main`) matches the resolved tables plus fallbacks.
- No spin-wait gates were emitted, since there's no MLB spin list yet (TotK's `tools/spin_waits.py`), and no patch sets.
- **Compiles and links with the installed toolchain:** LLVM clang 23.1.3 (`x86_64-pc-windows-msvc`, no MSYS2) using
  TotK's generated-code flags (`-O1 -march=x86-64-v3 …`). All 240 files compile in 2 min 29 s (10 jobs) with 0
  errors and 0 warnings, giving 170 MB of objects. Linked with lld against a stub runtime (`tools/linktest/`), the
  139 MB exe starts and reports its dispatch tables (94,980 + 40,069 functions).
- **Host ABI of the lifted code:** only `rt_dispatch`, `rt_svc`, `rt_brk`, `rt_udf`, `rt_ticks`, `fnprof_hit` and
  `g_fnprof`, plus the CRT's `__chkstk` and `_fltused`. Imports between modules go through the GOT/PLT at run time,
  so the host loader must apply relocations and bind `main`'s imports to `sdk` exports (or to HLE overrides).
- Not covered: running any of it, semantic correctness (TotK's Unicorn differential tests on these encodings), and
  whether the 100 + 30 fallbacks or any jump table is mis-bounded.

## 4. OS surface: SDK, imports, services, SVCs

### SDK and middleware (from `SDK MW+…` strings) **[C]**

- `NintendoSdk_nnSdk-19_3_11-Release`, `NintendoSDK_NVN-19_3_11`, `NintendoSDK_libz-19_3_11` (in sdk)
- `NintendoSDK_libcurl-19_3_11` (in main)

The **SDK is 19.3.11**. TotK 1.4.3 uses SDK 20.5.22, TotK 1.0–1.2.1 use older ones, and Astral Chain uses a much older
one. Symbol-level HLE written against another SDK must be re-checked against 19.3.11 struct layouts **[I]**.

### `main` → SDK imports (765 symbols; list in `work/analysis/main.imports.demangled.txt`) **[C]**

| Group | Count | Group | Count |
|---|---|---|---|
| libc / POSIX (nnmusl: stdio, math, pthread, setjmp…) | 286 | nn::account (incl. NSA/NA auth) | 22 |
| libc++ (`std::__1`) | 129 | nn::hid (Npad, vibration, touch, controller applet) | 21 |
| nn C API (`nnos*`, `nnsocket*`, `nnssl*`, `nnResult*`) | 67 | nn::ec (shop) | 20 |
| nn::audio (AudioRenderer **+ AudioOut + AudioIn**) | 60 | nn::friends | 17 |
| nn::os | 39 | nn::codec (**hardware Opus** + multistream) | 12 |
| nn::fs (MountRom, SaveData, Temporary, Cache) | 29 | nn::oe | 12 |
| nn::swkbd (incl. **InlineKeyboard**) | 27 | nn::mem StandardAllocator | 10 |
| nn::socket | 24 | nn::ssl, nn::nifm, nn::time | 9 / 8 / 8 |
| nn::vi, nn::web, nn::err, nn::ngc, nn::pctl, nn::diag, nn::pl, nn::ro, nn::nsd, nn::crypto, nn::util | small | `nv::InitializeGraphics`, `nv::SetGraphicsAllocator`, `nvnBootstrapLoader` | 3 |

Notable: **`nn::pl::IsRunningOnOunce()`** (the Switch 2 check; the game has a Switch 2 code path), and
`nn::os::GetHostArgc/Argv`.

### Coverage by TotK's host (static name match) **[C]**

74 of the 765 imports appear by name in TotK's runtime override set (fs 21/29, audio 9/60, os 4/39, hid 6/22,
oe 7/12, codec 6/12, vi 4/4, account 5/22, swkbd 4/27, time 3/8). The other 691 run as lifted SDK code in a
TotK-style design, so a "miss" is not a blocker. But in that design, lifted SDK code that reaches IPC for a service
TotK doesn't implement gets nothing. TotK implements only `audren:u` and `sm:` as real IPC services. Astral Chain
adds `ov_audout`, `ov_hwopus`, `ov_nifm`, `ov_fsp`, `ov_hid`, `ov_settings`, `ov_prepo`, which map well onto MLB's
needs. Mapping: `work/analysis/main.imports.vs_totk.txt`.

### NPDM (`main.npdm`) **[C]**

- 64-bit, 39-bit address space, main thread priority 44, core 0, stack 1 MiB.
- Thread priorities 28–59, **cores 0–2** (three application cores, the same scheduler model TotK emulates).
- Handle table 512; debug allowed; program type 1.
- 50 permitted SVCs (0x01–0x36 range plus 0x5F FlushProcessDataCache). `sdk` actually contains 58 `svc` sites,
  using 0x01–0x36, 0x40/41/43/44/45/4B/50/51/52/70/72. Some of these, such as the session and port SVCs, are present
  but not permitted, so they are presumably dead paths **[I]**.
- 49 service permissions, including `acc:u0 aoc:u apm appletOE audin:u audout:u audren:u bcat:u bsd:u caps:u
  csrng fsp-srv friend:u hid hwopus irs ldn:u lm mm:u nfc:* nfp:user ngc:u nifm:u nim:eca notif:a nsd:u ntc nvdrv
  pctl pl:u prepo:u set sfdnsres ssl time:u vi:u`. The full list is in `work/analysis/npdm.json`.

## 5. Engine and middleware (from `main` strings)

- **Engine:** Sony San Diego's in-house multiplatform engine, descended from the PS3 era (`MLBPS3/SRC`, `PS3Head`),
  with a native NVN renderer (`Engine::NVN::EngineGraphicsContext`, `EngineBaseCommandContext`) **[C]**. Assets are
  shared with other platforms (`PS4/Animation/*.xbn`, `ps5/stadiums/...`, `$(PLATFORM)`, `$(WORLD_PLATFORM)`) **[C]**.
  It is **not** Nintendo's sead/NintendoWare stack that the reference games use, so no game-side code or patch
  table from the references carries over **[C]**.
- **Audio:** Sony "Scream"/TnT_Audio (`snd_*`, `SCREAM_BANK_*`, `snd_host_nx64.cpp`). Codecs: PCM, MP3, AT9, AAC,
  Opus (Opus through `nn::codec` hardware decode), plus voice chat via AudioIn **[C]**.
- **Video:** RAD **Bink / Bink 2** (`.bik`, `.bk2`, `SonyStudio_INTRO.BIK`, `BinkOpaqueHDR`, `SetTextureBink`).
  The decoder is statically linked in `main`, so it is recompiled like the rest. No host decoder is needed, unlike
  TotK's VP9 `movie::Decoder` **[I]**. Bink 2 may use GPU compute for decode on some platforms **[H]**.
- **Scripting:** Lua 5.3 **[C]**.
- **Other statically linked libraries:** OpenSSL, libcurl, libwebp, Opus, zlib, stb_image, EXR, Dear ImGui (debug) **[C]**.
- **Shaders:** precompiled NVN Maxwell binaries in `FXB` banks; see §5a **[C]**.

## 5a. RomFS (1.0.15, merged)

`work/romfs/romfs_list.tsv` lists every file, and `romfs_info.json` has the layer statistics.

- **84,634 files, 37.41 GB** (compressed-storage virtual size 37,414,560,944 bytes) **[C]**.
- Top level: `NSW/` 26.95 GiB, `PS4/` 5.10 GiB (shared animation `.xbn`), `CROSS/` 1.98 GiB (movies, live content),
  `PS5/` 0.42 GiB, `AND/`, `FLUX/`, `FRONTEND/`, `BOOT/CONFIG_MAIN.TXT`, `CHANGELIST.TXT` **[C]**.
- By type: audio `.skx` 10.7 GiB (13 Scream banks, e.g. `PXP.SKX` 3.9 GB of play-by-play), `.xvag` 1.42 GiB,
  ATRAC9 `.at9`, `.bnk`. Textures `.tex` 37,790 files / 8.31 GiB. Models `.mod` 25,562 files / 1.78 GiB. Video
  `.bik` 3.88 GiB + `.bk2` 1.85 GiB. UI `.webp` 6,627 files. `.json` / `.xml` data **[C]**.
- **Shaders (`NSW/SHADERS/BANK_STANDARD`, `BANK_DEFERRED`, 456 `.FXBNVN`, 162 MiB) [C]:** a Sony `FXB\0` container
  with technique names followed by GPU programs in the NVN layout: a 0x30 header (magic `0x12345678`), a 0x50
  Maxwell SPH, then SASS. There are 7,234 graphics stage blobs (3,617 vertex + 3,617 fragment; about 4,505 unique).
  The 10 `*COMPUTE.FXBNVN` banks (skinning, hair, PBR, indirect draw, VAT…) use a different blob layout with no SPH,
  still to be parsed. All shaders are on disk, so **the whole set can be translated ahead of time from the RomFS**,
  with no need to capture them at play time (TotK depended on MacNativeGames' captured list). `SHADERLISTNSW.TXT`
  gives the bank defines (`-DSHADER_COMPILER_NVN -DSN_TARGET_NSW -DUSE_NSW_NVN`). The studio's shader build
  scripts and **`NVNGLSLC.DLL`** (Nintendo's offline compiler, 25 MB) shipped in the RomFS by accident. We don't need
  them, since the binaries are already compiled, and as SDK files they must not be redistributed or used beyond
  local analysis.
- **Textures [C/H]:** `TX12NSW\0` container with a 0x420-byte header (name, hashes, sizes) and a compressed payload.
  Of 7,021 sampled files, 434 decode as raw LZ4 and 45 as raw deflate; the rest neither (probably chunked, or another
  codec). Pixel formats, including whether ASTC is used, are **not yet known [H]**. `main` contains a `tex_lz4` string.
- `BOOT/CONFIG_MAIN.TXT` is a developer config of command-line style switches (`#NoWad`, `#FileSys=ROM`,
  `#world=ps3/stadiums/sf`…), all commented out **[C]**.

## 6. Graphics

- **API:** NVN 19.3.11, loaded through `nvnBootstrapLoader` and per-name `GetProcAddress` **[C]**. `main` carries
  a 536-entry NVN proc name table. 534 of those names appear in TotK's 537-entry `nvn_procs.inc`. The other two are
  the bootstrap entry points **[C]**. That table is the full loader list, so it shows API-version compatibility, not
  which calls MLB actually makes **[I]**.
- **GPU target:** Maxwell (Tegra X1) shader ISA, as with every NX title. The Ryujinx Maxwell→SPIR-V translator
  (TotK/Astral) or Power Moon's C Maxwell→GLSL translator applies **[I]**.
- **Shader volume:** the RomFS holds about 4,505 unique graphics stage binaries plus the compute banks **[C]**. The
  user's Ryujinx cache (~68 h on 1.0.15) holds only about 384 guest stages / 240 programs (estimated from TOC sizes)
  **[H]**, so play uses a small subset. Next to TotK's 29,587 variants, ahead-of-time translation is cheap.
- **Dev machine Vulkan:** RTX 3060 Ti, driver 610.88, Vulkan **1.4.341**, `VK_EXT_external_memory_host` ✔,
  `VK_EXT_graphics_pipeline_library` ✔, transform feedback ✔, custom border color ✔, geometry/tessellation ✔,
  **`textureCompressionASTC_LDR` ✘** **[C]**. It meets TotK's back-end requirements. If MLB uses ASTC, the GPU ASTC
  decode path is required **[H]** (texture formats are unknown until RomFS analysis).
- Vulkan is the recommended back end for the same reasons TotK gives: the translator emits SPIR-V, and a working
  NVN→Vulkan replay already exists.

## 7. Filesystem, save, input, audio, threading, memory

- **FS:** `nn::fs::MountRom` (with `QueryMountRomCacheSize` and `EnableGlobalFileDataCache`), save data per user
  (`MountSaveData(name, Uid)`, `ExtendSaveData`, `GetSaveDataSize`, `Commit`), `MountTemporaryStorage`,
  `MountCacheStorage` (NACP cache size is 0, so this is probably unused), plus libc `fopen`/`fread` through nnmusl **[C]**.
- **Input:** Npad FullKey, JoyDual, JoyLeft, JoyRight and Handheld states, vibration, touch screen, controller
  support applet, inline software keyboard **[C]**.
- **Audio:** AudioRenderer (voices, final mix, device sink), AudioOut, AudioIn, hardware Opus (single and multistream) **[C]**.
- **Threading:** `nn::os` threads plus pthread and `std::thread` via nnmusl; named threads such as "Kinematics PPU
  Thread", "DataStream Process Thread", `snd_stream_parsing_thread`, "Save Data Thread" **[C]**. The three-core,
  priority-preemptive model is required (NPDM cores 0–2). Whether MLB depends on per-core lock-free data the way
  TotK does is unknown **[H]**.
- **Memory:** `SetMemoryHeapSize`, `AllocateMemoryBlock`, `nn::mem::StandardAllocator`, `nv::SetGraphicsAllocator`,
  `QueryMemoryInfo` **[C]**. The 39-bit address space fits TotK's guest==host layout below 2^39 **[C]**.
- **Online (out of scope):** socket, ssl, nifm, nsd, curl, ec (shop), friends, NSA/NA auth, web applet. In a
  port they should report "no network" (`nifm::IsNetworkAvailable() == false`, request rejected). Switch titles
  must support that state, so offline modes should follow their normal no-network path **[I]**.

## 8. What the reference projects offer

| Component | TotK Recomp | Astral Chain Recomp | Power Moon Recomp | Fit for MLB |
|---|---|---|---|---|
| Lineage | LAS Recomp lifter + MacNativeGames host/NVN | Fork of TotK | LAS Recomp lineage, own host | |
| Lifter (ARM64→C, NEON→C, jump tables, flags/regs caching) | Mature, parallel, Unicorn diff tests | Same as TotK | Smaller variant | **Reuse TotK's.** It lifts 100% of MLB's instructions (§3). |
| Module layout | main+subsdk+sdk, guest==host <2^39 | + fixed RomFS address | | Reuse. Simpler for MLB (no subsdk). |
| Kernel / threads (3 cores, preemption, spin gates) | Yes | Yes | Simpler | Reuse TotK's. The spin-wait list must be regenerated for MLB. |
| SDK boundary | nn::* overrides by symbol + `audren:u` IPC | + audout, hwopus, nifm, fsp, hid, settings, prepo | IPC-level services (acc, am, audio, fs, hid, set, time) + symbol HLE | Mix: TotK/Astral symbol HLE, checked against SDK 19.3.11 layouts. Power Moon's IPC-level design is the most SDK-version-robust option. |
| NVN front end + Vulkan 1.3 back end | Yes (GPL pipelines, VMA, ASTC GPU decode, host-memory import) | Same + BC1/BC3 re-encode | Own, smaller | Reuse TotK/Astral. Game-specific hacks (resolution, HDR, DLAA sites) do **not** carry over. |
| Shader translation | Ryujinx 1.3.2 translator (.NET 10 NativeAOT) | Same | Own C translator → GLSL → shaderc | Ryujinx path preferred (MLB runs in Ryujinx today). .NET 10 is installed. |
| Extraction | hactool + BKTR patches, MacNativeGames extractor | Same | Expects pre-extracted exefs/romfs | **Not usable** (no sparse/compressed storage). Replaced by `tools/romfs_dump.py` (§1). |
| Game-specific code (patch tables, sead/Bezel HLE, romfs zstd, restbl) | TotK only | AC only | SMO only | **Not reusable.** |

Licensing: all three are MIT (vendored Ryujinx parts are MIT). Copy with headers and a THIRD_PARTY_NOTICES entry,
following TotK's own rules.

## 9. Compatibility gaps and risks

1. **SDK 19.3.11 vs. the references' SDKs:** HLE overrides that touch SDK structs (`nn::os::ThreadType`,
   `MutexType`, audio renderer config, fs handles) must be validated against this SDK.
2. **Audio:** AudioOut + AudioIn + hwopus (not only audren). Scream's mixer behaviour on the AudioRenderer
   revision used by SDK 19.3.11 is unknown. The game decodes ATRAC9 and XVAG itself (in `main`).
3. **Inline software keyboard** (`InlineKeyboard`): it renders through the game (`GetImage`), so it needs a host
   implementation or stub (TotK has only a basic swkbd).
4. **Switch 2 path:** `IsRunningOnOunce` must return false so the game stays on the NX path **[I]**.
5. **Lifter:** every instruction lifts (§3). Correctness, function discovery and jump tables are still unchecked
   on MLB's code.
6. **Threading assumptions:** unknown until the game runs. Use TotK's core model first.
7. **Texture formats:** the TEX payload codec and pixel formats (ASTC or not) are unknown. The RTX 3060 Ti has no
   native ASTC.
8. **Compute shader banks:** a different blob layout, not parsed yet.

## 10. Recommended next steps

1. **Decide the repository structure:** `recomp/` (generic lifter, forked from TotK with attribution), `runtime/`
   split into a generic host (kernel, memory, NVN→Vulkan, IPC/HLE) and `game/mlb/` (MLB-specific HLE, patches,
   config), and `tools/` (extraction, analysis). Initialise git before any of that lands.
2. **Port the TotK runtime under the generated code** (loader with relocations and import binding, kernel/threads,
   memory layout, `rt_svc`, `rt_dispatch`). The lifted code already compiles and links (§3a). Then run TotK's
   Unicorn differential tests on the encodings MLB uses, and generate an MLB spin-wait list.
3. **Shader smoke test:** feed a sample of `FXBNVN` vertex/fragment blobs through the Ryujinx translator (TotK's
   `tools/build_xlate.py` path, .NET 10 is installed) to confirm they translate to SPIR-V.
4. **First milestone: boot to first NVN present.** Lift `main` + `sdk`, link with the host, stub online services,
   and implement fs (RomFS image from `romfs_dump.py` + save), os, vi and NVN bootstrap. Trace unhandled IPC/HLE
   calls to drive the work.
5. **Asset formats:** the TEX payload codec and pixel formats (via the engine's texture loader in `main`, or by
   matching the `tex_lz4` path), and the compute bank layout.
6. **Ryujinx as a correctness oracle:** compare service calls and logs once our build runs.

## Reproducing

Requires Python 3.13 with a venv (`py -m venv .venv` then `.venv/Scripts/python -m pip install lz4 capstone
pycryptodome numpy itanium_demangler`), and your own `prod.keys` / `title.keys`. Outputs go to `work/` (git-ignored).

```bash
.venv/Scripts/python -I tools/nxextract.py --keys PATH/prod.keys --titlekeys PATH/title.keys "gamefiles/<update>.nsp" work/update --exefs --control
.venv/Scripts/python -I tools/nxextract.py --keys PATH/prod.keys --titlekeys PATH/title.keys "gamefiles/<base>.nsp" work/base --control
.venv/Scripts/python -I tools/nsoinfo.py work/update/exefs work/analysis
.venv/Scripts/python -I tools/isa_census.py work/analysis/main.elfimg 1c21340 work/analysis/main.isa.json
.venv/Scripts/python -I tools/isa_census.py work/analysis/sdk.elfimg 64e230 work/analysis/sdk.isa.json --lifter-dir refs/totk/recomp
.venv/Scripts/python -I refs/totk/recomp/gen.py --exefs work/update/exefs --out work/gen --modules main,sdk
sh tools/linktest/build.sh
.venv/Scripts/python -I tools/romfs_dump.py --keys PATH/prod.keys --titlekeys PATH/title.keys "gamefiles/<base>.nsp" "gamefiles/<update>.nsp" work/romfs --verify 64 [--extract GLOB... | --extract-all]
```

Tools: `tools/swcrypto.py` (AES via Windows CNG, Nintendo XTS), `tools/nxextract.py` (PFS0/NCA/CNMT/NACP; reports
sparse, compressed and BKTR sections), `tools/nsoinfo.py` (NSO/MOD0/dynamic/symbols/SVC/sysregs/NPDM),
`tools/isa_census.py` (capstone census; optional lifter oracle), `tools/ncastorage.py` (sparse / AesCtr(Ex) /
indirect / compressed storage layers), `tools/romfs_dump.py` (patched RomFS listing, IVFC verification, extraction), `tools/linktest/` (compile and link the
generated C against a stub runtime).
Reference checkouts in `refs/` (ignored): TotK `ae30a61`, Astral Chain `e9b63a9`, Power Moon `cdbe05c`, hactool
`1d64a83`, Atmosphère (sparse checkout of `libstratosphere` fssystem, read for behaviour only, GPL).
