"""Map addresses of one build of `main` to another (1.0.15 -> 1.0.20): the same code or data in the other build.

    python -I tools/addrmap.py OLD_EXEFS NEW_EXEFS ADDR [ADDR ...]
    python -I tools/addrmap.py work/update-1.0.15/exefs work/update/exefs 0x7001ad7bd0 main+0x5a5368

Addresses are guest addresses (0x70xxxxxxxx, main at 0x7000000000) or main+<offset>. Code is matched on instruction
words around the address with the fields that move between builds masked (branch, ADR/ADRP and literal-load
offsets, ADD/LDR/STR immediates that follow an ADRP page), widening the window until one place matches. Data is
matched on its bytes the same way. Comments in runtime/ name 1.0.15 addresses unless they say otherwise; this finds
their 1.0.20 places. Output stays local: it is derived from game code.
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nsoinfo import Nso  # noqa: E402

BASE = 0x7000000000


def masks(w):
    """Per-word masks keeping the bits that do not move between builds."""
    m = np.full(w.shape, 0xffffffff, dtype=np.uint32)
    m[(w & 0x7c000000) == 0x14000000] = 0xfc000000            # B, BL
    m[(w & 0xff000010) == 0x54000000] = 0xff00001f            # B.cond
    m[(w & 0x7e000000) == 0x34000000] = 0xff00001f            # CBZ, CBNZ
    m[(w & 0x7e000000) == 0x36000000] = 0xfff8001f            # TBZ, TBNZ
    m[(w & 0x1f000000) == 0x10000000] = 0x9f00001f            # ADR, ADRP
    m[(w & 0x3b000000) == 0x18000000] = 0xff00001f            # LDR (literal)
    # the low 12 bits of an address (ADD / load / store immediate after an ADRP) move with the data
    m[(w & 0x7f800000) == 0x11000000] = 0xffc003ff            # ADD (immediate), 64 and 32 bit
    m[(w & 0x3b000000) == 0x39000000] = 0xffc003ff            # LDR/STR (unsigned immediate)
    return m


def find(old, new, off, seg_old, seg_new, code):
    o0, osz = seg_old
    n0, nsz = seg_new
    if code:
        ow = np.frombuffer(bytes(old.image[o0:o0 + osz]), dtype='<u4')
        nw = np.frombuffer(bytes(new.image[n0:n0 + nsz]), dtype='<u4')
        om, nm = ow & masks(ow), nw & masks(nw)
        i = (off - o0) // 4
        for before, after in ((2, 6), (4, 12), (8, 24), (16, 48), (32, 96)):
            lo, hi = max(0, i - before), min(len(om), i + after)
            pat = om[lo:hi]
            cand = np.nonzero(nm[:len(nm) - len(pat) + 1] == pat[0])[0]
            for k in range(1, len(pat)):
                if len(cand) <= 1:
                    break
                cand = cand[nm[cand + k] == pat[k]]
            cand = [c for c in cand if np.array_equal(nm[c:c + len(pat)], pat)]
            if len(cand) == 1:
                return n0 + (cand[0] + i - lo) * 4 + (off - o0) % 4, hi - lo
            if not cand:
                return None, hi - lo
        return None, -1
    ob, nb = bytes(old.image[o0:o0 + osz]), bytes(new.image[n0:n0 + nsz])
    i = off - o0
    for before, after in ((0, 16), (16, 48), (64, 128), (256, 512)):
        lo, hi = max(0, i - before), min(len(ob), i + after)
        pat = ob[lo:hi]
        a = nb.find(pat)
        if a < 0:
            return None, hi - lo
        if nb.find(pat, a + 1) < 0:
            return n0 + a + i - lo, hi - lo
    return None, -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old', type=Path, help='the ExeFS folder (or main NSO) the addresses come from')
    ap.add_argument('new', type=Path, help='the ExeFS folder (or main NSO) to find them in')
    ap.add_argument('addr', nargs='+')
    a = ap.parse_args()
    old, new = (Nso(p / 'main' if p.is_dir() else p) for p in (a.old, a.new))
    for s in a.addr:
        v = int(s[5:], 16) if s.startswith('main+') else int(s, 16)
        off = v - BASE if v >= BASE else v
        seg = next((k for k, (m, size, _) in old.segments.items() if m <= off < m + size), None)
        if seg is None:
            print(f'{s}: not in main\'s text, rodata or data')
            continue
        so, sn = old.segments[seg][:2], new.segments[seg][:2]
        hit, width = find(old, new, off, so, sn, seg == 'text')
        if hit is None:
            why = 'none: it changed' + ('' if seg == 'text' else ', or it holds pointers, which move') if width >= 0 \
                else 'several'
            print(f'{s}: {seg}, no single match ({why})')
        else:
            print(f'{s}: {seg} {BASE + off:#x} -> {BASE + hit:#x} (main+{hit:#x}, {hit - off:+#x}; '
                  f'{width} {"words" if seg == "text" else "bytes"} matched)')


if __name__ == '__main__':
    main()
