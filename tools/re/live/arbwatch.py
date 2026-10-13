# Live (Halo 3): follows every actor whose character name matches a substring (default 'arbiter'|'dervish'):
# prints on change: actor datum, unit datum, health/shield (+0xF4/+0xF8), damage flags (+0x110), actor active byte,
# and any NEW unit of that character appearing (a re-placed copy). Usage: python arbwatch.py [seconds] [substr]
import sys, time, struct
from h3mem import *
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 120
pat = (sys.argv[2] if len(sys.argv) > 2 else 'arbiter,dervish').split(',')
t0 = time.time(); last = {}; seen_units = set()
print('map', mapname(), flush=True)
while time.time() - t0 < secs:
    try:
        acts = slot(1); n = i32(acts + 0x3C)
        cur = {}
        for i in range(min(n, 4096)):
            a = entry(acts, i); salt = struct.unpack('<H', rd(a, 2))[0]
            if not salt: continue
            ch = u32(a + 0x5C)
            if ch == 0xffffffff: continue
            name = nm(ch)
            if not any(p in name for p in pat): continue
            unit = u32(a + 0x14); o = obj(unit)
            cur[(salt << 16) | i] = (name, '%08x' % unit,
                ('hp %.3f sh %.3f flags %08x' % (f32(o + 0xF4), f32(o + 0xF8), u32(o + 0x110))) if o else 'NO UNIT',
                'active %d' % rd(a + 9, 1)[0])
            if unit not in seen_units:
                seen_units.add(unit); print('%7.2f NEW unit %08x of %s (actor %08x)' % (time.time() - t0, unit, name, (salt << 16) | i), flush=True)
        for k, v in cur.items():
            if last.get(k) != v: print('%7.2f actor %08x %s' % (time.time() - t0, k, ' | '.join(v)), flush=True)
        for k in last:
            if k not in cur: print('%7.2f actor %08x GONE' % (time.time() - t0, k), flush=True)
        last = cur
    except Exception as ex:
        print('err', ex, flush=True); time.sleep(0.5)
    time.sleep(0.05)
