"""Fetch the third-party build inputs of the graphics back end into work/deps (ignored by git).

    python -I tools/fetch_deps.py [--out work/deps]

- Vulkan-Headers v1.4.363 (Apache-2.0 OR MIT): include/vulkan, include/vk_video -> <out>/include/
- Vulkan Memory Allocator v3.4.0 (MIT): vk_mem_alloc.h -> <out>/include/vma/
- vulkan-1.lib: an import library for the Vulkan loader the GPU driver installed (C:\\Windows\\System32\\vulkan-1.dll),
  made from the DLL's export table with llvm-readobj and llvm-dlltool. No loader is built or shipped.

The same versions as TotK Recomp (THIRD_PARTY_NOTICES.md there). Downloads are checked against the SHA-256 recorded
below on the first successful fetch: a mismatch stops.
"""
import argparse
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LLVM = Path(r'C:\Program Files\LLVM\bin')
HEADERS_URL = 'https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.363'
VMA_URL = ('https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.4.0/include/'
           'vk_mem_alloc.h')
PINS = ROOT / 'tools' / 'data' / 'deps.sha256.json'   # url -> sha256, written by the first fetch


def fetch(url):
    data = urllib.request.urlopen(url, timeout=120).read()
    digest = hashlib.sha256(data).hexdigest()
    pins = json.loads(PINS.read_text()) if PINS.exists() else {}
    if url in pins and pins[url] != digest:
        raise SystemExit(f'{url}: sha256 {digest}, pinned {pins[url]}')
    if url not in pins:
        pins[url] = digest
        PINS.write_text(json.dumps(pins, indent=1, sort_keys=True) + '\n')
        print(f'pinned {url} {digest}')
    return data


def headers(out):
    data = fetch(HEADERS_URL)
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tf:
        for m in tf.getmembers():
            parts = Path(m.name).parts
            if len(parts) < 3 or parts[1] != 'include' or not m.isfile():
                continue
            if parts[2] not in ('vulkan', 'vk_video'):
                continue
            dst = out / 'include' / Path(*parts[2:])
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(tf.extractfile(m).read())
        lic = [m for m in tf.getmembers() if Path(m.name).name in ('LICENSE.md', 'LICENSE.txt')]
        if lic:
            (out / 'licenses').mkdir(parents=True, exist_ok=True)
            (out / 'licenses' / 'Vulkan-Headers.txt').write_bytes(tf.extractfile(lic[0]).read())
    print('Vulkan-Headers 1.4.363 ->', out / 'include')


def vma(out):
    dst = out / 'include' / 'vma' / 'vk_mem_alloc.h'
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(fetch(VMA_URL))
    print('VMA 3.4.0 ->', dst)


def import_lib(out):
    dll = Path(r'C:\Windows\System32\vulkan-1.dll')
    text = subprocess.run([str(LLVM / 'llvm-readobj.exe'), '--coff-exports', str(dll)], check=True,
                          capture_output=True, text=True).stdout
    names = sorted(set(re.findall(r'Name: (vk\w+)', text)))
    if 'vkCreateInstance' not in names:
        raise SystemExit(f'{dll}: no vkCreateInstance export')
    lib = out / 'lib'
    lib.mkdir(parents=True, exist_ok=True)
    (lib / 'vulkan-1.def').write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + ''.join(f'  {n}\n' for n in names))
    subprocess.run([str(LLVM / 'llvm-dlltool.exe'), '-m', 'i386:x86-64', '-d', str(lib / 'vulkan-1.def'),
                    '-l', str(lib / 'vulkan-1.lib')], check=True)
    print(f'vulkan-1.lib: {len(names)} exports of {dll}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=ROOT / 'work' / 'deps')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    headers(a.out)
    vma(a.out)
    import_lib(a.out)


if __name__ == '__main__':
    sys.exit(main())
