# Live (Halo 3): logs changes of the revert/game-state word (RVA 0x20B96AA, 2 bytes) and the Arbiter's health. usage: python revwatch.py <secs>
import sys, time, struct
from h3mem import *
end = time.time() + float(sys.argv[1]); t0 = time.time(); last = None; lasth = None
while time.time() < end:
    try:
        w = rd(BASE + 0x20B96AA, 8).hex(); hp = None
        acts = slot(1)
        for i in range(min(i32(acts + 0x3C), 4096)):
            a = entry(acts, i)
            if struct.unpack('<H', rd(a, 2))[0] and 'dervish' in nm(u32(a + 0x5C)):
                o = obj(u32(a + 0x14)); hp = ('%.2f' % f32(o + 0xF4)) if o else 'nounit'
        if w != last: print('%7.2f word %s' % (time.time() - t0, w), flush=True); last = w
        if hp != lasth and (hp is None or hp == 'nounit' or lasth is None or abs(float(hp) - float(lasth if lasth not in (None, 'nounit') else 1)) > 0.15 or hp in ('nounit',)):
            print('%7.2f arbiter hp %s' % (time.time() - t0, hp), flush=True); lasth = hp
    except Exception as ex: pass
    time.sleep(0.01)
