"""Build the shader translator: work/xlate/totk_xlate.exe (Ryujinx 1.3.2 shader translator + patches, .NET NativeAOT,
SPIR-V for Vulkan).

Adapted from TotK Recomp tools/build_xlate.py @ ae30a61 (MIT). The translator sources are TotK's, vendored unchanged:
tools/vendor/ryujinx (Ryujinx 1.3.2 subset, MIT; byte-identical to TotK's tools/vendor/ryujinx), tools/xlate
(Program.cs, the csproj, 13 of TotK's 15 patches; MIT). This script copies the vendor tree to work/xlate/ryujinx, applies the
patches there with `git apply`, and runs `dotnet publish` with NativeAOT. Needs the .NET 10 SDK and the MSVC linker
(Visual Studio 2022 with the C++ workload, found through vswhere.exe).

Next to the exe it writes totk_xlate.id, a sha256 over everything the build reads; runtime/core/gfx_shader.c puts it
into every shader cache key.

    python -I tools/build_xlate.py [--id-only]
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "tools" / "vendor" / "ryujinx"
TOTK_VENDOR = ROOT / "refs" / "totk" / "tools" / "vendor" / "ryujinx"
XLATE = ROOT / "tools" / "xlate"
BUILD = ROOT / "work" / "xlate"
EXE = "totk_xlate.exe"
VSWHERE_DIR = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer"
PATCHES = 14   # TotK's 15 less 0010 (PCSS sun shadows) and 0011 (shadow filter spread): TotK's look, not Maxwell's;
               # plus mlbts-native 0017 (memory loads stay in place)


def tree_hashes(base: Path) -> dict:
    def build_output(rel: Path) -> bool:
        return len(rel.parts) > 2 and rel.parts[0] == "src" and rel.parts[2] in ("obj", "bin")

    return {p.relative_to(base).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(base.rglob("*")) if p.is_file() and not build_output(p.relative_to(base))}


def verify_vendor() -> None:
    """tools/vendor/ryujinx must equal TotK's copy byte for byte (when refs/totk is present)."""
    if not TOTK_VENDOR.is_dir():
        print("refs/totk missing: vendor comparison skipped")
        return
    ours, theirs = tree_hashes(VENDOR), tree_hashes(TOTK_VENDOR)
    if ours != theirs:
        diff = sorted(k for k in ours.keys() | theirs.keys() if ours.get(k) != theirs.get(k))
        sys.exit(f"tools/vendor/ryujinx differs from TotK's in {len(diff)} files, first: {diff[:5]}")
    print(f"vendor identical to TotK's: {len(ours)} files")


def prepare_sources() -> Path:
    src = BUILD / "ryujinx"
    shutil.rmtree(src, ignore_errors=True)
    shutil.copytree(VENDOR, src)
    for junk in list(src.glob("src/*/obj")) + list(src.glob("src/*/bin")):
        shutil.rmtree(junk)
    patches = sorted((XLATE / "patches").glob("*.patch"))
    if len(patches) != PATCHES:
        sys.exit(f"expected {PATCHES} patches, found {len(patches)}")
    for patch in patches:
        for extra in (["--check"], []):   # --check first, so a failure names the patch
            r = subprocess.run(["git", "apply", *extra, "--whitespace=nowarn", "--directory", "ryujinx", str(patch)],
                               cwd=BUILD, capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit(f"patch does not apply: {patch.name}\n{r.stdout}{r.stderr}")
        print(f"applied {patch.name}")
    return src


def identity() -> str:
    h = hashlib.sha256()
    files = [Path(__file__).resolve(), XLATE / "Program.cs", XLATE / "totk_xlate.csproj",
             *sorted((XLATE / "patches").glob("*.patch"))]
    for f in files:
        h.update(f"{f.name}\0{hashlib.sha256(f.read_bytes()).hexdigest()}\n".encode())
    for rel, digest in tree_hashes(VENDOR).items():
        h.update(f"vendor/{rel}\0{digest}\n".encode())
    return h.hexdigest()


def write_identity(exe: Path) -> None:
    ident = identity()
    exe.with_suffix(".id").write_text(ident + "\n", encoding="ascii")
    print(f"translator identity {ident[:16]} -> {exe.with_suffix('.id')}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--id-only", action="store_true", help="write totk_xlate.id for the exe already built")
    a = ap.parse_args()
    if a.id_only:
        write_identity(BUILD / EXE)
        return
    started = time.time()
    dotnet = shutil.which("dotnet")
    if not dotnet:
        sys.exit("missing: dotnet (.NET 10 SDK)")
    if not (VSWHERE_DIR / "vswhere.exe").is_file():
        sys.exit(f"missing: vswhere.exe in {VSWHERE_DIR}; NativeAOT cannot link without it")
    env = dict(os.environ)
    env["PATH"] = str(VSWHERE_DIR) + os.pathsep + env["PATH"]
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    BUILD.mkdir(parents=True, exist_ok=True)
    verify_vendor()
    src = prepare_sources()
    work = BUILD / "xlate"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    for name in ("Program.cs", "totk_xlate.csproj"):
        shutil.copy2(XLATE / name, work / name)
    out = BUILD / "publish"
    shutil.rmtree(out, ignore_errors=True)
    cmd = [dotnet, "publish", str(work / "totk_xlate.csproj"), "-c", "Release", "-r", "win-x64", "-p:PublishAot=true",
           "-p:InvariantGlobalization=true", f"-p:RyuSrc={src}", "-o", str(out)]
    if subprocess.run(cmd, env=env).returncode != 0:
        sys.exit("dotnet publish failed")
    exe = BUILD / EXE
    shutil.copy2(out / EXE, exe)
    write_identity(exe)
    print(f"built {exe} ({exe.stat().st_size / 1e6:.1f} MB) in {time.time() - started:.0f} s")


if __name__ == "__main__":
    main()
