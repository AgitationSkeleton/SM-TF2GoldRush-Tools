#!/usr/bin/env python3
"""Find virtual-function indices in an x64 MSVC Source server.dll (no symbols needed).

Method: a function is located by a string only it references (RIP-relative LEA in
.text), its exact bounds come from the .pdata unwind table, and its vtable slot is
found in the class's vtable, located through MSVC RTTI (TypeDescriptor ->
CompleteObjectLocator -> the pointer just before the vtable).

  python -I vtable_find.py <server.dll> <class> <anchor string> [<anchor string> ...]
  python -I vtable_find.py <server.dll> --dump <class> [count]
"""
import struct
import sys


class PE:
    def __init__(self, path):
        self.b = b = open(path, "rb").read()
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<Q", b, opt + 24)[0]
        nsec = struct.unpack_from("<H", b, pe + 6)[0]
        so = opt + struct.unpack_from("<H", b, pe + 20)[0]
        self.secs = []
        for i in range(nsec):
            name, vs, va, rs, ro = struct.unpack_from("<8sIIII", b, so + i * 40)
            self.secs.append((name.rstrip(b"\0").decode(), va, max(vs, rs), ro, rs))
        dd = opt + 112
        self.pdata_rva, self.pdata_size = struct.unpack_from("<II", b, dd + 3 * 8)

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def off(self, rva):
        for _, va, sz, ro, rs in self.secs:
            if va <= rva < va + sz and rva - va < rs:
                return rva - va + ro
        return None

    def in_sec(self, rva, name):
        _, va, sz, _, _ = self.sec(name)
        return va <= rva < va + sz

    def funcs(self):
        o = self.off(self.pdata_rva)
        for i in range(self.pdata_size // 12):
            yield struct.unpack_from("<III", self.b, o + i * 12)

    def func_of(self, rva):
        """Start of the function containing rva, following chained unwind info: MSVC splits
        cold code into separate .pdata fragments whose UNWIND_INFO chains to the parent."""
        for beg, end, unw in self.funcs():
            if beg <= rva < end:
                for _ in range(16):
                    o = self.off(unw & ~3)
                    flags = self.b[o] >> 3
                    if not flags & 0x4:          # UNW_FLAG_CHAININFO
                        return beg
                    ncodes = self.b[o + 2]
                    co = o + 4 + 2 * ((ncodes + 1) & ~1)
                    beg, end, unw = struct.unpack_from("<III", self.b, co)
                return beg
        return None

    def string_rvas(self, s):
        raw = s.encode() + b"\0"
        out, i = [], 0
        while True:
            i = self.b.find(raw, i)
            if i < 0:
                return out
            if i == 0 or self.b[i - 1] == 0:
                for _, va, sz, ro, rs in self.secs:
                    if ro <= i < ro + rs:
                        out.append(i - ro + va)
            i += 1

    def lea_refs(self, target):
        """RVAs of RIP-relative instructions (lea/mov) in .text that reference target."""
        _, va, sz, ro, rs = self.sec(".text")
        b = self.b
        refs = []
        for k in range(ro, ro + rs - 7):
            # REX.W 8D /r with modrm mod=00 rm=101 (RIP-relative)
            if b[k] in (0x48, 0x4C) and b[k + 1] == 0x8D and (b[k + 2] & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", b, k + 3)[0]
                rva = k - ro + va
                if rva + 7 + disp == target:
                    refs.append(rva)
        return refs

    def vtable(self, cls):
        name = (".?AV%s@@" % cls).encode()
        i = self.b.find(name + b"\0")
        if i < 0:
            raise SystemExit("no RTTI for " + cls)
        td_off = i - 0x10
        td_rva = next(td_off - ro + va for _, va, sz, ro, rs in self.secs if ro <= td_off < ro + rs)
        _, rva0, rsz, rro, rrs = self.sec(".rdata")
        cols = []
        for k in range(rro, rro + rrs - 24, 4):
            sig, offset, cdoff, ptd = struct.unpack_from("<IIII", self.b, k)
            if sig == 1 and ptd == td_rva and offset == 0:
                cols.append(k - rro + rva0)
        for col in cols:
            want = struct.pack("<Q", self.base + col)
            j = self.b.find(want)
            while j >= 0:
                vt_rva = next(j - ro + va for _, va, sz, ro, rs in self.secs if ro <= j < ro + rs) + 8
                return vt_rva
        raise SystemExit("no vtable for " + cls)

    def vfuncs(self, vt_rva, limit=2000):
        out = []
        o = self.off(vt_rva)
        for n in range(limit):
            va = struct.unpack_from("<Q", self.b, o + 8 * n)[0]
            rva = va - self.base
            if not (0 < rva and self.in_sec(rva, ".text")):
                break
            out.append(rva)
        return out


def main():
    pe = PE(sys.argv[1])
    if sys.argv[2] == "--dump":
        vt = pe.vtable(sys.argv[3])
        fs = pe.vfuncs(vt)
        print("%s vtable @%x: %d entries" % (sys.argv[3], vt, len(fs)))
        return
    cls = sys.argv[2]
    vt = pe.vtable(cls)
    fs = pe.vfuncs(vt)
    print("%s vtable rva %x, %d slots" % (cls, vt, len(fs)))
    for s in sys.argv[3:]:
        for srva in pe.string_rvas(s):
            for ref in pe.lea_refs(srva):
                f = pe.func_of(ref)
                idx = [i for i, x in enumerate(fs) if x == f]
                print("  %-50r ref %x in func %s -> slot %s" % (s[:50], ref, hex(f) if f else None, idx))


if __name__ == "__main__":
    main()
