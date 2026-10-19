"""Rebuild every shipped UI asset of the addon from the stock game archives.

Into --work (default build/):
1. the stock screens/laptop_r_and_d.ui and art/screens/career/rnd_overview.idf;
2. make_laptop_r_and_d5: laptop_r_and_d.5tiles.ui;
3. make_rnd_overview5: rnd_overview.5tiles.idf, then --bottom fill: rnd_overview.5tiles.fill.idf;
4. make_bg5: tex/graph_b5.bc7;
5. build_pie: addon/ui/ and addon/erp/rnd_fifth_pie.json.

Then it compares addon/ with the shipped ui/ and erp/rnd_fifth_pie.json and exits 1 on any
difference. --write replaces the shipped files with the build instead.
"""
import argparse
import os
import shutil
import sys

import build_pie
import common
import make_bg5
import make_laptop_r_and_d5
import make_rnd_overview5

SHIPPED_JSON = os.path.join('erp', 'rnd_fifth_pie.json')


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def generated(root):
    files = {SHIPPED_JSON} if os.path.isfile(os.path.join(root, SHIPPED_JSON)) else set()
    for d, _dirs, names in os.walk(os.path.join(root, 'ui')):
        files |= {os.path.relpath(os.path.join(d, n), root) for n in names}
    return files


def compare(built, shipped):
    a, b = generated(built), generated(shipped)
    same, differ = [], []
    for rel in sorted(a & b):
        with open(os.path.join(built, rel), 'rb') as x, open(os.path.join(shipped, rel), 'rb') as y:
            (same if x.read() == y.read() else differ).append(rel)
    return same, differ, sorted(a - b), sorted(b - a)


def write(built, shipped):
    shutil.rmtree(os.path.join(shipped, 'ui'), ignore_errors=True)
    shutil.copytree(os.path.join(built, 'ui'), os.path.join(shipped, 'ui'))
    shutil.copyfile(os.path.join(built, SHIPPED_JSON), os.path.join(shipped, SHIPPED_JSON))


def main():
    ap = argparse.ArgumentParser(description='Rebuild the addon\'s ui/ and erp/rnd_fifth_pie.json from the stock game.')
    common.add_game_arg(ap)
    ap.add_argument('--work', default=common.BUILD, help='work folder (default: build)')
    ap.add_argument('--write', action='store_true', help='replace the shipped ui/ and erp/rnd_fifth_pie.json with the build')
    a = ap.parse_args()
    work = os.path.abspath(a.work)

    career = common.stock(a.game)
    print(f'stock: {career.path}')
    stock_ui = career.read(common.UI_SCREEN)
    stock_overview = career.read(common.OVERVIEW)
    put(os.path.join(work, 'laptop_r_and_d.stock.ui'), stock_ui)
    put(os.path.join(work, 'rnd_overview.stock.idf'), stock_overview)

    ui5 = make_laptop_r_and_d5.build(stock_ui)
    put(os.path.join(work, 'laptop_r_and_d.5tiles.ui'), ui5)
    print(f'laptop_r_and_d.5tiles.ui: {len(ui5)} bytes')

    overview5, _ = make_rnd_overview5.build(stock_overview)
    put(os.path.join(work, 'rnd_overview.5tiles.idf'), overview5)
    fill, lines = make_rnd_overview5.build(overview5, bottom='fill')
    put(os.path.join(work, 'rnd_overview.5tiles.fill.idf'), fill)
    print(f'rnd_overview.5tiles.fill.idf: {len(fill)} bytes, ' + '\n'.join(lines))

    bc7, mx, mean = make_bg5.build(make_bg5.stock_graph_bg(a.game), os.path.join(work, 'tex'))
    print(f'graph_b5: {len(bc7)} bytes BC7, round-trip max err {mx}, mean {mean:.3f}')

    built = os.path.join(work, 'addon')
    out, kinds = build_pie.build(a.game, ui5, fill, bc7, built)
    print(f'{built}: {len(out.files)} files, ops {kinds}')

    if a.write:
        write(built, common.ADDON)
        print(f'wrote {len(generated(built))} files into {common.ADDON}')
        return
    same, differ, extra, missing = compare(built, common.ADDON)
    print(f'{len(same)} files identical to the shipped ones')
    for label, rels in (('differs', differ), ('only in the build', extra), ('only in the addon', missing)):
        for rel in rels:
            print(f'  {label}: {rel}')
    sys.exit(1 if differ or extra or missing else 0)


if __name__ == '__main__':
    main()
