# Live (Halo 3): where does a unit datum appear in a TLS slot block? usage: python findunit.py <datum hex> [slot ...]
import sys, struct
from h3mem import *
d = int(sys.argv[1], 16); pat = struct.pack('<I', d)
for s in ([int(x) for x in sys.argv[2:]] or range(0, 70)):
    try:
        b = slot(s)
        if not b: continue
        data = rd(b, 0x4000)
        i = 0
        while True:
            i = data.find(pat, i)
            if i < 0: break
            print('slot', s, 'offset 0x%x' % i, 'ctx', data[max(0,i-8):i+24].hex()); i += 4
    except Exception as e: pass
