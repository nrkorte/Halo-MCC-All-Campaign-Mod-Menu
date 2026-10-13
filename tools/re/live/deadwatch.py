# Live (Halo 3): word-level diffs of a unit's object bytes (flags area) and the AI-globals revive area, to see
# what revives it. usage: python deadwatch.py <unit datum hex> <seconds>
import sys, time, struct
from h3mem import *
u = int(sys.argv[1], 16); end = time.time() + float(sys.argv[2]); t0 = time.time()
R = [(0x100, 0x180), (0x300, 0x3c0)]  # object ranges
prev = None; pa = None; had = True
def words(b): return struct.unpack('<%dI' % (len(b) // 4), b)
while time.time() < end:
    try:
        o = obj(u); g = slot(6)
        cur = {}
        if o:
            for a, b in R: cur.update({a + 4 * i: w for i, w in enumerate(words(rd(o + a, b - a)))})
        ai = words(rd(g + 0x3e0, 0xe0))
        out = []
        if prev is not None and o:
            for k, v in cur.items():
                if prev.get(k) != v and k not in (0xf4, 0xf8): out.append('o+%x %08x>%08x' % (k, prev.get(k, 0), v))
        if pa is not None:
            for i, w in enumerate(ai):
                if pa[i] != w: out.append('ai+%x %08x>%08x' % (0x3e0 + 4 * i, pa[i], w))
        if bool(o) != had: print('%7.2f %s' % (time.time() - t0, 'object appeared' if o else 'OBJECT GONE'), flush=True); had = bool(o)
        if out: print('%7.2f %s' % (time.time() - t0, ' '.join(out[:24])), flush=True)
        prev = cur if o else prev; pa = ai
    except Exception as ex: print('err', ex, flush=True); time.sleep(0.5)
    time.sleep(0.05)
