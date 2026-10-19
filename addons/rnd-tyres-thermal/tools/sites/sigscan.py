import os, sys, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 're', 'tools'))
from pe_index import *

va, b = code_bytes('.xtls')
b = bytes(b)
for pat in sys.argv[1:]:
    rx = b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in pat.split())
    hits = [m.start() for m in re.finditer(rx, b, re.S)]
    print(len(hits), [hex(BASE + va + h) for h in hits[:4]], pat)
