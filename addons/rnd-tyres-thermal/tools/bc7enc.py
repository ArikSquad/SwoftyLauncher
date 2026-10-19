"""Minimal BC7 encoder (mode 6 only) for UI masks whose colour is (near) constant.

Mode 6 is one subset, RGBA 7-bit endpoints plus one p-bit per endpoint and
4-bit indices. For each 4x4 block the endpoints are the min/max alpha texels,
which is exact for a single varying channel (white line art on alpha). The
output decodes with Swofty UI's bc7.js (bcdec port), which round_trip() checks
against the source texels.
"""
from common import bc7_decode

W4 = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64]


def _interp(a, b, w):
    return ((64 - w) * a + w * b + 32) >> 6


def _endpoint(rgba):
    best = None
    for p in (0, 1):
        q = [min(127, max(0, (c - p + 1) >> 1)) for c in rgba]
        val = [(x << 1) | p for x in q]
        err = sum((v - c) ** 2 for v, c in zip(val, rgba))
        if best is None or err < best[0]:
            best = (err, q, p, val)
    return best[1], best[2], best[3]


def _block(px):
    lo = min(px, key=lambda t: t[3])
    hi = max(px, key=lambda t: t[3])
    e0, p0, v0 = _endpoint(list(lo))
    e1, p1, v1 = _endpoint(list(hi))
    pal = [[_interp(v0[c], v1[c], w) for c in range(4)] for w in W4]
    idx = []
    for t in px:
        idx.append(min(range(16), key=lambda i: sum((pal[i][c] - t[c]) ** 2 * (4 if c == 3 else 1) for c in range(4))))
    if idx[0] >= 8:
        e0, e1, p0, p1 = e1, e0, p1, p0
        idx = [15 - i for i in idx]
    bits = 1 << 6
    pos = 7

    def put(v, n):
        nonlocal bits, pos
        bits |= (v & ((1 << n) - 1)) << pos
        pos += n

    for c in range(4):
        put(e0[c], 7)
        put(e1[c], 7)
    put(p0, 1)
    put(p1, 1)
    put(idx[0], 3)
    for i in idx[1:]:
        put(i, 4)
    assert pos == 128
    return bits.to_bytes(16, 'little')


def encode(rgba, w, h):
    assert w % 4 == 0 and h % 4 == 0
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            px = []
            for y in range(4):
                o = ((by + y) * w + bx) * 4
                for x in range(4):
                    px.append(tuple(rgba[o + 4 * x:o + 4 * x + 4]))
            out += _block(px)
    return bytes(out)


def round_trip(bc7, rgba, w, h):
    dec = bc7_decode(bc7, w, h)
    diff = [abs(a - b) for a, b in zip(dec, rgba)]
    return max(diff), sum(diff) / len(diff), dec
