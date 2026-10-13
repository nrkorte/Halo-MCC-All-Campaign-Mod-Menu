# Live: logs a unit's damage flags (+0x138), +0x143, the AI revive table and
# whether the unit still exists, on change. Usage: python katwatch.py <unit datum hex> <seconds>
import sys,time,struct; from reachlive import *
u=int(sys.argv[1],16); end=time.time()+float(sys.argv[2]); t0=time.time(); last=None
while time.time()<end:
    try:
        o=obj(u); g=slot(6)
        tab=' '.join(h.rd(g+0x43c+0x18*i,0x10).hex() for i in range(8) if h.u32(g+0x43c+0x18*i)!=0xffffffff)
        cur=(('flags %08x 143=%02x parent %08x'%(h.u32(o+0x138),h.rd(o+0x143,1)[0],h.u32(o+0x14))) if o else 'GONE', tab)
        if cur!=last: print('%6.2f'%(time.time()-t0),cur[0],'| table',cur[1] or '-',flush=True); last=cur
    except Exception as ex: print('err',ex,flush=True); time.sleep(0.5)
    time.sleep(0.05)
