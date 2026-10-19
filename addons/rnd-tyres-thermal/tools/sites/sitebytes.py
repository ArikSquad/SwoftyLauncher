import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 're', 'tools'))
from pe_index import *

def rd(va, n):
    o = rva2off(va - BASE)
    return bytes(DATA[o:o + n])

for a in sys.argv[1:]:
    s, n = a.split(':')
    s = int(s, 16); n = int(n)
    bs = rd(s, n)
    print(f'{s:x} [{n}] ' + ' '.join(f'{x:02X}' for x in bs) + '   {' + ', '.join(f'0x{x:02x}' for x in bs) + '}')
