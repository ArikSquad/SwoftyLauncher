import os, sys, struct, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 're', 'tools'))
from pe_index import *
from capstone import *

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True


def rd(va, n):
    o = rva2off(va - BASE)
    return bytes(DATA[o:o + n])


def func_bounds(va):
    f = func_of(va - BASE)
    return (BASE + f[0], BASE + f[1]) if f else None


def disasm(start, end):
    return list(md.disasm(rd(start, end - start), start))


def jump_tables(ins):
    out = {}
    for i, a in enumerate(ins):
        if a.mnemonic == 'lea' and 'rip +' in a.op_str and i + 3 < len(ins):
            nxt = ins[i + 1]
            if nxt.mnemonic == 'movsxd' and 'dword ptr [' in nxt.op_str and '*4]' in nxt.op_str:
                tbl = a.address + a.size + int(a.op_str.split('rip + ')[1].rstrip(']'), 16)
                bound = None
                for b in reversed(ins[max(0, i - 4):i]):
                    m = re.match(r'cmp \w+, (0x[0-9a-f]+|\d+)', b.mnemonic + ' ' + b.op_str)
                    if m:
                        bound = int(m.group(1), 0)
                        break
                n = (bound + 1) if bound is not None else 4
                out[tbl] = [tbl + struct.unpack('<i', rd(tbl + 4 * k, 4))[0] for k in range(n)]
    return out


def targets(ins):
    t = set()
    for a in ins:
        if a.mnemonic.startswith('j') or a.mnemonic == 'call':
            try:
                t.add(int(a.op_str, 16))
            except ValueError:
                pass
    for tbl, ts in jump_tables(ins).items():
        t.update(ts)
    return t


def check_site(fstart, fend, site, need=5):
    ins = disasm(fstart, fend)
    addrs = {a.address: a for a in ins}
    if site not in addrs:
        return False, f'{site:x} is not an instruction boundary', []
    covered = []
    cur = site
    while cur - site < need:
        a = addrs[cur]
        covered.append(a)
        cur += a.size
    tg = targets(ins)
    inner = [x for x in tg if site < x < cur]
    ok = not inner
    return ok, (f'site {site:x}: {cur - site} bytes over {len(covered)} insn(s); '
                f'internal branch targets: {[hex(x) for x in inner] or "none"}'), covered


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'range':
        s, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
        f = func_bounds(s)
        ins = disasm(f[0], f[1])
        tg = targets(ins)
        for a in ins:
            if s <= a.address < e:
                mark = '>' if a.address in tg else ' '
                print(f'{mark}{a.address:x}: {a.bytes.hex():28s} {a.mnemonic} {a.op_str}')
    elif cmd == 'check':
        for sv in sys.argv[2:]:
            site = int(sv, 16)
            f = func_bounds(site)
            ok, msg, cov = check_site(f[0], f[1], site)
            print(('OK  ' if ok else 'BAD ') + f'func {f[0]:x}-{f[1]:x} ' + msg)
            for a in cov:
                print(f'      {a.address:x}: {a.bytes.hex():28s} {a.mnemonic} {a.op_str}')
    elif cmd == 'tables':
        site = int(sys.argv[2], 16)
        f = func_bounds(site)
        ins = disasm(f[0], f[1])
        for tbl, ts in jump_tables(ins).items():
            print(hex(tbl), [hex(x) for x in ts])
