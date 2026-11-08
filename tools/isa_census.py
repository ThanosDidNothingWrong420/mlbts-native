"""ARM64 instruction census of a decompressed NSO image (.elfimg from nsoinfo.py).

Usage:
    python -I tools/isa_census.py IMAGE.elfimg TEXT_SIZE_HEX OUT.json [--lifter-dir DIR]

- Capstone mnemonic histogram and feature classes over every word of .text that decodes.
- Optional coverage oracle: with --lifter-dir pointing at a checkout's recomp/ directory (TotK Recomp's
  lifter.py + simd_c.py, MIT), every distinct decodable word is lifted with simd='c' and the Unsupported
  reasons are tallied, weighted by occurrence.
"""
import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs

try:
    from capstone import CS_ARCH_AARCH64 as _A64
except ImportError:
    _A64 = CS_ARCH_ARM64


def classify(mn, op):
    regs = op.replace(" ", "")
    if mn.startswith(("aes", "sha1", "sha256", "sha512", "sha3", "pmull", "eor3", "rax1", "xar", "bcax", "sm3", "sm4")):
        return "crypto"
    if mn.startswith("crc32"):
        return "crc32"
    if mn in ("ldadd", "ldclr", "ldeor", "ldset", "swp", "cas", "casp") or mn.startswith(
            ("ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin", "ldumax", "ldumin", "swp", "cas")):
        return "lse_atomic"
    if mn.startswith(("ldxr", "ldaxr", "stxr", "stlxr", "ldxp", "ldaxp", "stxp", "stlxp", "clrex")):
        return "exclusive"
    if mn.startswith(("ldar", "stlr", "ldapr", "dmb", "dsb", "isb")):
        return "barrier_acqrel"
    if mn in ("svc", "brk", "hlt", "hvc", "smc", "udf"):
        return "exception"
    if mn in ("mrs", "msr", "sys", "sysl", "hint", "nop", "yield", "wfe", "wfi", "sev", "sevl", "dc", "ic", "tlbi",
              "prfm", "prfum", "bti", "paciasp", "autiasp", "pacibsp", "autibsp", "xpaclri", "retaa", "retab"):
        return "system_hint"
    if any(t in regs for t in (".16b", ".8b", ".8h", ".4h", ".4s", ".2s", ".2d", ".1d", ".h[", ".s[", ".d[", ".b[")):
        return "neon_vector"
    if mn.startswith("f") or mn in ("scvtf", "ucvtf") or (regs[:1] in ("s", "d", "h") and regs[1:2].isdigit()):
        return "fp_scalar"
    if regs[:1] == "q" and regs[1:2].isdigit():
        return "simd_q_ldst"
    if mn.startswith(("ld", "st")):
        return "load_store"
    if mn in ("b", "bl", "br", "blr", "ret", "cbz", "cbnz", "tbz", "tbnz") or mn.startswith("b."):
        return "branch"
    return "integer"


def main():
    img_path, text_size, out_path = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
    lifter_dir = None
    if "--lifter-dir" in sys.argv:
        lifter_dir = sys.argv[sys.argv.index("--lifter-dir") + 1]
    img = Path(img_path).read_bytes()
    words = np.frombuffer(img, dtype="<u4", count=text_size // 4)
    uniq, counts = np.unique(words, return_counts=True)
    md = Cs(_A64, CS_MODE_ARM)
    mn_hist, cls_hist = Counter(), Counter()
    valid = {}
    undecodable = 0
    for w, c in zip(uniq.tolist(), counts.tolist()):
        r = list(md.disasm_lite(w.to_bytes(4, "little"), 0, 1))
        if not r:
            undecodable += c
            continue
        _, _, mn, op = r[0]
        valid[w] = (mn, c)
        mn_hist[mn] += c
        cls_hist[classify(mn, op)] += c
    out = {"image": Path(img_path).name, "text_words": int(words.size), "distinct_words": int(uniq.size),
           "undecodable_words": undecodable, "distinct_mnemonics": len(mn_hist),
           "classes": dict(cls_hist.most_common()), "mnemonics": dict(mn_hist.most_common())}
    if lifter_dir:
        sys.path.insert(0, lifter_dir)
        from lifter import Lifter, Unsupported  # noqa: E402
        lf = Lifter(0x7000000000, simd="c")
        reasons, by_mn, ok, errs = Counter(), Counter(), 0, Counter()
        pc = 0x7000000000 + 0x1000
        for w, (mn, c) in valid.items():
            try:
                lf.lift(w, pc)
                ok += c
            except Unsupported as e:
                key = str(e)
                key = key if len(key) < 40 else key[:40]
                reasons[" ".join(k for k in key.split() if not all(ch in "0123456789abcdef" for ch in k)) or key] += c
                by_mn[mn] += c
            except Exception as e:  # lifter bug / needs context
                errs[f"{type(e).__name__}:{mn}"] += c
        total = sum(c for _, c in valid.values())
        out["lifter_oracle"] = {"lifter_dir": lifter_dir, "decodable_words": total, "lifted_ok": ok,
                                "unsupported_words": sum(reasons.values()),
                                "unsupported_reasons": dict(reasons.most_common(60)),
                                "unsupported_by_mnemonic": dict(by_mn.most_common(80)),
                                "other_errors": dict(errs.most_common(30))}
    Path(out_path).write_text(json.dumps(out, indent=1))
    print(json.dumps({k: v for k, v in out.items() if k not in ("mnemonics",)}, indent=1)[:6000])


if __name__ == "__main__":
    main()
