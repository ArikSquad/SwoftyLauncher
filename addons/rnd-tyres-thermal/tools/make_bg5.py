"""Build the 5-spoke development-potential backdrop from the stock graph_bg.

The stock texture (232x232, white RGB, alpha line art) is a hatched disc with a
4-arm cross that matches the stock 90-degree quadrants. This removes the cross by
re-sampling the hatch along its own direction (the hatch is invariant along its
lines), then draws five spokes on the 72-degree sector boundaries with the stock
cross profile (2 px, alpha ~136, composited 'over' the hatch like the stock
crossings). Image orientation equals screen orientation (row 0 = top), as the
in-game hatch direction confirms.
"""
import argparse
import math
import os

import numpy as np

import bc7enc
import common

N = 232
C = 116.0
HATCH_DX_PER_ROW = 1.35
OUTLINE_R = 114.0
SPOKE_ALPHA = 136.0
SPOKE_HALF_WIDTH = 1.0
BOUNDARIES_DEG = [18.0, 90.0, 162.0, 234.0, 306.0]


def remove_cross(a):
    out = a.copy()
    cross_cols = {115, 116}
    cross_rows = {115, 116}
    xs = np.arange(N, dtype=float)
    for r in range(N):
        for c in range(N):
            if c not in cross_cols and r not in cross_rows:
                continue
            if math.hypot(c + 0.5 - C, r + 0.5 - C) >= OUTLINE_R - 1.0:
                continue
            for n in (3, -3, 4, -4, 5, -5, 6, -6, 7, -7, 8, -8):
                rr = r + n
                cc = c + n * HATCH_DX_PER_ROW
                if not (0 <= rr < N) or rr in cross_rows:
                    continue
                if any(abs(cc - k) < 1.6 for k in cross_cols):
                    continue
                if math.hypot(cc + 0.5 - C, rr + 0.5 - C) >= OUTLINE_R - 1.5:
                    continue
                out[r, c] = np.interp(cc, xs, a[rr])
                break
    for r in range(N):
        for c in range(N):
            if c not in cross_cols and r not in cross_rows:
                continue
            rad = math.hypot(c + 0.5 - C, r + 0.5 - C)
            if not (OUTLINE_R - 1.0 <= rad < OUTLINE_R + 1.0):
                continue
            ang = math.atan2(r + 0.5 - C, c + 0.5 - C) + math.radians(4.0)
            sx = C + rad * math.cos(ang) - 0.5
            sy = C + rad * math.sin(ang) - 0.5
            x0, y0 = int(math.floor(sx)), int(math.floor(sy))
            fx, fy = sx - x0, sy - y0
            v = (a[y0, x0] * (1 - fx) * (1 - fy) + a[y0, x0 + 1] * fx * (1 - fy)
                 + a[y0 + 1, x0] * (1 - fx) * fy + a[y0 + 1, x0 + 1] * fx * fy)
            out[r, c] = v
    return out


def spokes(a):
    ss = 4
    yy, xx = np.mgrid[0:N * ss, 0:N * ss]
    px = (xx + 0.5) / ss
    py = (yy + 0.5) / ss
    cov = np.zeros((N * ss, N * ss))
    for deg in BOUNDARIES_DEG:
        t = math.radians(deg)
        dx, dy = math.cos(t), -math.sin(t)
        vx, vy = px - C, py - C
        along = vx * dx + vy * dy
        perp = np.abs(vx * dy - vy * dx)
        hit = (along >= 0) & (along <= OUTLINE_R - 0.5) & (perp <= SPOKE_HALF_WIDTH)
        cov = np.maximum(cov, hit.astype(float))
    cov = cov.reshape(N, ss, N, ss).mean(axis=(1, 3))
    line = SPOKE_ALPHA / 255.0 * cov
    base = a / 255.0
    return np.clip((line + base * (1.0 - line)) * 255.0, 0, 255)


def build(stock_rgba, out_dir):
    rgba = np.frombuffer(stock_rgba, np.uint8).reshape(N, N, 4).copy()
    alpha = rgba[:, :, 3].astype(float)
    clean = remove_cross(alpha)
    final = spokes(clean)
    rgba[:, :, 3] = np.round(final).astype(np.uint8)
    rgba[:, :, 0:3] = 255
    raw = rgba.tobytes()
    bc7 = bc7enc.encode(raw, N, N)
    mx, mean, dec = bc7enc.round_trip(bc7, raw, N, N)
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, 'graph_b5.rgba'), 'wb').write(raw)
    open(os.path.join(out_dir, 'graph_b5.bc7'), 'wb').write(bc7)
    open(os.path.join(out_dir, 'graph_b5.decoded.rgba'), 'wb').write(dec)
    clean_rgba = rgba.copy()
    clean_rgba[:, :, 3] = np.round(clean).astype(np.uint8)
    open(os.path.join(out_dir, 'graph_nocross.rgba'), 'wb').write(clean_rgba.tobytes())
    return bc7, mx, mean


def stock_graph_bg(game):
    w, h, rgba = common.stock(game).texture(common.GRAPH_BG)
    assert (w, h) == (N, N), (w, h)
    return rgba


def main():
    ap = argparse.ArgumentParser(description='Build the 5-spoke graph_b5 backdrop from the stock graph_bg.')
    common.add_game_arg(ap)
    ap.add_argument('--out', default=os.path.join(common.BUILD, 'tex'), help='output folder (default: build/tex)')
    a = ap.parse_args()
    bc7, mx, mean = build(stock_graph_bg(a.game), a.out)
    print(f'graph_b5: {len(bc7)} bytes BC7, round-trip max err {mx}, mean {mean:.3f}')


if __name__ == '__main__':
    main()
