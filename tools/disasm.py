"""Disassemble part of an NSO module, with its dynamic symbols as labels and as call-target names.

    python -I tools/disasm.py work/update/exefs/sdk 0x13e700 [0x140]
    python -I tools/disasm.py work/update/exefs/sdk _ZN2nn2os27RegisterSystemWorkerHandlerEPNS0_19MultiWaitHolderTypeEPFvvE

Offsets are module-relative (sdk+0x13e700 in runtime logs); a symbol name starts at that symbol and defaults to its
size. Output stays local: it is derived from game code.
"""
import argparse
import struct
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nsoinfo import Nso  # noqa: E402


def plt_targets(nso):
    """PLT stub address -> symbol: JUMP_SLOT relocations name the GOT slots, and each 16-byte stub is
    adrp x16, slot page; ldr x17, [x16, slot offset]; add; br x17."""
    tags = nso.dyn
    if 23 not in tags:
        return {}
    slots = {}
    for i in range(tags.get(2, 0) // 24):
        off, info, _ = struct.unpack_from('<QQq', nso.image, tags[23] + i * 24)
        if info >> 32:
            slots[off] = nso.syms[info >> 32]['name']
    text_off, text_size, _ = nso.segments['text']
    out = {}
    img = bytes(nso.image)
    # the PLT is one run of stubs; scan the end of .text where it lives
    lo = max(text_off, text_off + text_size - 16 * (len(slots) + 64))
    for a in range(lo & ~15, text_off + text_size - 12, 4):
        w0, w1, _, w3 = struct.unpack_from('<IIII', img, a)
        if (w0 & 0x9F00001F) != 0x90000010 or (w1 & 0xFFC003FF) != 0xF9400211 or w3 != 0xD61F0220:
            continue
        immlo, immhi = (w0 >> 29) & 3, (w0 >> 5) & 0x7FFFF
        page = ((immhi << 2 | immlo) << 12)
        if page & (1 << 32):
            page -= 1 << 33
        slot = (a & ~0xFFF) + page + ((w1 >> 10) & 0xFFF) * 8
        if slot in slots:
            out[a] = slots[slot]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('nso')
    ap.add_argument('start')
    ap.add_argument('length', nargs='?', type=lambda v: int(v, 0))
    a = ap.parse_args()
    nso = Nso(a.nso)
    nso.parse_dynamic()
    by_addr = {}
    by_name = {}
    for s in nso.syms:
        if s['shndx'] and s['name'] and s['value']:
            by_addr.setdefault(s['value'], s['name'])
            by_name[s['name']] = s
    plt = plt_targets(nso)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    if a.start in by_name:
        start, length = by_name[a.start]['value'], a.length or by_name[a.start]['size'] or 0x100
    else:
        start, length = int(a.start, 0), a.length or 0x100
    for ins in md.disasm(bytes(nso.image[start:start + length]), start):
        if ins.address in by_addr:
            print(f'{by_addr[ins.address]}:')
        note = ''
        if ins.mnemonic in ('bl', 'b') and ins.op_str.startswith('#'):
            t = int(ins.op_str[1:], 0)
            if t in by_addr:
                note = f'  ; {by_addr[t]}'
            elif t in plt:
                note = f'  ; plt {plt[t]}'
        print(f'  {ins.address:#08x}  {ins.mnemonic:8} {ins.op_str}{note}')


if __name__ == '__main__':
    sys.exit(main())
