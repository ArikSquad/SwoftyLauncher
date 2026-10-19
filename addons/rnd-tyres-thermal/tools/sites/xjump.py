import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 're', 'tools'))
from pe_index import *

ranges = []
for a in sys.argv[1:]:
    s, n = a.split(':')
    s = int(s, 16)
    n = int(n)
    ranges.append((s, n))
tg = []
for s, n in ranges:
    tg += [s + k - BASE for k in range(1, n)]
refs = riprel_refs(tg)
va, b = code_bytes('.xtls')
hits = []
for site, t in refs:
    o = site - va
    op = bytes(b[o - 2:o])
    if op[1] in (0xE8, 0xE9) or (op[0] == 0x0F and 0x80 <= op[1] <= 0x8F):
        hits.append((BASE + site, BASE + t, op.hex()))
for s, n in ranges:
    lo, hi = s + 1, s + n
    for k in range(lo, hi):
        o = rva2off(k - BASE)
    # rel8 short jumps within +-128 bytes
    o0 = rva2off(s - 0x90 - BASE)
    win = bytes(DATA[o0:o0 + 0x90 + n + 0x90])
    for i in range(len(win) - 1):
        opb = win[i]
        if opb == 0xEB or 0x70 <= opb <= 0x7F:
            d = win[i + 1]
            d = d - 256 if d >= 128 else d
            tgt = s - 0x90 + i + 2 + d
            if lo <= tgt < hi:
                hits.append((s - 0x90 + i, tgt, 'rel8-candidate'))
print('rel32/rel8 candidate branches INTO hook ranges:', len(hits))
for h in hits:
    print('  from %x -> %x (%s)' % h)
