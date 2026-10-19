"""Composite the development-potential pie the way the game draws it, stock vs 5-sector.

Per sector: comp_potential (full-radius wedge), potential_nub (dashed arc) scaled by
potential_perf_fraction, current_nub (filled wedge) scaled by perf_fraction, all tinted
with dept_colour (the .ui colourOverride), over the bg disc (me_colour alpha 0.102).
Geometry comes from the shipped vertex/UV buffers and scene transforms; textures are the
decoded stock BC7 masks and the new graph_b5 backdrop. Sample data stands in for live
department_performance values. Writes pie_stock_composite.png, pie_5_composite.png and
pie_compare.png (both side by side) to --out.
"""
import argparse
import json
import math
import os
import struct
import zlib

import numpy as np

import common

R = 1.12
BG_ALPHA = 0.10196
PANEL = np.array([27, 31, 36], float) / 255

COLOURS = {
    'aero': 0xffeead00, 'chassis': 0xff3ec58d, 'maintenance': 0xff16deff,
    'engine': 0xff7a2aed, 'tyres': 0xff0078ff,
}
LABELS = {'aero': 'Aerodynamics', 'chassis': 'Chassis', 'maintenance': 'Durability',
          'engine': 'Powertrain', 'tyres': 'Tyres & Thermal'}
SAMPLE = {'aero': (0.48, 0.95), 'chassis': (0.27, 0.92), 'maintenance': (0.45, 0.90),
          'engine': (0.55, 0.97), 'tyres': (0.36, 0.74)}

STOCK_LAYOUT = [('chassis', 0.0, 90.0, (0.04, 0.04)), ('maintenance', -90.0, 90.0, (0.04, -0.04)),
                ('engine', 180.0, 90.0, (-0.04, -0.04)), ('aero', 90.0, 90.0, (-0.04, 0.04))]


def png(path, w, h, rgba):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def five_layout():
    out = []
    off = 0.04 / math.sin(math.radians(36))
    for i, d in enumerate(['chassis', 'maintenance', 'engine', 'aero', 'tyres']):
        start = 90.0 - 72.0 * (i + 1)
        mid = math.radians(start + 36)
        out.append((d, start, 72.0, (off * math.cos(mid), off * math.sin(mid))))
    return out


def rgb(abgr):
    return np.array([abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff], float) / 255


def tex(w, h, rgba):
    return (np.frombuffer(rgba, np.uint8).astype(float) / 255).reshape(h, w, 4)


def sample(t, u, v):
    h, w = t.shape[:2]
    x = np.clip(u * w - 0.5, 0, w - 1.001)
    y = np.clip(v * h - 0.5, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    return (t[y0, x0] * (1 - fx) * (1 - fy) + t[y0, x0 + 1] * fx * (1 - fy)
            + t[y0 + 1, x0] * (1 - fx) * fy + t[y0 + 1, x0 + 1] * fx * fy)


def over(dst, col, a):
    a = a[..., None]
    return dst * (1 - a) + col * a


def render(layout, bg, masks, size, span=1.3):
    t_comp, t_cur, t_pot = masks['comp_potential'], masks['current_nub'], masks['potential_nub']
    ys, xs = np.mgrid[0:size, 0:size]
    X = (xs + 0.5) / size * 2 * span - span
    Y = span - (ys + 0.5) / size * 2 * span
    img = np.broadcast_to(PANEL, (size, size, 3)).copy()
    inside = (np.abs(X) <= 1.16) & (np.abs(Y) <= 1.16)
    s = sample(bg, (X + 1.16) / 2.32, (1.16 - Y) / 2.32)
    img = over(img, s[..., :3], np.where(inside, s[..., 3] * BG_ALPHA, 0))
    for dept, start, ang, off in layout:
        c, p = SAMPLE[dept]
        tint = rgb(COLOURS[dept])
        th = math.radians(-start)
        lx = (X - off[0]) * math.cos(th) - (Y - off[1]) * math.sin(th)
        ly = (X - off[0]) * math.sin(th) + (Y - off[1]) * math.cos(th)
        for t, scale in ((t_comp, 1.0), (t_pot, p), (t_cur, c)):
            qx, qy = lx / scale, ly / scale
            a = np.degrees(np.arctan2(qy, qx))
            m = (qx >= 0) & (qy >= 0) & (qx <= R) & (qy <= R) & (a >= 0) & (a <= ang)
            s = sample(t, qx / R, 1 - qy / R)
            img = over(img, s[..., :3] * tint, np.where(m, s[..., 3], 0))
    return img


def to_png(img, path):
    h, w = img.shape[:2]
    rgba = np.concatenate([np.clip(img, 0, 1), np.ones((h, w, 1))], axis=2)
    png(path, w, h, (rgba * 255 + 0.5).astype(np.uint8).tobytes())


def main():
    ap = argparse.ArgumentParser(description='Composite the stock and five-sector pies.')
    common.add_game_arg(ap)
    ap.add_argument('--out', default=os.path.join(common.BUILD, 'preview'), help='output folder (default: build/preview)')
    a = ap.parse_args()
    career = common.stock(a.game)
    masks = {k: tex(*career.texture(t)) for k, t in common.MASKS.items()}
    graph_bg = tex(*career.texture(common.GRAPH_BG))
    with open(os.path.join(common.ADDON, 'ui', 'pie', 'bg', 'graph_b5.tif.image.f1'), 'rb') as f:
        graph_b5 = tex(232, 232, common.bc7_decode(f.read(), 232, 232))
    size = 640
    stock = render(STOCK_LAYOUT, graph_bg, masks, size)
    five = render(five_layout(), graph_b5, masks, size)
    os.makedirs(a.out, exist_ok=True)
    to_png(stock, os.path.join(a.out, 'pie_stock_composite.png'))
    to_png(five, os.path.join(a.out, 'pie_5_composite.png'))
    to_png(np.concatenate([stock, five], axis=1), os.path.join(a.out, 'pie_compare.png'))
    json.dump({'sample': SAMPLE, 'layout': [(d, s, an, o) for d, s, an, o in five_layout()]},
              open(os.path.join(a.out, 'pie_5_composite.json'), 'w'), indent=1)
    print('wrote', ', '.join(os.path.join(a.out, n) for n in ('pie_stock_composite.png', 'pie_5_composite.png', 'pie_compare.png')))


if __name__ == '__main__':
    main()
