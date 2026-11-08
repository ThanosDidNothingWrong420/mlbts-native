#!/bin/sh
# Compile the TotK-generated C for MLB (work/gen) with LLVM clang and link it against a stub runtime.
# Smoke test only: proves the lifted code compiles and links; it does not run game code.
#   sh tools/linktest/build.sh        (from the repository root, after recomp/gen.py wrote work/gen)
set -e
CL="${CLANG:-/c/Program Files/LLVM/bin/clang.exe}"
RT="${TOTK_RUNTIME:-refs/totk/runtime}"
FL="-O1 -fno-strict-aliasing -Wno-return-type -Wno-unused-label -fvisibility=hidden -march=x86-64-v3 -I $RT -I work/gen"
mkdir -p work/obj
ls work/gen/main/*.c work/gen/sdk/*.c | xargs -P "${JOBS:-10}" -I{} sh -c '
  f="{}"; o="work/obj/$(basename "$f" .c).o"
  "'"$CL"'" '"$FL"' -c "$f" -o "$o" || echo "FAIL $f"'
"$CL" -O1 -march=x86-64-v3 -I "$RT" -I work/gen -c tools/linktest/stub_rt.c -o work/obj/stub_rt.obj
ls work/obj/*.o > work/obj/objs.rsp
"$CL" -fuse-ld=lld work/obj/stub_rt.obj @work/obj/objs.rsp -o work/obj/mlb_linktest.exe -Wl,/STACK:8388608
work/obj/mlb_linktest.exe
