"""Build mlb.exe: the generated C (recomp/gen.py output) plus the host runtime, with LLVM clang + lld through Ninja.

    python -I tools/build.py [--gen work/gen] [--out work/build] [--clang PATH] [--jobs N] [--version 1.0.15]

Inputs: --gen holds layout.json/layout.h/patches.h and one directory per module (main, sdk) from recomp/gen.py.
Writes --out/modules.c (layout table, linked modules, expected ExeFS hashes from tools/data/expected-<version>.json,
default paths, version), --out/build.ninja, objects under --out/obj, and --out/mlb.exe. Everything under --out is
derived from game code and stays out of git (work/ is ignored).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from check_content import GENERATED_MARKER  # noqa: E402

DEFAULT_CLANG = r'C:\Program Files\LLVM\bin\clang.exe'
# Generated code: TotK Recomp tools/build_game.py GEN_FLAGS @ ae30a61 (LAS Recomp's flags plus -march).
GEN_FLAGS = ['-O1', '-fno-strict-aliasing', '-Wno-return-type', '-Wno-unused-label', '-fvisibility=hidden',
             '-march=x86-64-v3']
RT_FLAGS = ['-O2', '-g', '-Wall', '-Wno-unused-function', '-fno-strict-aliasing', '-march=x86-64-v3', '-std=gnu11',
            '-D_CRT_SECURE_NO_WARNINGS']
LIBS = ['bcrypt', 'user32', 'kernel32', 'advapi32', 'ntdll', 'synchronization', 'winmm', 'psapi', 'shell32', 'ole32',
        'dbghelp', 'gdi32', 'dwmapi', 'imm32', 'uuid']
RUNTIME_SOURCES = ['runtime/core/*.c', 'runtime/core/platform/common.c', 'runtime/core/platform/win32.c',
                   'runtime/core/platform/audio_win32.c', 'runtime/core/platform/prof_win.c',
                   'runtime/mlb/*.c', 'runtime/gfx/*.c', 'runtime/gfx/*.cpp',
                   # the in-game settings menu (F1) and Dear ImGui, which draws it
                   'runtime/gfx/ui/*.cpp', 'runtime/third_party/imgui/*.cpp',
                   'runtime/third_party/imgui/backends/*.cpp']
DEPS = ROOT / 'work' / 'deps'   # tools/fetch_deps.py: Vulkan headers, VMA, vulkan-1.lib
CXX_FLAGS = ['-O2', '-g', '-std=c++17', '-march=x86-64-v3', '-D_CRT_SECURE_NO_WARNINGS', '-Wno-nullability-completeness']


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def module_glue(layout, linked, hashes, paths, version):
    """modules.c: the layout table, the linked modules' function tables (dispatch.h), expected ExeFS hashes, the
    default game paths and the version strings (host.h). After TotK Recomp tools/build_game.py module_glue."""
    out = [f'// Module glue written by tools/build.py; do not edit. {GENERATED_MARKER}',
           '#include "dispatch.h"', '#include "host.h"', '#include "layout.h"', '']
    for m in layout:
        u = m['name'].upper()
        out.append(f'_Static_assert(MOD_{u}_BASE == {m["base"]}ull && MOD_{u}_SIZE == {m["size"]}ull, '
                   f'"layout.h and layout.json disagree on {m["name"]}");')
    out += ['', 'const RtLayoutEntry rt_layout[] = {']
    out += [f'    {{"{m["name"]}", MOD_{m["name"].upper()}_BASE, MOD_{m["name"].upper()}_SIZE}},' for m in layout]
    out += ['};', f'const int rt_layout_count = {len(layout)};', '']
    for name in linked:
        out += [f'extern const uint64_t {name}_fn_addrs[];', f'extern const GuestFn {name}_fn_ptrs[];',
                f'extern const uint64_t {name}_fn_count;']
    out += ['', 'int rt_linked_modules(RtModule *out, int max) {', '    int n = 0;']
    for name in linked:
        u = name.upper()
        out.append(f'    if (n < max) out[n++] = (RtModule){{"{name}", MOD_{u}_BASE, MOD_{u}_SIZE, {name}_fn_addrs, '
                   f'{name}_fn_ptrs, {name}_fn_count}};')
    out += ['    return n;', '}', '', 'const RtFileHash rt_exefs_sha256[] = {']
    out += [f'    {{{c_string(f)}, {c_string(h)}}},' for f, h in sorted(hashes.items())]
    out += ['};', f'const int rt_exefs_sha256_count = {len(hashes)};', '']
    for name in ('exefs', 'romfs', 'profile'):
        out.append(f'const char rt_default_{name}[] = {c_string(paths[name])};')
    out += ['// 0: not a TotK build. Only TotK self-test probes (loader.c, ipc.c) read it.',
            'const int rt_game_version = 0;', f'const char rt_port_version[] = {c_string(version)};', '']
    return '\n'.join(out)


def compiler_rt(clang):
    """clang_rt.builtins for the MSVC target (128-bit division, __builtin_cpu_supports), which clang does not link by
    itself there."""
    libs = sorted(Path(clang).resolve().parent.parent.glob('lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib'))
    if not libs:
        raise SystemExit(f'clang_rt.builtins-x86_64.lib not found next to {clang}')
    return libs[-1].as_posix()


def rel(p):
    """Path relative to the repository root (ninja runs there), so no flag carries the profile path's spaces."""
    p = Path(p).resolve()
    return Path(os.path.relpath(p, ROOT)).as_posix()


