"""Build the 5-sector development-potential pie for R&D > Performance.

Inputs (read-only): the stock flow_career_hub.erp, the 5-tile screens from
make_laptop_r_and_d5.py and make_rnd_overview5.py (laptop_r_and_d.5tiles.ui,
rnd_overview.5tiles.fill.idf) and tex/graph_b5.bc7 from make_bg5.py, all in --work.
Output: --out/ui/ (scenes, the .ui, raw fragments) and --out/erp/rnd_fifth_pie.json.

Isolation: the Performance page reaches the pie through
rnd_overview.idf -> rnd_dev_potential_full.idf (career, only used there) -> chart_1.
team_hq/research/dev_potential_full.idf also references the stock rnd_dev_potential.idf,
so the stock pie, sector scene, meshes and textures are left untouched. chart_1 is
re-pointed at a new pie scene (rnd_dev_potential5.idf) whose five sectors reference a new
sector scene (rnd_dev_potential_sect72.idf) with 72-degree wedge meshes. New render
resources keep the stock string lengths (sector -> sect72, graph_bg -> graph_b5), so
mesh/model/SRV/material bytes are the stock bytes with in-place string swaps.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import uuid

import common
import assets as A
import idf as I
import pkg as P
from pkg import Arr, Obj, Val
from uitree import is_node

ARCHIVES = ['2025_asset_groups/ui_package/flow_career_hub.erp', '2025_asset_groups/ui_package/flow_team_hq.erp']

PKG = 'eaid://ui_package/'
RT = PKG + 'art/components/career/rnd_tree/'
SECTOR_OLD = RT + 'rnd_dev_potential_sector.idf'
SECTOR_NEW = RT + 'rnd_dev_potential_sect72.idf'
PIE_OLD = RT + 'rnd_dev_potential.idf'
PIE_NEW = RT + 'rnd_dev_potential5.idf'
FULL = RT + 'rnd_dev_potential_full.idf'
EMB_OLD = RT + 'rnd_dev_potential.emb?context=default'
EMB_NEW = RT + 'rnd_dev_potential5.emb?context=default'
MAT_OLD = PKG + 'art/materials/team_hq/research/dev_potential_graph_bg.material'
MAT_NEW = PKG + 'art/materials/career/rnd_tree/rnd_dev_potential5_bg.material'
TEX_OLD = PKG + 'art/textures/team_hq/rnd/dev_potential/graph_bg.tif'
TEX_NEW = PKG + 'art/textures/team_hq/rnd/dev_potential/graph_b5.tif'
OVERVIEW = PKG + 'art/screens/career/rnd_overview.idf'
UI = PKG + 'screens/laptop_r_and_d.ui'

ELEMS = ['comp_potential', 'current_nub', 'potential_nub']
R = 1.12
WEDGE_DEG = 72.0
GAP = 0.08
OFFSET = (GAP / 2) / math.sin(math.radians(WEDGE_DEG / 2))

TAB = 'tab_content_rnd_upgrades_alt'
DEV = f'{TAB}.dev_potential'
FOCUS = f'{TAB}.focusable'
DEPT = 'tyres'

SECTORS = [
    ('sector_1', 'chassis', 90.0 - 1 * WEDGE_DEG),
    ('sector_2', 'maintenance', 90.0 - 2 * WEDGE_DEG),
    ('sector_3', 'engine', 90.0 - 3 * WEDGE_DEG),
    ('sector_4', 'aero', 90.0 - 4 * WEDGE_DEG),
    ('sector_5', DEPT, 90.0 - 5 * WEDGE_DEG),
]

assert len(SECTOR_NEW) == len(SECTOR_OLD) and len(TEX_NEW) == len(TEX_OLD)


_stock = None


def stock(name, frag=0):
    return _stock.read(name, frag)


def md5be64(s):
    return int.from_bytes(hashlib.md5(s.encode()).digest()[:8], 'big')


def swap(data, old, new, count=None):
    o, n = old.encode(), new.encode()
    assert len(o) == len(n), (old, new)
    k = data.count(o)
    assert k and (count is None or k == count), (old, k)
    return data.replace(o, n)


class Out:
    def __init__(self):
        self.files = {}
        self.ops = []

    def put(self, rel, data):
        self.files[rel] = data
        return rel

    def replace(self, resource, rel, data):
        self.put(rel, data)
        for erp in ARCHIVES:
            self.ops.append({'op': 'replace-resource', 'erp': erp, 'resource': resource, 'source': rel})

    def add(self, resource, kind, frags):
        rels = [self.put(rel, data) for rel, data in frags]
        for erp in ARCHIVES:
            self.ops.append({'op': 'add-resource', 'erp': erp, 'resource': resource, 'type': kind, 'sources': rels})

    def write(self, out_dir):
        ui = os.path.join(out_dir, 'ui')
        if os.path.isdir(ui):
            shutil.rmtree(ui)
        for rel, data in self.files.items():
            p = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(data)


def wedge_buffers(elem):
    base = f'{SECTOR_OLD}/{elem}?context=render&lod=0&'
    vb0 = bytearray(stock(base + 'vb=0', 1))
    vb2 = bytearray(stock(base + 'vb=2', 1))
    x0, y0, z0 = struct.unpack_from('<3f', vb0, 0)
    u0, v0 = struct.unpack_from('<2f', vb2, 0)
    assert abs(x0) < 1e-6 and abs(z0 + R) < 1e-5 and (u0, v0) == (0.0, 0.0), (elem, x0, z0, u0, v0)
    cot = 1.0 / math.tan(math.radians(WEDGE_DEG))
    struct.pack_into('<3f', vb0, 0, R * cot, y0, z0)
    struct.pack_into('<2f', vb2, 0, cot, 0.0)
    return bytes(vb0), bytes(vb2)


def build_render(out):
    for e in ELEMS:
        old = f'{SECTOR_OLD}/{e}?context=render'
        new = f'{SECTOR_NEW}/{e}?context=render'
        d = f'ui/pie/sect72/{e}'
        out.add(new + '?render', 'ModelProxyRes', [(f'{d}.proxy.f0', stock(old + '?render'))])
        out.add(new + '?model', 'GfxModelRes', [(f'{d}.model.f0', swap(stock(old + '?model'), SECTOR_OLD, SECTOR_NEW, 1))])
        out.add(new + '&lod=0&mesh', 'GfxMeshRes', [(f'{d}.mesh.f0', swap(stock(old + '&lod=0&mesh'), SECTOR_OLD, SECTOR_NEW, 4))])
        vb0, vb2 = wedge_buffers(e)
        data = {'vb=0': vb0, 'vb=1': stock(old + '&lod=0&vb=1', 1), 'vb=2': vb2, 'ib=0': stock(old + '&lod=0&ib=0', 1)}
        for key, payload in data.items():
            tag = key.replace('=', '')
            out.add(f'{new}&lod=0&{key}', 'GfxBufferRes',
                    [(f'{d}.{tag}.f0', stock(f'{old}&lod=0&{key}', 0)), (f'{d}.{tag}.f1', payload)])


def find_elem(obj, name):
    if isinstance(obj, Obj):
        if isinstance(obj.get('name'), Val) and obj.get('name').value == name and obj.get('elementId') is not None:
            return obj
        for _k, v in obj.items:
            r = find_elem(v, name)
            if r is not None:
                return r
    elif isinstance(obj, Arr):
        for v in obj.items:
            r = find_elem(v, name)
            if r is not None:
                return r
    return None


def set_xref(elem, path):
    old = elem.get('xref')
    assert old is not None, elem.get('name').value
    elem.set('xref', Val(old.type, I.component_id(path)))


def set_param_keep(elem, name, value):
    for it in elem.get('params').items:
        if it.get('name').value == name:
            it.set('value', Val(it.get('value').type, value))
            return
    raise KeyError(name)


def scene_entry(doc):
    return doc.pkg.root.get('scenelist').items[0]


def build_sector_scene(out):
    data = stock(SECTOR_OLD)
    doc = I.Idf(data)
    assert doc.to_bytes() == data
    ent = scene_entry(doc)
    ent.set('name', Val('ui64', I.component_id(SECTOR_NEW)))
    n = 0
    for e in ELEMS:
        el = find_elem(ent, e)
        for g in el.get('geoms').items:
            rid = g.get('resourceid').value
            assert rid == f'{SECTOR_OLD}/{e}?context=render'
            nid = rid.replace(SECTOR_OLD, SECTOR_NEW)
            g.set('resourceid', Val('stri', nid))
            g.set('asset', Val('ui64', md5be64(nid)))
            n += 1
    assert n == 3
    blob = doc.to_bytes()
    out.add(SECTOR_NEW, 'UILayout', [('ui/pie/rnd_dev_potential_sect72.idf', blob)])
    return blob


def quat_z(deg):
    h = math.radians(deg) / 2
    q = [0.0, 0.0, math.sin(h), math.cos(h)]
    if q[3] > 0:
        q = [-x for x in q]
    return [round(x, 6) + 0.0 for x in q]


def build_pie_scene(out):
    data = stock(PIE_OLD)
    doc = I.Idf(data)
    assert doc.to_bytes() == data
    ent = scene_entry(doc)
    ent.set('name', Val('ui64', I.component_id(PIE_NEW)))
    null = find_elem(ent, 'null_1')
    kids = null.get('children').items
    src = find_elem(ent, 'sector_4')
    s5 = I.clone_node(src)
    s5.set('name', Val('stri', 'sector_5'))
    I.set_param(s5, 'guid', str(uuid.uuid5(uuid.NAMESPACE_URL, PIE_NEW + '#sector_5')))
    kids.insert(0, s5)
    for name, _dept, start in SECTORS:
        el = find_elem(ent, name)
        set_param_keep(el, 'fileRef', SECTOR_NEW)
        set_xref(el, SECTOR_NEW)
        mid = math.radians(start + WEDGE_DEG / 2)
        pos = el.get('position')
        el.set('position', Val(pos.type, [round(OFFSET * math.cos(mid), 6) + 0.0, round(OFFSET * math.sin(mid), 6) + 0.0, -0.0, 0.0]))
        rot = el.get('rotation')
        el.set('rotation', Val(rot.type, quat_z(start)))
    ids = []
    I.recompute_ids(s5, 'null_1.sector_5', ids)
    bg = find_elem(ent, 'bg')
    for g in bg.get('geoms').items:
        assert g.get('materialid').value == EMB_OLD
        g.set('materialid', Val('stri', EMB_NEW))
    I.relookup(ent)
    blob = doc.to_bytes()
    out.add(PIE_NEW, 'UILayout', [('ui/pie/rnd_dev_potential5.idf', blob)])
    return blob


def build_full_scene(out):
    data = stock(FULL)
    doc = I.Idf(data)
    assert doc.to_bytes() == data
    chart = find_elem(scene_entry(doc), 'chart_1')
    set_param_keep(chart, 'fileRef', PIE_NEW)
    set_xref(chart, PIE_NEW)
    set_param_keep(chart, 'XrefMaterialBinding', EMB_NEW)
    blob = doc.to_bytes()
    out.replace(FULL, 'ui/rnd_dev_potential_full.idf', blob)
    return blob


def matbind(slots):
    names = b''.join(s.encode() + b'\0' for s, _m in slots)
    mats = []
    for _s, m in slots:
        if m not in mats:
            mats.append(m)
    mat_blob = b''.join(m.encode() + b'\0' for m in mats)
    table = b''
    noff = 0
    for s, m in slots:
        moff = sum(len(x.encode()) + 1 for x in mats[:mats.index(m)])
        table += struct.pack('<II', noff, moff)
        noff += len(s.encode()) + 1
    return struct.pack('<III', len(slots), len(mats), 8 * len(slots) + len(names)) + table + names + mat_blob


def build_bg(out, px):
    stock_emb = stock(EMB_OLD)
    assert A.parse_matbind(stock_emb) == {'graph_bg': MAT_OLD}
    assert matbind([('graph_bg', MAT_OLD)]) == stock_emb
    emb = matbind([('graph_bg', MAT_NEW)])
    assert A.parse_matbind(emb) == {'graph_bg': MAT_NEW}
    out.add(EMB_NEW, 'GfxMatBindRes', [('ui/pie/bg/rnd_dev_potential5.emb.f0', emb)])
    mat = swap(stock(MAT_OLD), TEX_OLD, TEX_NEW, 2)
    out.add(MAT_NEW, 'GfxMaterialRes', [('ui/pie/bg/rnd_dev_potential5_bg.material.f0', mat)])
    srv = swap(stock(TEX_OLD), TEX_OLD + '.image', TEX_NEW + '.image', 1)
    out.add(TEX_NEW, 'GfxSRVResource', [('ui/pie/bg/graph_b5.tif.f0', srv)])
    hdr = stock(TEX_OLD + '.image', 0)
    assert len(px) == len(stock(TEX_OLD + '.image', 1))
    out.add(TEX_NEW + '.image', 'GfxSurfaceRes', [('ui/pie/bg/graph_b5.tif.image.f0', hdr), ('ui/pie/bg/graph_b5.tif.image.f1', px)])


def ui_find(x, path, want):
    if is_node(x):
        path = path + [x.items[0]['name'].value]
        if '.'.join(path) == want:
            return x
        for c in x.items[1].items if isinstance(x.items[1], Arr) else []:
            r = ui_find(c, path, want)
            if r is not None:
                return r
        return None
    items = x.items if isinstance(x, (Arr, Obj)) else []
    for it in items:
        r = ui_find(it[1] if isinstance(x, Obj) else it, path, want)
        if r is not None:
            return r
    return None


def ui_clone(n):
    if isinstance(n, Obj):
        return Obj([(ui_clone(k), ui_clone(v)) for k, v in n.items], n.version)
    if isinstance(n, Arr):
        return Arr([ui_clone(v) for v in n.items], n.tag)
    if isinstance(n, Val):
        return Val(n.type, n.value, None)
    return n


def ui_rewrite(x, old, new):
    if isinstance(x, Obj):
        for i, (k, v) in enumerate(x.items):
            if isinstance(v, Val) and v.type == 'stri' and isinstance(v.value, str):
                if k.value == 'to_object_name' and (v.value == old or v.value.startswith(old + '.')):
                    path = new + v.value[len(old):]
                    x.items[i] = (k, Val('stri', path))
                    x.set('to_object', Val('ui32', I.djb2(path)))
            else:
                ui_rewrite(v, old, new)
    elif isinstance(x, Arr):
        for v in x.items:
            ui_rewrite(v, old, new)


def ui_fix_ids(node, path):
    node.items[0].set('id', Val('ui32', I.djb2(path)))
    for c in node.items[1].items if isinstance(node.items[1], Arr) else []:
        if is_node(c):
            ui_fix_ids(c, f"{path}.{c.items[0]['name'].value}")


def signal_like(template, to_name, pin, from_pin):
    s = ui_clone(template)
    s.set('to_object', Val('ui32', I.djb2(to_name)))
    s.set('to_object_name', Val('stri', to_name))
    s.set('to_pin_name', Val('stri', pin))
    s.set('to_pin', Val('ui32', I.djb2(pin)))
    s.set('from_pin_name', Val('stri', from_pin))
    return s


def build_ui(out, data):
    doc = P.Pkg(data)
    assert doc.to_bytes() == data

    tile = ui_find(doc.root, [], f'{FOCUS}.tyres')
    assert tile is not None and tile.items[0].get('dataContext').get('dbentry').value == 'fake'
    tile.items[0].get('dataContext').set('dbentry', Val('stri', DEPT))
    swapped = 0
    for ev in tile.items[3].items:
        for sig in ev.get('signals').items:
            attrs = sig.get('attributes')
            cmd = attrs.get('command') if attrs is not None else None
            if cmd is not None and cmd.get('data') is not None and cmd.get('data').get('value').value == 'fake':
                cmd.get('data').set('value', Val('stri', DEPT))
                swapped += 1
    assert swapped == 1, swapped

    group = ui_find(doc.root, [], FOCUS)
    trig = [c for c in group.items[2].items if c.get('commandName').value == 'trigger']
    assert len(trig) == 1
    tsigs = trig[0].get('signals').items
    assert not any(t.get('to_object_name').value == f'{FOCUS}.{DEPT}' for t in tsigs)
    src = [t for t in tsigs if t.get('to_object_name').value == f'{FOCUS}.engine']
    assert len(src) == 1 and src[0].get('to_pin_name').value == 'Trigger'
    at = tsigs.index(src[0]) + 1
    tsigs.insert(at, signal_like(src[0], f'{FOCUS}.{DEPT}', 'Trigger', 'trigger'))

    focus = ui_find(doc.root, [], f'{FOCUS}.focused_department')
    fake = [e for e in focus.items[3].items if e.get('eventName').value == 'fake']
    assert len(fake) == 1
    fake[0].set('eventName', Val('stri', DEPT))
    fake[0].set('event', Val('ui32', I.djb2(DEPT)))
    for sig in fake[0].get('signals').items:
        sig.set('from_pin_name', Val('stri', DEPT))

    dev = ui_find(doc.root, [], DEV)
    aero = ui_find(doc.root, [], f'{DEV}.aero')
    assert ui_find(doc.root, [], f'{DEV}.{DEPT}') is None
    grp = ui_clone(aero)
    props = grp.items[0]
    props.set('name', Val('stri', DEPT))
    props.set('nameId', Val('ui32', I.djb2(DEPT)))
    props.get('dataContext').set('dbentry', Val('stri', f'focused_team_perf_root.department_performance.{DEPT}'))
    layout = grp.items[4].items[0].items
    assert [o.get('debugName').value for o in layout] == ['chart_1', 'sector_4']
    layout[1].set('debugName', Val('stri', 'sector_5'))
    layout[1].set('nodeId', Val('ui32', I.djb2('sector_5')))
    ui_rewrite(grp, f'{DEV}.aero', f'{DEV}.{DEPT}')
    ui_fix_ids(grp, f'{DEV}.{DEPT}')
    kids = dev.items[1].items
    at = next(i for i, c in enumerate(kids) if is_node(c) and c.items[0]['name'].value == 'highlight_department')
    kids.insert(at, grp)

    trig = [c for c in dev.items[2].items if c.get('commandName').value == 'trigger']
    assert len(trig) == 1
    sigs = trig[0].get('signals').items
    sigs.append(signal_like(sigs[0], f'{DEV}.{DEPT}', 'trigger', 'trigger'))

    hl = ui_find(doc.root, [], f'{DEV}.highlight_department')
    events = hl.items[3].items
    names = [e.get('eventName').value for e in events]
    assert names == ['default', 'chassis', 'engine', 'aero'], names
    for ev in events:
        s0 = ev.get('signals').items[0]
        ev.get('signals').items.append(signal_like(s0, f'{DEV}.{DEPT}', 'unhighlight', ev.get('eventName').value))
    tev = ui_clone(events[1])
    tev.set('eventName', Val('stri', DEPT))
    tev.set('event', Val('ui32', I.djb2(DEPT)))
    s0 = tev.get('signals').items[0]
    tev.get('signals').items[:] = [signal_like(s0, f'{DEV}.{DEPT}', 'highlight', DEPT)] + [
        signal_like(s0, f'{DEV}.{d}', 'unhighlight', DEPT) for d in ('aero', 'engine', 'chassis', 'maintenance')]
    events.append(tev)

    blob = doc.to_bytes()
    chk = P.Pkg(blob)
    assert chk.to_bytes() == blob
    out.replace(UI, 'ui/laptop_r_and_d.ui', blob)
    return blob


def build_overview(out, blob):
    out.replace(OVERVIEW, 'ui/rnd_overview.idf', blob)


def build(game, ui5, overview5, graph_b5, out_dir):
    global _stock
    _stock = common.stock(game)
    out = Out()
    build_overview(out, overview5)
    build_ui(out, ui5)
    build_full_scene(out)
    build_pie_scene(out)
    build_sector_scene(out)
    build_render(out)
    build_bg(out, graph_b5)
    doc = {'schema_version': 1, 'ops': out.ops}
    out.files['erp/rnd_fifth_pie.json'] = (json.dumps(doc, indent=2) + '\n').encode()
    out.write(out_dir)
    kinds = {}
    for op in out.ops:
        kinds[op['op']] = kinds.get(op['op'], 0) + 1
    return out, kinds


def main():
    ap = argparse.ArgumentParser(description='Build the five-sector pie ui/ files and erp/rnd_fifth_pie.json.')
    common.add_game_arg(ap)
    ap.add_argument('--work', default=common.BUILD,
                    help='folder with laptop_r_and_d.5tiles.ui, rnd_overview.5tiles.fill.idf and tex/graph_b5.bc7 (default: build)')
    ap.add_argument('--out', help='folder that gets ui/ and erp/rnd_fifth_pie.json (default: <work>/addon)')
    a = ap.parse_args()
    out_dir = a.out or os.path.join(a.work, 'addon')

    def read(*rel):
        with open(os.path.join(a.work, *rel), 'rb') as f:
            return f.read()

    out, kinds = build(a.game, read('laptop_r_and_d.5tiles.ui'), read('rnd_overview.5tiles.fill.idf'),
                       read('tex', 'graph_b5.bc7'), out_dir)
    print(f'wrote {out_dir}: {len(out.files)} files, ops {kinds}')


if __name__ == '__main__':
    main()
