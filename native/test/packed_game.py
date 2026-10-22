#!/usr/bin/env python3
"""Packed-build fixtures for run_wine_test.sh.

make <exe> <packed root> <plain root>: writes bindings.packed-test.json into both game folders (packed: true with
the EA App distribution, packed: false with Steam), copies the exe unencrypted into the plain folder and with .xtls
encrypted into the packed one.
check <mode> <loader.log> <exe>: checks the [bind] lines of a packed, packed_never or packed_plain run.
"""
import json
import os
import re
import struct
import sys

SEED = 0x5EEDC0DE
LINE = re.compile(r'^\[bind\] id=(\S+) build=(steam|ea|unknown) pass=(init|window|stage:\d+) '
                  r'result=(resolved|deferred|unresolved) method=(hint|scan|anchor|anchor-fallback|-) matches=(\d+|-) '
                  r'rva=(0x[0-9a-f]+|-) match=(0x[0-9a-f]+|-) verified=(yes|no|-) reason="((?:[^"\\]|\\.)*)"'
                  r'( site=(0x[0-9a-f]+|-))? site_bytes="((?:[0-9A-F]{2})(?: [0-9A-F]{2}){0,31})?"$')
SUMMARY = re.compile(r'^\[bind\] summary build=(steam|ea|unknown) pass=(init|window|stage:\d+) resolved=\d+ '
                     r'deferred=\d+ unresolved=\d+ total=(\d+) pe_timestamp=-?\d+ size=-?\d+ sha256=([0-9a-f]{64}|-)$')
PLACED = re.compile(r'(installed: |hooked)')
RVA = re.compile(r'base\+0x([0-9a-f]+)')


def pe(b):
    at = struct.unpack_from('<I', b, 0x3c)[0]
    nsec, stamp, opt = (struct.unpack_from('<H', b, at + 6)[0], struct.unpack_from('<I', b, at + 8)[0],
                        struct.unpack_from('<H', b, at + 20)[0])
    secs = [struct.unpack_from('<8sIIII', b, at + 24 + opt + 40 * i) for i in range(nsec)]
    return at, stamp, secs


def xtls(secs):
    _, size, va, _, raw = next(s for s in secs if s[0].rstrip(b'\0') == b'.xtls')
    return size, va, raw


def relocations(b, at, secs):
    rva, size = struct.unpack_from('<II', b, at + 24 + 112 + 5 * 8)

    def off(r):
        return next(s[4] + r - s[2] for s in secs if s[2] <= r < s[2] + max(s[1], s[3]))

    p, out = off(rva), []
    end = p + size
    while p < end and struct.unpack_from('<I', b, p + 4)[0]:
        page, block = struct.unpack_from('<II', b, p)
        out += [page + (e & 0xfff) for e in struct.unpack_from(f'<{(block - 8) // 2}H', b, p + 8) if e >> 12]
        p += block
    return out


def make(exe, pk, pp):
    b = bytearray(open(exe, 'rb').read())
    at, stamp, secs = pe(b)
    size, va, raw = xtls(secs)
    assert size >= 0x2000, f'.xtls is {size:#x} bytes; the core samples whole pages, so it needs two'
    relocs = relocations(b, at, secs)
    # The loader would apply these to the encrypted bytes, which the exe's own decryption cannot undo.
    assert not any(va <= r < va + size for r in relocs), 'a base relocation falls inside .xtls'
    code = bytes(b[raw:raw + size])

    def kb(tag):
        hit = code.find(b'\x48\xb8' + tag.encode())
        assert hit >= 0 and code.find(b'\x48\xb8' + tag.encode(), hit + 1) < 0, tag
        return hex(va + hit)

    for root, packed in ((pk, True), (pp, False)):
        core = os.path.join(root, 'swofty', 'addons', 'core')
        doc = json.load(open(os.path.join(core, 'bindings.json')))
        for x in doc['bindings']:
            if 'kb_tag' in x:
                x['rva'] = kb(x.pop('kb_tag'))
                if not x.pop('kb_no_match', False):
                    x['match'] = kb(x.pop('kb_match_tag')) if 'kb_match_tag' in x else hex(int(x['rva'], 16) - x['anchor'])
            else:
                x.pop('rva', None)
                x.pop('match', None)
                if isinstance(x.get('string_anchor'), dict):
                    x['string_anchor'].pop('func_rva', None)
        doc['build'] = {'exe': 'F1_25.exe', 'sha256': 'packed-test', 'pe_timestamp': stamp, 'size': len(b),
                        'image_base': '0x140000000', 'distribution': 'EA App' if packed else 'Steam', 'packed': packed}
        json.dump(doc, open(os.path.join(core, 'bindings.packed-test.json'), 'w'), indent=1)
        json.dump({'schema': 1, 'profile': 'wine-test', 'addons': [],
                   'early_plugins': ['t_packed.dll', 't_packed_scan.dll']},
                  open(os.path.join(root, 'swofty', 'settings.json'), 'w'))
    open(os.path.join(pp, 'F1_25.exe'), 'wb').write(b)
    x = SEED
    for i in range(size):
        x ^= (x << 13) & 0xffffffff
        x ^= x >> 17
        x ^= (x << 5) & 0xffffffff
        b[raw + i] ^= (x >> 8) & 0xff
    open(os.path.join(pk, 'F1_25.exe'), 'wb').write(b)
    print(f'packed: .xtls ({size:#x} bytes at {va:#x}) encrypted in {pk}; {len(relocs)} relocation(s), none inside it')