def ninja_path(p):
    return rel(p).replace(':', '$:').replace(' ', '$ ')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', type=Path, default=ROOT / 'work' / 'gen')
    ap.add_argument('--out', type=Path, default=ROOT / 'work' / 'build')
    ap.add_argument('--clang', default=os.environ.get('CLANG', DEFAULT_CLANG))
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--version', default='1.0.15')
    a = ap.parse_args()
    gen, out = a.gen.resolve(), a.out.resolve()
    layout = json.loads((gen / 'layout.json').read_text())
    linked = [m['name'] for m in layout if (gen / m['name'] / f'{m["name"]}_table.c').exists()]
    if not linked:
        raise SystemExit(f'no generated modules under {gen}: run recomp/gen.py first')
    expected = json.loads((ROOT / 'tools' / 'data' / f'expected-{a.version}.json').read_text())
    port_version = (ROOT / 'VERSION').read_text().strip()
    paths = {'exefs': (ROOT / 'work' / 'update' / 'exefs').as_posix(), 'romfs': (ROOT / 'work' / 'romfs' / 'romfs').as_posix(),
             'profile': (ROOT / 'work' / 'profile').as_posix()}
    out.mkdir(parents=True, exist_ok=True)
    glue = out / 'modules.c'
    text = module_glue(layout, linked, expected['exefs_sha256'], paths, port_version)
    if not glue.exists() or glue.read_text() != text:
        glue.write_text(text)

    rt_srcs = sorted({p for pat in RUNTIME_SOURCES for p in ROOT.glob(pat)}) + [glue]
    gen_srcs = sorted(p for name in linked for p in (gen / name).glob('*.c'))
    if not (DEPS / 'lib' / 'vulkan-1.lib').is_file():
        raise SystemExit(f'{DEPS} is missing the Vulkan headers and import library: run tools/fetch_deps.py')
    if not (DEPS / 'include' / 'SDL3' / 'SDL.h').is_file() or not (DEPS / 'bin' / 'SDL3.dll').is_file():
        raise SystemExit(f'{DEPS} is missing SDL3 (controllers): run tools/fetch_deps.py')
    # runtime/mlb/hid.c loads SDL3.dll from mlb.exe's folder at run time. A running game holds it: keep the old copy then.
    sdl_src, sdl_dst = DEPS / 'bin' / 'SDL3.dll', out / 'SDL3.dll'
    if not sdl_dst.is_file() or sdl_dst.read_bytes() != sdl_src.read_bytes():
        try:
            shutil.copyfile(sdl_src, sdl_dst)
        except OSError as e:
            print(f'warning: {sdl_dst} not updated ({e})')
    # runtime/mlb/prompts.c: the Xbox and PlayStation button icons, drawn and compressed by tools/make_prompts.py (about
    # half a minute), again only when that script changed
    icons, icon_tool = gen / 'prompt_icons.h', ROOT / 'tools' / 'make_prompts.py'
    if not icons.is_file() or icons.stat().st_mtime < icon_tool.stat().st_mtime:
        if subprocess.run([sys.executable, '-I', str(icon_tool), '--out', str(icons)], cwd=ROOT).returncode:
            raise SystemExit('tools/make_prompts.py failed')
    inc_rt = ['-I', 'runtime/mlb', '-I', 'runtime/core', '-I', 'runtime/gfx', '-I', rel(gen), '-I', rel(DEPS / 'include'),
              '-I', rel(DEPS / 'include' / 'vma'), '-I', 'runtime/third_party/imgui']
    inc_gen = ['-I', 'runtime/core', '-I', rel(gen)]
    for d in (rel(gen), rel(out)):
        if ' ' in d:
            raise SystemExit(f'{d}: keep --gen and --out inside the repository, without spaces')
    exe = out / 'mlb.exe'
    lines = [f'# {GENERATED_MARKER}: written by tools/build.py', f'builddir = {rel(out)}', f'cc = "{Path(a.clang).as_posix()}"',
             f'genflags = {" ".join(GEN_FLAGS)} {" ".join(inc_gen)}',
             f'rtflags = {" ".join(RT_FLAGS)} {" ".join(inc_rt)}',
             'rule cc_gen', '  command = $cc $genflags -c $in -o $out', '  description = CC $in',
             'rule cc_rt', '  command = $cc $rtflags -MD -MF $out.d -c $in -o $out', '  depfile = $out.d',
             '  deps = gcc', '  description = CC $in',
             f'cxxflags = {" ".join(CXX_FLAGS)} {" ".join(inc_rt)}',
             'rule cxx_rt', '  command = $cc -x c++ $cxxflags -MD -MF $out.d -c $in -o $out', '  depfile = $out.d',
             '  deps = gcc', '  description = CXX $in',
             'rule link', f'  command = $cc -fuse-ld=lld -g @$out.rsp -o $out {" ".join("-l" + l for l in LIBS)} '
                          f'"{compiler_rt(a.clang)}" {rel(DEPS / "lib" / "vulkan-1.lib")} '
                          f'-Wl,/STACK:8388608 -Wl,/errorlimit:0 -Wl,/MAP:{rel(out)}/mlb.map', '  rspfile = $out.rsp', '  rspfile_content = $in',
             '  description = LINK $out', '']
    objs = []
    for s in rt_srcs:
        sub = s.relative_to(ROOT) if s.is_relative_to(ROOT) else Path(s.name)
        o = out / "obj" / "rt" / sub.with_suffix(".o")
        lines.append(f'build {ninja_path(o)}: {"cxx_rt" if s.suffix == ".cpp" else "cc_rt"} {ninja_path(s)}')
        objs.append(o)
    for s in gen_srcs:
        o = out / 'obj' / 'gen' / s.parent.name / (s.stem + '.o')
        lines.append(f'build {ninja_path(o)}: cc_gen {ninja_path(s)}')
        objs.append(o)
    lines.append(f'build {ninja_path(exe)}: link {" ".join(ninja_path(o) for o in objs)}')
    lines.append(f'default {ninja_path(exe)}')
    nf = out / 'build.ninja'
    nf.write_text('\n'.join(lines) + '\n')
    ninja = shutil.which('ninja') or str(Path(sys.executable).parent / 'ninja.exe')
    cmd = [ninja, '-f', rel(nf), '-k', '0'] + (['-j', str(a.jobs)] if a.jobs else [])   # every failure, not the first
    sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)


if __name__ == '__main__':
    main()
