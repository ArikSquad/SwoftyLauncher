"""Offline validation of the 5-sector pie build.

1. Scenes: re-parse, byte round-trip, elementId == djb2(dotted path), nodeidlookup ==
   sorted unique djb2(leaf names), componentId == md5-64(path), geom asset ==
   md5-64(resourceid), every fileRef / resourceid / materialid / anim asset resolves in
   the stock archive or the new resource set.
2. Meshes: parse each new GfxMeshRes like the viewer does, follow its vb/ib names to the
   new buffers, rasterise the two triangles in screen space, sample the bound mask
   texture through the UVs and measure the covered wedge (angle span, radius).
3. .ui: uicheck (ids, nameIds, layout nodeIds, event/command/signal hashes, targets),
   plus the tyres wiring.
4. Archives: apply every op of erp/rnd_fifth_pie.json to copies of both stock
   archives with erpw (same semantics as the launcher: replace keeps other fragments,
   add takes type arg/flags/compression from the first same-type resource with the same
   fragment count), re-read them with erp.py and check every touched resource
   decompresses to the shipped bytes and every other resource is byte-identical. The
   two rebuilt archives are written to --out.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys

import common
import assets as A
import erpw
import idf as I
import uicheck
from erp import parse as erp_parse

ADDON = common.ADDON
UI_DIR = None
OUT = os.path.join(common.BUILD, 'ui_package')
problems = []
notes = []
STOCK_MISSING = {
    'eaid://ui_package/art/components/career/rnd_tree/rnd_dev_label_alt.emb?context=default',
    'eaid://ui_package/art/components/career/rnd_tree/rnd_perf_stack.emb?context=default',
    'eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential_full.emb?context=default',
}


def ok(cond, msg):
    if not cond:
        problems.append(msg)
    return cond


def md5be64(s):
    return int.from_bytes(hashlib.md5(s.encode()).digest()[:8], 'big')


def load_ops():
    return json.load(open(os.path.join(ADDON, 'erp', 'rnd_fifth_pie.json')))['ops']


def shipped():
    res = {}
    for op in load_ops():
        if op['op'] == 'replace-resource':
            res[op['resource']] = ('replace', [open(os.path.join(ADDON, op['source']), 'rb').read()])
        elif op['op'] == 'add-resource':
            res[op['resource']] = (op['type'], [open(os.path.join(ADDON, s), 'rb').read() for s in op['sources']])
    return res


def check_scene(name, blob, known):
    doc = I.Idf(blob)
    ok(doc.to_bytes() == blob, f'{name}: round-trip')
    sc = doc.scenes[0]
    ok(sc.name == I.component_id(name), f'{name}: componentId')
    names = set()
    n = 0
    for e, _d in sc.root.walk():
        n += 1
        names.add(I.djb2(e.name))
        if e is not sc.root:
            ok(e.id_ok(), f'{name}: elementId {e.path}')
        for g in e.geoms:
            ok(g.get('asset') == md5be64(g['resourceid']), f'{name}: geom asset {e.path}')
            ok(g['resourceid'] + '&lod=0&mesh' in known, f'{name}: mesh for {g["resourceid"]}')
            ok(g['materialid'] in known, f'{name}: material {g["materialid"]}')
        for an in e.anims:
            ok(an['asset'] in known, f'{name}: anim {an["asset"]}')
        if e.type == 'eXRef' and 'fileRef' in e.params:
            xr = I._val(e.obj.get('xref'))
            ok(xr == I.component_id(e.params['fileRef']),
               f'{name}: xref id {xr} of {e.path} != md5-64({e.params["fileRef"]}) (the engine resolves the xref id)')
        for p in ('fileRef', 'XrefMaterialBinding'):
            if p in e.params and e.params[p] not in known:
                if e.params[p] in STOCK_MISSING:
                    notes.append(f'{name}: {p} {e.params[p]} is missing in stock too (unchanged)')
                else:
                    ok(False, f'{name}: {p} {e.params[p]}')
    ok(sc.nodeidlookup == sorted(names), f'{name}: nodeidlookup')
    return doc, n


def raster_wedge(verts, uvs, idx, tex, tw, th, size=400):
    thr = max(tex[3::4]) // 2
    pts = [(v[0], -v[2]) for v in verts]
    cov = 0
    angles = []
    radii = []
    lo, hi = -0.1, 1.25
    for py in range(size):
        for px in range(size):
            x = lo + (px + 0.5) / size * (hi - lo)
            y = lo + (py + 0.5) / size * (hi - lo)
            for t in range(0, len(idx), 3):
                a, b, c = (pts[idx[t]], pts[idx[t + 1]], pts[idx[t + 2]])
                d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
                w1 = ((b[0] - x) * (c[1] - y) - (c[0] - x) * (b[1] - y)) / d
                w2 = ((c[0] - x) * (a[1] - y) - (a[0] - x) * (c[1] - y)) / d
                w3 = 1 - w1 - w2
                if min(w1, w2, w3) < -1e-9:
                    continue
                ua, ub, uc = uvs[idx[t]], uvs[idx[t + 1]], uvs[idx[t + 2]]
                u = w1 * ua[0] + w2 * ub[0] + w3 * uc[0]
                v = w1 * ua[1] + w2 * ub[1] + w3 * uc[1]
                tx = min(tw - 1, max(0, int(u * tw)))
                ty = min(th - 1, max(0, int(v * th)))
                if tex[(ty * tw + tx) * 4 + 3] > thr:
                    cov += 1
                    angles.append(math.degrees(math.atan2(y, x)))
                    radii.append(math.hypot(x, y))
                break
    return cov, (min(angles), max(angles)) if angles else None, max(radii) if radii else None


def check_meshes(res, stock):
    out = []
    slots = {'comp_potential': 'rnd_comp_potential', 'current_nub': 'rnd_dev_current', 'potential_nub': 'rnd_dev_potential'}
    for e in common.MASKS:
        base = f'eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential_sect72.idf/{e}?context=render'
        mesh = A.parse_mesh(res[base + '&lod=0&mesh'][1][0])
        ok(mesh['slot'] == slots[e], f'{e}: slot {mesh["slot"]}')
        ok(all(v.startswith(base) for v in mesh['vbs'] + [mesh['ib']]), f'{e}: buffer names {mesh}')
        bufs = [A.parse_buffer(res[v][1][0], res[v][1][1]) for v in mesh['vbs']]
        stride, count, data = bufs[0]
        verts = [struct.unpack_from('<3f', data, i * stride) for i in range(count)]
        uvs = [struct.unpack_from('<2f', bufs[2][2], i * 8) for i in range(count)]
        ihdr, idata = res[mesh['ib']][1]
        idx = list(struct.unpack('<%dH' % (len(idata) // 2), idata))
        tw, th, tex = stock.texture(common.MASKS[e])
        cov, span, rmax = raster_wedge(verts, uvs, idx, tex, tw, th)
        stock_cov, stock_span, stock_r = raster_wedge(
            [(0.0, 0, -1.12), (0.0, 0, 0.0), (1.12, 0, -1.12), (1.12, 0, 0.0)],
            [(0, 0), (0, 1), (1, 0), (1, 1)], idx, tex, tw, th)
        ratio = cov / stock_cov if stock_cov else 0
        out.append(f'{e}: verts {[[round(x, 4) for x in v] for v in verts]} uv {[[round(x, 4) for x in u] for u in uvs]} '
                   f'idx {idx}; mask texels {cov} vs stock 90deg {stock_cov} (ratio {ratio:.3f}, ideal 0.800), '
                   f'span {tuple(round(a, 2) for a in span)} deg (stock {tuple(round(a, 2) for a in stock_span)}), '
                   f'r_max {rmax:.3f} (stock {stock_r:.3f})')
        if e != 'potential_nub':
            ok(abs(ratio - 0.8) < 0.02, f'{e}: area ratio {ratio}')
        ok(span[0] > -0.5 and abs(span[1] - 72.0) < 0.6, f'{e}: span {span}')
        ok(abs(rmax - stock_r) < 0.01, f'{e}: radius {rmax} vs {stock_r}')
    return out


def check_ui(blob):
    n, s, bad, _outside = uicheck.check(blob)
    for b in bad:
        problems.append('ui: ' + b)
    return n, s


def apply_archives(res):
    ops = load_ops()
    os.makedirs(OUT, exist_ok=True)
    report = []
    for rel in common.ARCHIVES:
        src = os.path.join(UI_DIR, rel)
        e = erpw.Erp(src)
        before = {r.name: r for r in e.res}
        stock_raw = {r.name: [fr.raw for fr in r.frags] for r in e.res}
        erp_key = '2025_asset_groups/ui_package/' + rel
        touched = set()
        for op in ops:
            if op.get('erp') != erp_key:
                continue
            if op['op'] == 'replace-resource':
                r = e.find(op['resource'])
                e.set_frag(r, op.get('fragment', 0), open(os.path.join(ADDON, op['source']), 'rb').read())
            else:
                ok(op['resource'] not in before, f'{rel}: {op["resource"]} already exists')
                srcs = [open(os.path.join(ADDON, s), 'rb').read() for s in op['sources']]
                tmpl = next(r for r in e.res if r.type.rstrip(b'\0').decode() == op['type'] and len(r.frags) == len(srcs))
                r = erpw.Res()
                r.name, r.type, r.unk, r.frags, r.hash = op['resource'], tmpl.type, tmpl.unk, [], b''
                for i, data in enumerate(srcs):
                    fr = erpw.Frag()
                    t = tmpl.frags[i]
                    fr.name, fr.flags, fr.comp, fr.off = t.name, t.flags, t.comp, 0
                    fr.raw = erpw.zstd_c(data) if t.comp == erpw.ZSTD else data
                    fr.size, fr.packed = len(data), len(fr.raw)
                    r.frags.append(fr)
                r.hash = hashlib.md5(b''.join(srcs)).digest()
                e.res.append(r)
            touched.add(op['resource'])
        out = os.path.join(OUT, rel)
        e.to_file(out)
        meta, parsed = erp_parse(out)
        chk = erpw.Erp(out)
        same = 0
        for r in chk.res:
            if r.name in touched:
                want = res[r.name][1]
                got = [chk.frag_data(r, i) for i in range(len(r.frags))]
                if res[r.name][0] == 'replace':
                    ok(got[0] == want[0], f'{rel}: {r.name} replaced bytes')
                else:
                    ok(got == want, f'{rel}: {r.name} added bytes')
                    ok(r.hash == hashlib.md5(b''.join(want)).digest(), f'{rel}: {r.name} md5')
            else:
                ok([fr.raw for fr in r.frags] == stock_raw[r.name], f'{rel}: {r.name} changed')
                same += 1
        report.append(f'{rel}: {len(chk.res)} resources ({len(chk.res) - len(before)} added, {len(touched)} touched), '
                      f'{same} untouched byte-identical, size {os.path.getsize(src)} -> {os.path.getsize(out)}')
    return report


def main():
    global ADDON, UI_DIR, OUT
    ap = argparse.ArgumentParser(description='Validate the shipped five-sector pie assets offline.')
    common.add_game_arg(ap)
    ap.add_argument('--addon', default=common.ADDON, help='addon folder to check (default: this addon)')
    ap.add_argument('--out', default=OUT, help='where the two rebuilt archives go (default: build/ui_package)')
    a = ap.parse_args()
    ADDON, UI_DIR, OUT = a.addon, common.ui_package(a.game), a.out
    if common.gamedir.is_inside(OUT, common.gamedir.game_root_of(UI_DIR)):
        sys.exit('--out must be outside the game folder')
    res = shipped()
    stock_names = set()
    for rel in common.ARCHIVES + [common.PERSISTENT]:
        _m, rr = erp_parse(os.path.join(UI_DIR, rel))
        stock_names |= {r[0] for r in rr}
    known = stock_names | set(res)
    lines = []
    for name in ('eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential5.idf',
                 'eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential_sect72.idf',
                 'eaid://ui_package/art/components/career/rnd_tree/rnd_dev_potential_full.idf',
                 'eaid://ui_package/art/screens/career/rnd_overview.idf'):
        _doc, n = check_scene(name, res[name][1][0], known)
        lines.append(f'scene {name.rsplit("/", 1)[-1]}: {n} elements ok')
    lines += check_meshes(res, common.Archive(os.path.join(UI_DIR, common.CAREER)))
    n, s = check_ui(res['eaid://ui_package/screens/laptop_r_and_d.ui'][1][0])
    lines.append(f'ui: {n} nodes, {s} signals checked')
    lines += apply_archives(res)
    print('\n'.join(lines))
    for n_ in sorted(set(notes)):
        print('note:', n_)
    print(f'{len(problems)} problem(s)')
    for p in problems:
        print('  ', p)
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