def check(mode, log, exe):
    bad = 0

    def ok(cond, what):
        nonlocal bad
        print(f'{"ok  " if cond else "FAIL"}: {mode}: {what}')
        bad += not cond

    _, _, secs = pe(open(exe, 'rb').read())
    size, va, _ = xtls(secs)
    root = os.path.dirname(exe)
    doc = json.load(open(os.path.join(root, 'swofty', 'addons', 'core', 'bindings.packed-test.json')))
    code = {b['id'] for b in doc['bindings'] if b['section'] == '.xtls' and b.get('status') != 'unresolvable'}
    lines = [l.rstrip('\r\n') for l in open(log, encoding='utf-8', errors='replace')]
    window = next((i for i, l in enumerate(lines) if 'stage: window (' in l), len(lines))
    passes, malformed, summaries = {}, [], 0
    for l in lines:
        if '[bind] ' not in l:
            continue
        l = l[l.index('[bind] '):]
        if l.startswith('[bind] summary'):
            summaries += 1
            m = SUMMARY.match(l)
            if not m or int(m.group(3)) != len(doc['bindings']):
                malformed.append(l)
            continue
        m = LINE.match(l)
        if not m or (m.group(4) == 'unresolved') != (m.group(11) is not None) or \
                (m.group(4) == 'resolved') != (m.group(7) != '-') or (m.group(5) == '-') != (m.group(4) != 'resolved'):
            malformed.append(l)
            continue
        passes.setdefault(m.group(3), {})[m.group(1)] = (m.group(4), m.group(10))
    for l in malformed[:3]:
        print('      malformed:', l)
    ok(not malformed and summaries, f'{sum(len(p) for p in passes.values())} [bind] line(s) and {summaries} summary '
       'line(s) follow the log contract, every summary counts the whole set')
    ok(all(len(p) == len(doc['bindings']) for p in passes.values()), 'every pass reports every binding once')
    early = [l for l in lines[:window] if PLACED.search(l) and any(va <= int(r, 16) < va + size for r in RVA.findall(l))]
    for l in early[:3]:
        print('      before the window stage:', l)
    init = passes.get('init', {})
    if mode == 'packed_plain':
        ok(all(init.get(i, ('?',))[0] != 'deferred' for i in code), 'every .xtls binding is settled at init')
        ok(set(passes) == {'init'}, 'init is the only resolution pass')
    else:
        ok(not early, 'no hook is placed in .xtls before the window stage')
        ok(all(init.get(i, ('?',))[0] == 'deferred' for i in code),
           f'all {len(code)} .xtls binding(s) are deferred at init, none resolved or given up on encrypted code')
    if mode == 'packed':
        win = passes.get('window', {})
        ok(all(win.get(i, ('deferred',))[0] != 'deferred' for i in code), 'every .xtls binding is settled at the window pass')
        ok(any(win.get(i, ('',))[0] == 'resolved' for i in code), 'the window pass resolves .xtls bindings')
    if mode == 'packed_never':
        last = passes.get('stage:8', {})
        ok(all(last.get(i, ('',))[0] == 'unresolved' and last[i][1].endswith('so it fails closed') for i in code),
           'every .xtls binding fails closed with a reason at the last retry')
    return bad


if __name__ == '__main__':
    if sys.argv[1] == 'make':
        make(*sys.argv[2:5])
    else:
        sys.exit(1 if check(*sys.argv[2:5]) else 0)
