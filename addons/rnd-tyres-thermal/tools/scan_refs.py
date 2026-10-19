"""List the UI resources (scenes, .ui, material bindings, materials, SRVs) whose bytes name
a resource, across every UI archive. Used to find who else references a stock resource
before replacing it (rnd_dev_potential.idf is also used by the Team HQ research overview).
"""
import argparse

import common
from erp import parse as erp_parse
from erpsource import ErpCatalog

KINDS = ('UILayout', 'UIDF', 'GfxMatBindRes', 'GfxMaterialRes', 'GfxSRVResource')


def main():
    ap = argparse.ArgumentParser(description='Find the UI resources that reference each needle.')
    common.add_game_arg(ap)
    ap.add_argument('needles', nargs='+', help='byte strings to look for, e.g. rnd_dev_potential.idf')
    a = ap.parse_args()
    needles = [n.encode() for n in a.needles]
    cat = ErpCatalog(common.ui_package(a.game)).build()
    hits = {}
    for rel in cat.erps:
        try:
            _meta, res = erp_parse(cat.erp_path(rel))
        except Exception:
            continue
        for name, kind, _unk, _frags, _h in res:
            if kind not in KINDS:
                continue
            try:
                data = cat.read_fragment(rel, name, 0)
            except Exception:
                continue
            for n in needles:
                if n in data and n.decode() not in name:
                    hits.setdefault(n.decode(), set()).add((kind, name, rel))
    for n, s in hits.items():
        print('==', n)
        for k, name, rel in sorted(s):
            print('  ', k, name, rel)


if __name__ == '__main__':
    main()
