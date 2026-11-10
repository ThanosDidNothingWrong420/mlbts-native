# Third-party notices

Code copied or adapted from another project is listed here with its source, the exact commit, its license, and the
files it went into. Each copied file keeps its original copyright and attribution header. Those headers also name
the projects TotK Recomp itself took the code from (LAS Recomp, MacNativeGames, d2r-mobile, nxnative, musl). Their
rows are in TotK Recomp's own `THIRD_PARTY_NOTICES.md` at the commit below, and the licence text is in
`runtime/core/LICENSE.totk-recomp`.

| Component | Source and commit | License | Used in |
|---|---|---|---|
| TotK Recomp code generator: lifter, NEON/FP lowering, jump tables, flag and register caching, layout, NSO reader, patch tables | https://github.com/valentderah/totk-pc-recomp @ `ae30a61b7bee4a3db959a234e7459e95939ac596`, Copyright (c) 2026 Valiantsin Dzerakh (and the upstreams named in each file header) | MIT | `recomp/*.py`, `tools/hostmem.py` |
| TotK Recomp host runtime core: CPU and SIMD headers, dispatch, host-function table, NSO loader, guest memory, kernel and threads, SVCs, IPC, SHA-256, platform layer, FMA fallback, frame timing and sampling profiler | same commit | MIT | `runtime/core/`, `tools/prof_report.py` |
| Atmosphère `libstratosphere` fssystem (NCA storage layering, bucket tree, sparse / AesCtrEx / indirect / compressed storage). **Read for behaviour only; no code copied** | https://github.com/Atmosphere-NX/Atmosphere | GPL-2.0 | `tools/ncastorage.py` (independent implementation) |
| TotK Recomp `fs_host.c` (nn::fs on host directories, journaled save commit) | same TotK commit | MIT | `runtime/core/fs_host.c` (changes marked `mlbts-native`) |
| TotK Recomp graphics: NVN front end (MacNativeGames lineage), Vulkan back end (LAS Recomp lineage), present and Win32 window, shader client, texture deswizzle and ASTC, and its committed SPIR-V headers | same TotK commit | MIT | `runtime/gfx/` (vendored unmodified in its own commit; changes marked `mlbts-native`) |
| TotK Recomp audio: `audren:u` renderer (MacNativeGames lineage, Ryujinx.Audio voice behaviour), mixer and IAudioDevice, delay/reverb/compressor and 3D reverb DSP (Ryujinx.Audio algorithms) | same TotK commit | MIT | `runtime/core/audren.c`, `audio_mixer.c` (SDL3 output replaced by WASAPI, marked `mlbts-native`), `audio_effects.c/.h`, `audio_reverb.c/.h` |
| Astral Chain Recomp `audout:u` service | https://github.com/captainj-sparrow/astral-chain-pc-recomp @ `e9b63a94f200e6ca81c9e629c42e1433862e6116`, Copyright (c) 2026 Valiantsin Dzerakh | MIT | `runtime/core/audout.c` (changes marked `mlbts-native`; licence text in `runtime/core/LICENSE.astral-recomp`) |
| TotK Recomp shader translator project: `Program.cs`, csproj, patches 0001-0009, 0012, 0013, 0015, 0016 (0001-0004 MacNativeGames') | same TotK commit | MIT | `tools/xlate/`, built by `tools/build_xlate.py` (adapted from TotK's) |
| Ryujinx 1.3.2 shader translator and common library, the subset TotK vendors | https://github.com/Ryujinx/Ryujinx, via TotK Recomp `tools/vendor/ryujinx` at the same commit, Copyright (c) Ryujinx Team and Contributors | MIT | `tools/vendor/ryujinx/` (byte-identical) |
| RenderDoc in-application API header `renderdoc_app.h` (RenderDoc v1.43) | https://github.com/baldurk/renderdoc, Copyright (c) 2015-2026 Baldur Karlsson | MIT | `runtime/gfx/renderdoc_app.h` (unmodified, licence in its header), used by `runtime/gfx/rdoc.c` |
| Vulkan-Headers 1.4.363 | https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.363 | Apache-2.0 OR MIT | fetched by `tools/fetch_deps.py` into `work/deps/` (not committed) |
| Vulkan Memory Allocator 3.4.0, header | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tree/v3.4.0 | MIT | fetched by `tools/fetch_deps.py` into `work/deps/`; compiled through `runtime/gfx/vk_vma.cpp` |
