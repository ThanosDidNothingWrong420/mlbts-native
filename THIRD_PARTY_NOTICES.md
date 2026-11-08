# Third-party notices

Code copied or adapted from another project is listed here with its source, the exact commit, its license, and the
files it went into. Each copied file keeps its original copyright and attribution header. Those headers also name
the projects TotK Recomp itself took the code from (LAS Recomp, MacNativeGames, d2r-mobile, nxnative, musl). Their
rows are in TotK Recomp's own `THIRD_PARTY_NOTICES.md` at the commit below, and the licence text is in
`runtime/core/LICENSE.totk-recomp`.

| Component | Source and commit | License | Used in |
|---|---|---|---|
| TotK Recomp code generator: lifter, NEON/FP lowering, jump tables, flag and register caching, layout, NSO reader, patch tables | https://github.com/valentderah/totk-pc-recomp @ `ae30a61b7bee4a3db959a234e7459e95939ac596`, Copyright (c) 2026 Valiantsin Dzerakh (and the upstreams named in each file header) | MIT | `recomp/*.py`, `tools/hostmem.py` |
| TotK Recomp host runtime core: CPU and SIMD headers, dispatch, host-function table, NSO loader, guest memory, kernel and threads, SVCs, IPC, SHA-256, platform layer, FMA fallback | same commit | MIT | `runtime/core/` |
| Atmosphère `libstratosphere` fssystem (NCA storage layering, bucket tree, sparse / AesCtrEx / indirect / compressed storage). **Read for behaviour only; no code copied** | https://github.com/Atmosphere-NX/Atmosphere | GPL-2.0 | `tools/ncastorage.py` (independent implementation) |
