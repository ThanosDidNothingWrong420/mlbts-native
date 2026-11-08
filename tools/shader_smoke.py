"""Translate GPU programs from MLB's FXB shader banks with the shader translator, offline (docs/graphics-plan.md G3).

    python -I tools/shader_smoke.py [--romfs work/romfs/romfs] [--banks N] [--per-bank N] [--out work/shader_smoke]

Each graphics program in an .FXBNVN bank starts with a 0x30-byte header whose first word is 0x12345678, followed by the
0x50-byte Maxwell SPH and the SASS. The SPH's type field (bits 10-13 of word 0: 1 vertex, 5 fragment) gives the stage.
The code handed to the translator is the SPH and everything up to the next program header or the end of the bank (the
translator stops at the program's end itself). Textures are declared 2D float for every slot the translator asks
about; this checks that the code decodes and translates, not that bindings are right. Prints counts only.
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAGIC = struct.pack('<I', 0x12345678)
STAGE = {1: 'vs', 5: 'ps'}


def programs(data):
    starts = []
    i = data.find(MAGIC)
    while i >= 0:
        if i % 4 == 0 and i + 0x80 <= len(data):
            sph0 = struct.unpack_from('<I', data, i + 0x30)[0]
            sph_type = (sph0 >> 10) & 0xF
            if (sph0 & 0x1F) == 1 or (sph0 & 0x1F) == 2 or sph_type in STAGE:   # SPH type 1 / version, or stage
                starts.append(i)
        i = data.find(MAGIC, i + 4)
    for k, s in enumerate(starts):
        end = starts[k + 1] if k + 1 < len(starts) else len(data)
        sph0 = struct.unpack_from('<I', data, s + 0x30)[0]
        stage = STAGE.get((sph0 >> 10) & 0xF)
        if stage:
            yield s, stage, data[s + 0x30:end]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--romfs', type=Path, default=ROOT / 'work' / 'romfs' / 'romfs')
    ap.add_argument('--banks', type=int, default=12)
    ap.add_argument('--per-bank', type=int, default=6)
    ap.add_argument('--out', type=Path, default=ROOT / 'work' / 'shader_smoke')
    ap.add_argument('--xlate', type=Path, default=ROOT / 'work' / 'xlate' / 'totk_xlate.exe')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    banks = sorted(p for p in (a.romfs / 'NSW' / 'SHADERS').rglob('*.FXBNVN') if 'COMPUTE' not in p.name)
    step = max(1, len(banks) // a.banks)
    jobs = []
    for bank in banks[::step][:a.banks]:
        data = bank.read_bytes()
        for n, (off, stage, code) in enumerate(programs(data)):
            if n >= a.per_bank:
                break
            key = f'{bank.stem.lower()}_{off:x}_{stage}'
            cpath = a.out / f'{key}.bin'
            cpath.write_bytes(code)
            jobs.append({'key': key, 'stage': stage, 'code': str(cpath), 'c1': '', 'tex': {}, 'img': {}})
    jobs_file = a.out / 'jobs.json'
    jobs_file.write_text(json.dumps(jobs))
    r = subprocess.run([str(a.xlate), str(jobs_file), str(a.out)], capture_output=True, text=True)
    ok = sum(1 for j in jobs if (a.out / f"{j['key']}.spv").is_file())
    by_stage = {s: (sum(1 for j in jobs if j['stage'] == s and (a.out / f"{j['key']}.spv").is_file()),
                    sum(1 for j in jobs if j['stage'] == s)) for s in ('vs', 'ps')}
    print(f'{len(banks)} banks, {len(jobs)} programs from {min(len(banks), a.banks)} of them: {ok} translated to SPIR-V; '
          + ', '.join(f'{s} {o}/{t}' for s, (o, t) in by_stage.items()))
    fails = [l for l in (r.stdout + r.stderr).splitlines() if 'fail' in l.lower()][:8]
    for l in fails:
        print('  ', l[:200])
    return 0 if ok == len(jobs) else 1


if __name__ == '__main__':
    sys.exit(main())
