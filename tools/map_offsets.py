#!/usr/bin/env python3
"""Translate SourceMod's TF2 vtable offsets to TF2: Gold Rush by aligning vtables.

Both server.dlls are x64 MSVC builds of TF2 code without symbols. For a class, each
vtable slot's function is fingerprinted (its first bytes with call/jump targets and
RIP-relative displacements masked, so relocation differences don't matter). The two
fingerprint sequences are aligned (Needleman-Wunsch: match, mismatch, gap), which gives
a TF2-slot -> Gold Rush-slot map. A slot is only trusted when it is anchored: the
fingerprints match exactly, or both neighbours on each side matched exactly.

  python -I map_offsets.py <tf2 server.dll> <goldrush server.dll> <class> [slot ...]
"""
import hashlib
import struct
import sys

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vtable_find as vf  # noqa: E402

FP_BYTES = 48


def fingerprint(pe, rva):
    o = pe.off(rva)
    raw = bytearray(pe.b[o:o + FP_BYTES])
    k = 0
    while k < len(raw) - 5:
        op = raw[k]
        if op in (0xE8, 0xE9):                       # call/jmp rel32
            raw[k + 1:k + 5] = b"\0\0\0\0"
            k += 5
            continue
        if op in (0x48, 0x4C, 0x49, 0x4D) and k + 7 <= len(raw) and (raw[k + 2] & 0xC7) == 0x05:
            raw[k + 3:k + 7] = b"\0\0\0\0"           # REX + op + modrm(rip) + disp32
            k += 7
            continue
        if op == 0xFF and (raw[k + 1] & 0xC7) == 0x05 and k + 6 <= len(raw):
            raw[k + 2:k + 6] = b"\0\0\0\0"           # call/jmp [rip+disp32]
            k += 6
            continue
        if op == 0xC3 or op == 0xCC:
            raw = raw[:k + 1]
            break
        k += 1
    return hashlib.md5(bytes(raw)).hexdigest()[:12]


def align(a, b):
    n, m = len(a), len(b)
    gap, mis = -2, -1
    score = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        score[i][0] = i * gap
    for j in range(1, m + 1):
        score[0][j] = j * gap
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            s = 3 if a[i - 1] == b[j - 1] else mis
            score[i][j] = max(score[i - 1][j - 1] + s, score[i - 1][j] + gap, score[i][j - 1] + gap)
    i, j, pairs = n, m, {}
    while i > 0 and j > 0:
        s = 3 if a[i - 1] == b[j - 1] else mis
        if score[i][j] == score[i - 1][j - 1] + s:
            pairs[i - 1] = j - 1
            i, j = i - 1, j - 1
        elif score[i][j] == score[i - 1][j] + gap:
            i -= 1
        else:
            j -= 1
    return pairs


def mapping(tf2, gr, cls):
    fa = [fingerprint(tf2, r) for r in tf2.vfuncs(tf2.vtable(cls))]
    fb = [fingerprint(gr, r) for r in gr.vfuncs(gr.vtable(cls))]
    pairs = align(fa, fb)
    out = {}
    for i, j in pairs.items():
        exact = fa[i] == fb[j]
        anchored = exact or all(pairs.get(i + d) == j + d and fa[i + d] == fb[j + d]
                                for d in (-2, -1, 1, 2) if 0 <= i + d < len(fa))
        out[i] = (j, exact, anchored)
    return out, len(fa), len(fb)


def main():
    tf2, gr = vf.PE(sys.argv[1]), vf.PE(sys.argv[2])
    cls = sys.argv[3]
    m, na, nb = mapping(tf2, gr, cls)
    print("%s: TF2 %d slots, Gold Rush %d slots, %d aligned (%d exact)" %
          (cls, na, nb, len(m), sum(1 for v in m.values() if v[1])))
    for s in sys.argv[4:]:
        s = int(s)
        print("  TF2 %d -> %s" % (s, m.get(s)))


if __name__ == "__main__":
    main()
