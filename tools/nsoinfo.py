"""Decode Switch NSO modules and NPDM: segments, MOD0/dynamic, symbols, relocations, SVCs, SDK/middleware strings.

Usage:
    py -I tools/nsoinfo.py EXEFS_DIR OUTDIR

Writes OUTDIR/<module>.elfimg (decompressed flat image, for local analysis only), OUTDIR/<module>.json
(summary), OUTDIR/<module>.imports.txt / .exports.txt and OUTDIR/npdm.json.
"""
import hashlib
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

import lz4.block

DT = {1: "NEEDED", 2: "PLTRELSZ", 3: "PLTGOT", 4: "HASH", 5: "STRTAB", 6: "SYMTAB", 7: "RELA", 8: "RELASZ",
      9: "RELAENT", 10: "STRSZ", 11: "SYMENT", 12: "INIT", 13: "FINI", 14: "SONAME", 16: "SYMBOLIC", 20: "PLTREL",
      23: "JMPREL", 24: "BIND_NOW", 25: "INIT_ARRAY", 26: "FINI_ARRAY", 27: "INIT_ARRAYSZ", 28: "FINI_ARRAYSZ",
      30: "FLAGS", 0x23: "RELRSZ", 0x24: "RELR", 0x25: "RELRENT", 0x6ffffef5: "GNU_HASH", 0x6ffffff9: "RELACOUNT",
      0x6ffffffb: "FLAGS_1"}
RELOC_NAMES = {257: "ABS64", 1025: "GLOB_DAT", 1026: "JUMP_SLOT", 1027: "RELATIVE", 1024: "COPY", 1030: "TLS_TPREL",
               1031: "TLSDESC"}


