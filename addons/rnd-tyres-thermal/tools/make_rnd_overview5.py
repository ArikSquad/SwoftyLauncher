"""Add tile_5 to art/screens/career/rnd_overview.idf and lay the five tiles out 3+2.

tile_5 (under transition_null_8) is a clone of tile_4 (under transition_null_4). The
top row keeps three tiles at --scale; --bottom fill widens the bottom two so both rows
span the same width. A scene that already has tile_5 is only re-laid out. The clone's
guids are fixed so the output is reproducible.
"""
import argparse
import sys

import common  # noqa: F401
import idf as I
from pkg import Val

URI = 'eaid://ui_package/art/screens/career/rnd_overview.idf'
TILE_W, TILE_H, GAP = 4.15, 2.32, 0.2
GUIDS = {
    'transition_null_8': '6f55a395-87b2-43bd-b837-7833e3e72a12',
    'tile_5': '0c85cfdd-e565-4dfa-b7bc-8d4f83e3d793',
}


def element(sc, name):
    e = sc.root.find(name)
    if e is None:
        sys.exit(f'{name} not found')
    return e


def place(tile, parent_pos, target, s):
    x, y, z = parent_pos
    tile.obj.set('position', Val('vec3', [target[0] - x, target[1] - y, 0.0, 0.0]))
    tile.obj.set('scale', Val('vec3', [s, s, 1.0, 0.0]))


def layout(nulls, scale, align, bottom):
    xs = [n.position[0] for n in nulls]
    ys = [n.position[1] for n in nulls]
    cx = (min(xs) + max(xs)) / 2
    top = max(ys) + TILE_H / 2
    low = min(ys) - TILE_H / 2
    w, h = TILE_W * scale, TILE_H * scale
    row_w = 3 * w + 2 * GAP
    s2 = (row_w - GAP) / 2 / TILE_W if bottom == 'fill' else scale
    w2, h2 = TILE_W * s2, TILE_H * s2
    if align == 'top':
        row1 = top - h / 2
    elif s2 == scale:
        row1 = (top + low) / 2 + (h + GAP) / 2
    else:
        row1 = (top + low) / 2 + (h + GAP + h2) / 2 - h / 2
    row2 = row1 - (h + GAP) if s2 == scale else row1 - h / 2 - GAP - h2 / 2
    targets = [
        (cx - (w + GAP), row1), (cx, row1), (cx + (w + GAP), row1),
        (cx - (w2 + GAP) / 2, row2), (cx + (w2 + GAP) / 2, row2),
    ]
    scales = [scale] * 3 + [s2] * 2
    sizes = [(w, h)] * 3 + [(w2, h2)] * 2
    return targets, scales, sizes, (top, low)


def add_fifth(doc, sc, nulls):
    src_null = nulls[3].obj
    clone = I.clone_node(src_null)
    clone.set('name', Val('stri', 'transition_null_8'))
    I.set_param(clone, 'guid', GUIDS['transition_null_8'])
    child = clone.get('children').items[0]
    child.set('name', Val('stri', 'tile_5'))
    I.set_param(child, 'guid', GUIDS['tile_5'])
    parent_path = nulls[3].path.rsplit('.', 1)[0]
    I.recompute_ids(clone, f'{parent_path}.transition_null_8', [])
    parent = element(sc, parent_path.rsplit('.', 1)[-1])
    parent.obj.get('children').items.append(clone)
    I.relookup(sc.entry)
    return I.Idf(doc.to_bytes())


def build(data, scale=0.65, align='top', bottom='centre'):
    doc = I.Idf(data)
    sc = doc.scenes[0]
    assert sc.name == I.component_id(URI), 'not rnd_overview.idf'
    nulls = [element(sc, f'transition_null_{i}') for i in range(1, 5)]
    has5 = sc.root.find('tile_5') is not None and sc.root.find('transition_null_8') is not None
    if not has5 and (sc.root.find('tile_5') or sc.root.find('transition_null_8')):
        sys.exit('partial 5th tile')
    targets, scales, sizes, (top, low) = layout(nulls, scale, align, bottom)
    doc2 = doc if has5 else add_fifth(doc, sc, nulls)
    sc2 = doc2.scenes[0]
    for i, (t, s) in enumerate(zip(targets, scales), start=1):
        tile = element(sc2, f'tile_{i}')
        null = element(sc2, 'transition_null_8' if i == 5 else f'transition_null_{i}')
        place(tile, null.position, t, s)
    lines = [f'{"relayout" if has5 else "add tile_5"}, scale {scale}, align {align}, bottom {bottom}',
             f'  stock grid y {top:.4f} .. {low:.4f}']
    for i, (t, s, (w, h)) in enumerate(zip(targets, scales, sizes), start=1):
        lines.append(f'  tile_{i} centre ({t[0]:.5f}, {t[1]:.5f}) scale {s:.6f} '
                     f'x {t[0] - w / 2:.5f} .. {t[0] + w / 2:.5f}  y {t[1] + h / 2:.5f} .. {t[1] - h / 2:.5f}')
    return doc2.to_bytes(), lines


def main():
    ap = argparse.ArgumentParser(description='Add tile_5 to rnd_overview.idf and lay the tiles out 3+2.')
    ap.add_argument('src', help='stock (or already 5-tile) art/screens/career/rnd_overview.idf')
    ap.add_argument('out')
    ap.add_argument('--scale', type=float, default=0.65)
    ap.add_argument('--align', choices=['top', 'centre'], default='top')
    ap.add_argument('--bottom', choices=['centre', 'fill'], default='centre')
    a = ap.parse_args()
    with open(a.src, 'rb') as f:
        data, lines = build(f.read(), a.scale, a.align, a.bottom)
    with open(a.out, 'wb') as f:
        f.write(data)
    print(f'wrote {a.out} ({len(data)} bytes): ' + '\n'.join(lines))


if __name__ == '__main__':
    main()
