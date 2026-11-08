"""List / extract the patched RomFS of a base + update NSP pair (sparse base, BKTR patch, compressed storage).

Usage:
    python -I tools/romfs_dump.py --keys prod.keys --titlekeys title.keys BASE.nsp UPDATE.nsp OUTDIR
        [--verify N] [--extract GLOB ...] [--extract-all]

Writes OUTDIR/romfs_list.tsv (size, data offset, path) and OUTDIR/romfs_info.json. --verify N checks N random IVFC
data blocks (and every hash block of the upper levels touched) against the level above, up to the master hash.
Extracted files go to OUTDIR/romfs/.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ncastorage as ns  # noqa: E402
import nxextract as nx  # noqa: E402


def open_program(nsp, keys, titlekeys):
    fh = open(nsp, "rb")
    whole = nx.File(fh, 0, os.path.getsize(nsp))
    entries = nx.parse_pfs0(whole.read)
    tickets = {}
    for name, off, size in entries:
        if name.endswith(".tik"):
            t = whole.read(off, size)
            tickets[t[0x2A0:0x2B0].hex()] = t[0x180:0x190]
    for name, off, size in entries:
        if name.endswith(".nca") and not name.endswith(".cnmt.nca"):
            n = nx.Nca(nx.File(fh, off, size), keys, titlekeys, tickets)
            if n.content_type == 0:
                sec = next(s for s in n.sections if s["fs_type"] == 0)
                return n, sec
    raise SystemExit(f"no program NCA in {nsp}")


def verify_ivfc(info, n, master):
    """Check n random level-6 blocks up the hash chain. Returns (checked, failures)."""
    st, lv = info["ivfc"], info["levels"]
    fails = 0
    rnd = random.Random(1234)
    data_off, data_size, data_log = lv[5]
    nblocks = -(-data_size // (1 << data_log))
    picks = [0, nblocks - 1] + [rnd.randrange(nblocks) for _ in range(max(0, n - 2))]
    for b in picks:
        idx = b
        for level in range(5, 0, -1):
            off, size, log = lv[level]
            bs = 1 << log
            start = idx * bs
            chunk = st.read(off + start, min(bs, size - start))
            chunk += bytes(bs - len(chunk))  # last block hashed zero-padded
            h = hashlib.sha256(chunk).digest()
            poff = lv[level - 1][0]
            expect = st.read(poff + idx * 32, 32)
            if h != expect:
                fails += 1
                break
            idx = (idx * 32) >> lv[level - 1][2]
        else:
            l1off, l1size, _ = lv[0]
            if hashlib.sha256(st.read(l1off, l1size)).digest() != master:
                fails += 1
    return len(picks), fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keys", required=True)
    ap.add_argument("--titlekeys")
    ap.add_argument("base")
    ap.add_argument("update")
    ap.add_argument("outdir")
    ap.add_argument("--verify", type=int, default=64)
    ap.add_argument("--extract", nargs="*", default=[])
    ap.add_argument("--extract-all", action="store_true")
    a = ap.parse_args()
    keys = nx.load_keys(a.keys)
    tk = nx.load_keys(a.titlekeys) if a.titlekeys else {}
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    base_nca, base_sec = open_program(a.base, keys, tk)
    upd_nca, upd_sec = open_program(a.update, keys, tk)
    romfs, info = ns.patched_romfs(upd_nca, upd_sec, base_nca, base_sec)
    f = ns.fsh_fields(upd_sec["fsh"])
    checked, fails = verify_ivfc(info, a.verify, f["ivfc_master_hash"])
    print(f"IVFC verify: {checked} blocks, {fails} failures")
    hdr = romfs.read(0, 0x50)
    files = nx.parse_romfs(romfs.read, 0)
    rep = {k: v for k, v in info.items() if k not in ("ivfc", "levels")}
    rep.update(ivfc_blocks_checked=checked, ivfc_failures=fails, romfs_header_size=int.from_bytes(hdr[:8], "little"),
               file_count=len(files), total_bytes=sum(s for _, _, s in files))
    with open(out / "romfs_list.tsv", "w", encoding="utf-8") as lf:
        for p, off, size in files:
            lf.write(f"{size}\t{off}\t{p}\n")
    (out / "romfs_info.json").write_text(json.dumps(rep, indent=1))
    print(json.dumps(rep, indent=1))
    pats = a.extract
    if a.extract_all or pats:
        done = 0
        for p, off, size in sorted(files, key=lambda x: x[1]):
            if not a.extract_all and not any(fnmatch.fnmatch(p.lower(), g.lower()) for g in pats):
                continue
            dst = out / "romfs" / p.lstrip("/")
            dst.parent.mkdir(parents=True, exist_ok=True)
            with open(dst, "wb") as w:
                pos = 0
                while pos < size:
                    n = min(16 << 20, size - pos)
                    w.write(romfs.read(off + pos, n))
                    pos += n
            done += 1
        print("extracted", done, "files")


if __name__ == "__main__":
    main()