class Nso:
    def __init__(self, path):
        d = Path(path).read_bytes()
        self.raw_sha256 = hashlib.sha256(d).hexdigest()
        assert d[:4] == b"NSO0", path
        self.version, _, self.flags = struct.unpack_from("<III", d, 4)
        segs = []
        for i in range(3):
            foff, moff, size, extra = struct.unpack_from("<IIII", d, 0x10 + i * 0x10)
            segs.append([foff, moff, size, extra])
        self.module_id = d[0x40:0x60].hex()
        csizes = struct.unpack_from("<III", d, 0x60)
        self.api_info = struct.unpack_from("<II", d, 0x88)
        self.dynstr = struct.unpack_from("<II", d, 0x90)
        self.dynsym = struct.unpack_from("<II", d, 0x98)
        self.seg_hashes = [d[0xA0 + i * 0x20:0xC0 + i * 0x20].hex() for i in range(3)]
        self.bss = segs[2][3]
        end = max(s[1] + s[2] for s in segs)
        self.size = (end + self.bss + 0xFFF) & ~0xFFF
        img = bytearray(self.size)
        self.segments = {}
        for i, name in enumerate(("text", "rodata", "data")):
            foff, moff, size, _ = segs[i]
            blob = d[foff:foff + csizes[i]]
            if self.flags & (1 << i):
                blob = lz4.block.decompress(blob, uncompressed_size=size)
            assert len(blob) == size
            if self.flags & (1 << (i + 3)):
                assert hashlib.sha256(blob).hexdigest() == self.seg_hashes[i], f"{name} hash mismatch"
            img[moff:moff + size] = blob
            self.segments[name] = (moff, size, csizes[i])
        self.image = img

    def u32(self, a):
        return struct.unpack_from("<I", self.image, a)[0]

    def u64(self, a):
        return struct.unpack_from("<Q", self.image, a)[0]

    def parse_dynamic(self):
        mod0 = self.u32(4)
        assert self.image[mod0:mod0 + 4] == b"MOD0"
        dyn_off, bss_s, bss_e, eh_s, eh_e, modobj = struct.unpack_from("<iiiiii", self.image, mod0 + 4)
        dyn = mod0 + dyn_off
        self.mod0 = {"offset": hex(mod0), "dynamic": hex(dyn), "bss": (hex(mod0 + bss_s), hex(mod0 + bss_e)),
                     "eh_frame_hdr": (hex(mod0 + eh_s), hex(mod0 + eh_e)), "module_object": hex(mod0 + modobj)}
        self.eh_frame_hdr = (mod0 + eh_s, mod0 + eh_e)
        tags = {}
        needed = []
        a = dyn
        while True:
            t, v = struct.unpack_from("<qQ", self.image, a)
            a += 16
            if t == 0:
                break
            if t == 1:
                needed.append(v)
            else:
                tags[t] = v
        self.dyn = tags
        strtab = tags.get(5)
        self.strtab = strtab

        def cstr(o):
            e = self.image.index(b"\0", strtab + o)
            return self.image[strtab + o:e].decode("utf-8", "replace")
        self.needed = [cstr(o) for o in needed]
        # symbol count: from HASH nchain, else from dynsym extent
        if 4 in tags:
            nsyms = self.u32(tags[4] + 4)
        else:
            nsyms = (strtab - tags[6]) // 24
        syms = []
        for i in range(nsyms):
            name_o, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", self.image, tags[6] + i * 24)
            syms.append({"name": cstr(name_o), "bind": info >> 4, "type": info & 0xF, "shndx": shndx,
                         "value": value, "size": size})
        self.syms = syms
        relocs = Counter()
        sym_refs = Counter()

        def rela(off, size):
            for i in range(size // 24):
                r_off, r_info, r_add = struct.unpack_from("<QQq", self.image, off + i * 24)
                typ = r_info & 0xFFFFFFFF
                relocs[RELOC_NAMES.get(typ, typ)] += 1
                si = r_info >> 32
                if si:
                    sym_refs[si] += 1
        if 7 in tags:
            rela(tags[7], tags.get(8, 0))
        if 23 in tags:
            rela(tags[23], tags.get(2, 0))
        if 0x24 in tags:
            relocs["RELR_words"] += tags.get(0x23, 0) // 8
        self.relocs = dict(relocs)
        self.imports = sorted({syms[i]["name"] for i in sym_refs if syms[i]["shndx"] == 0 and syms[i]["name"]})
        self.undef = sorted({s["name"] for s in syms if s["shndx"] == 0 and s["name"]})
        self.exports = sorted({s["name"] for s in syms if s["shndx"] != 0 and s["name"] and s["bind"] in (1, 2)})

    def scan_text(self):
        moff, size, _ = self.segments["text"]
        import numpy as np
        w = np.frombuffer(bytes(self.image[moff:moff + size]), dtype="<u4")
        svc_mask = (w & 0xFFE0001F) == 0xD4000001
        svcs = Counter(int((x >> 5) & 0xFFFF) for x in w[svc_mask])
        # system register access: MRS/MSR (register) 0xD5300000 / 0xD5100000 class
        mrs = w[(w & 0xFFF00000) == 0xD5300000]
        msr = w[(w & 0xFFF00000) == 0xD5100000]
        sysreg = Counter()
        for x in mrs:
            sysreg["mrs " + _sysreg_name(int(x))] += 1
        for x in msr:
            sysreg["msr " + _sysreg_name(int(x))] += 1
        excl = {
            "ldxr/ldaxr": int(((w & 0x3FBF8000) == 0x085F0000).sum() + ((w & 0x3FBF8000) == 0x085F8000).sum()),
            "stxr/stlxr": int(((w & 0x3FA08000) == 0x08000000).sum() + ((w & 0x3FA08000) == 0x08008000).sum()),
            "ldxp/ldaxp": int(((w & 0xBFFF8000) == 0x887F0000).sum() + ((w & 0xBFFF8000) == 0x887F8000).sum()),
            "lse_atomics(ldadd etc)": int(((w & 0x3F208C00) == 0x38200000).sum()),
            "cas": int(((w & 0x3FA07C00) == 0x08A07C00).sum()),
        }
        return {"instructions": int(w.size), "svc": {hex(k): v for k, v in sorted(svcs.items())},
                "sysreg": dict(sysreg.most_common()), "exclusive_atomic_counts": excl}

    def strings(self, pattern):
        return sorted({m.group().decode("ascii", "replace") for m in re.finditer(pattern, bytes(self.image))})


SYSREGS = {(3, 3, 13, 0, 2): "TPIDR_EL0", (3, 3, 13, 0, 3): "TPIDRRO_EL0", (3, 3, 14, 0, 1): "CNTPCT_EL0",
           (3, 3, 14, 0, 2): "CNTVCT_EL0", (3, 3, 14, 0, 0): "CNTFRQ_EL0", (3, 3, 4, 2, 0): "NZCV",
           (3, 3, 4, 4, 0): "FPCR", (3, 3, 4, 4, 1): "FPSR", (3, 3, 0, 0, 1): "CTR_EL0", (3, 3, 0, 0, 7): "DCZID_EL0",
           (3, 0, 0, 0, 5): "MPIDR_EL1", (3, 0, 0, 0, 0): "MIDR_EL1"}


def _sysreg_name(x):
    o0 = 2 + ((x >> 19) & 1)
    op1, crn, crm, op2 = (x >> 16) & 7, (x >> 12) & 0xF, (x >> 8) & 0xF, (x >> 5) & 7
    return SYSREGS.get((o0, op1, crn, crm, op2), f"S{o0}_{op1}_C{crn}_C{crm}_{op2}")


SVC_NAMES = {0x01: "SetHeapSize", 0x02: "SetMemoryPermission", 0x03: "SetMemoryAttribute", 0x04: "MapMemory",
             0x05: "UnmapMemory", 0x06: "QueryMemory", 0x07: "ExitProcess", 0x08: "CreateThread", 0x09: "StartThread",
             0x0A: "ExitThread", 0x0B: "SleepThread", 0x0C: "GetThreadPriority", 0x0D: "SetThreadPriority",
             0x0E: "GetThreadCoreMask", 0x0F: "SetThreadCoreMask", 0x10: "GetCurrentProcessorNumber",
             0x11: "SignalEvent", 0x12: "ClearEvent", 0x13: "MapSharedMemory", 0x14: "UnmapSharedMemory",
             0x15: "CreateTransferMemory", 0x16: "CloseHandle", 0x17: "ResetSignal", 0x18: "WaitSynchronization",
             0x19: "CancelSynchronization", 0x1A: "ArbitrateLock", 0x1B: "ArbitrateUnlock",
             0x1C: "WaitProcessWideKeyAtomic", 0x1D: "SignalProcessWideKey", 0x1E: "GetSystemTick",
             0x1F: "ConnectToNamedPort", 0x20: "SendSyncRequestLight", 0x21: "SendSyncRequest",
             0x22: "SendSyncRequestWithUserBuffer", 0x23: "SendAsyncRequestWithUserBuffer", 0x24: "GetProcessId",
             0x25: "GetThreadId", 0x26: "Break", 0x27: "OutputDebugString", 0x28: "ReturnFromException",
             0x29: "GetInfo", 0x2A: "FlushEntireDataCache", 0x2B: "FlushDataCache", 0x2C: "MapPhysicalMemory",
             0x2D: "UnmapPhysicalMemory", 0x2E: "GetDebugFutureThreadInfo", 0x2F: "GetLastThreadInfo",
             0x30: "GetResourceLimitLimitValue", 0x31: "GetResourceLimitCurrentValue",
             0x32: "SetThreadActivity", 0x33: "GetThreadContext3", 0x34: "WaitForAddress", 0x35: "SignalToAddress",
             0x36: "SynchronizePreemptionState", 0x37: "GetResourceLimitPeakValue", 0x39: "CreateIoPool",
             0x3A: "CreateIoRegion", 0x3C: "KernelDebug", 0x3D: "ChangeKernelTraceState",
             0x40: "CreateSession", 0x41: "AcceptSession", 0x43: "ReplyAndReceive",
             0x44: "ReplyAndReceiveWithUserBuffer", 0x45: "CreateEvent", 0x48: "MapPhysicalMemoryUnsafe",
             0x49: "UnmapPhysicalMemoryUnsafe", 0x4A: "SetUnsafeLimit", 0x4B: "CreateCodeMemory",
             0x4C: "ControlCodeMemory", 0x4D: "SleepSystem", 0x4E: "ReadWriteRegister",
             0x4F: "SetProcessActivity", 0x50: "CreateSharedMemory", 0x51: "MapTransferMemory",
             0x52: "UnmapTransferMemory", 0x53: "CreateInterruptEvent", 0x55: "QueryIoMapping",
             0x5F: "FlushProcessDataCache", 0x60: "DebugActiveProcess", 0x65: "GetProcessList",
             0x6F: "GetSystemInfo", 0x70: "CreatePort", 0x73: "SetProcessMemoryPermission",
             0x77: "MapProcessCodeMemory", 0x78: "UnmapProcessCodeMemory", 0x7F: "CallSecureMonitor",
             0x90: "MapInsecurePhysicalMemory", 0x91: "UnmapInsecurePhysicalMemory"}


def parse_npdm(path):
    d = Path(path).read_bytes()
    assert d[:4] == b"META"
    out = {"size": len(d), "sha256": hashlib.sha256(d).hexdigest()}
    flags = d[0xC]
    out["is_64bit"] = bool(flags & 1)
    out["address_space"] = {0: "32bit", 1: "64bitOld", 2: "32bitNoReserved", 3: "64bit(39-bit)"}.get((flags >> 1) & 7)
    out["main_thread_priority"] = d[0xE]
    out["main_thread_core"] = d[0xF]
    out["system_resource_size"] = hex(struct.unpack_from("<I", d, 0x14)[0])
    out["version"] = struct.unpack_from("<I", d, 0x18)[0]
    out["main_thread_stack_size"] = hex(struct.unpack_from("<I", d, 0x1C)[0])
    out["name"] = d[0x20:0x30].split(b"\0")[0].decode()
    out["product_code"] = d[0x30:0x40].split(b"\0")[0].decode()
    aci_off, aci_sz, acid_off, acid_sz = struct.unpack_from("<IIII", d, 0x70)
    aci = d[aci_off:aci_off + aci_sz]
    assert aci[:4] == b"ACI0"
    out["program_id"] = f"{struct.unpack_from('<Q', aci, 0x10)[0]:016X}"
    fah_off, fah_sz, sac_off, sac_sz, kc_off, kc_sz = struct.unpack_from("<IIIIII", aci, 0x20)
    fah = aci[fah_off:fah_off + fah_sz]
    out["fs_access_flags"] = hex(struct.unpack_from("<Q", fah, 4)[0]) if len(fah) >= 12 else None
    sac = aci[sac_off:sac_off + sac_sz]
    services, i = [], 0
    while i < len(sac):
        c = sac[i]
        n = (c & 7) + 1
        services.append(("host:" if c & 0x80 else "") + sac[i + 1:i + 1 + n].decode())
        i += 1 + n
    out["services"] = services
    kc = struct.unpack_from(f"<{kc_sz // 4}I", aci, kc_off)
    caps = {"svcs": [], "other": []}
    for v in kc:
        if v == 0xFFFFFFFF:
            continue
        low = (~v & (v + 1))  # lowest clear bit as mask
        t = low.bit_length() - 1
        if t == 3:
            caps["thread_info"] = {"lowest_prio": (v >> 4) & 0x3F, "highest_prio": (v >> 10) & 0x3F,
                                   "min_core": (v >> 16) & 0xFF, "max_core": (v >> 24) & 0xFF}
        elif t == 4:
            idx, mask = (v >> 29) & 7, (v >> 5) & 0xFFFFFF
            for b in range(24):
                if mask & (1 << b):
                    n = idx * 24 + b
                    caps["svcs"].append(f"0x{n:02X} {SVC_NAMES.get(n, '?')}")
        elif t == 13:
            caps["program_type"] = (v >> 14) & 7
        elif t == 14:
            caps["kernel_version"] = f"{(v >> 19) & 0x1FFF}.{(v >> 15) & 0xF}"
        elif t == 15:
            caps["handle_table_size"] = (v >> 16) & 0x3FF
        elif t == 16:
            caps["debug_flags"] = {"allow_debug": bool((v >> 17) & 1), "force_debug": bool((v >> 18) & 1)}
        else:
            caps["other"].append({"type_bit": t, "value": hex(v)})
    out["kernel_caps"] = caps
    return out


def main():
    exefs, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    if (exefs / "main.npdm").exists():
        (outdir / "npdm.json").write_text(json.dumps(parse_npdm(exefs / "main.npdm"), indent=1))
    for name in ["rtld", "main"] + [f"subsdk{i}" for i in range(10)] + ["sdk"]:
        p = exefs / name
        if not p.exists():
            continue
        n = Nso(p)
        n.parse_dynamic()
        (outdir / f"{name}.elfimg").write_bytes(n.image)
        info = {"name": name, "file_size": p.stat().st_size, "file_sha256": n.raw_sha256, "module_id": n.module_id,
                "flags": hex(n.flags), "image_size": hex(n.size), "bss": hex(n.bss),
                "segments": {k: {"mem_off": hex(v[0]), "size": hex(v[1]), "compressed": hex(v[2])}
                             for k, v in n.segments.items()},
                "mod0": n.mod0, "dynamic_tags": {DT.get(k, hex(k)): hex(v) for k, v in n.dyn.items()},
                "needed": n.needed, "symbols": len(n.syms), "imports_referenced": len(n.imports),
                "undefined_symbols": len(n.undef), "exports": len(n.exports), "relocs": n.relocs}
        info.update(n.scan_text())
        info["sdk_strings"] = n.strings(rb"SDK MW\+[ -~]{3,120}")[:200]
        info["nn_version_strings"] = n.strings(rb"NintendoSdk_[ -~]{3,80}|nnSdk[ -~]{0,60}")[:50]
        (outdir / f"{name}.json").write_text(json.dumps(info, indent=1))
        (outdir / f"{name}.imports.txt").write_text("\n".join(n.undef) + "\n")
        (outdir / f"{name}.exports.txt").write_text("\n".join(n.exports) + "\n")
        print(name, info["image_size"], "syms", info["symbols"], "undef", info["undefined_symbols"],
              "exports", info["exports"], "svc", info["svc"])


if __name__ == "__main__":
    main()
