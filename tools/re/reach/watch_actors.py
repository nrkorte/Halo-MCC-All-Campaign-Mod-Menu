# Logs actor/unit changes in live Reach every 50 ms (for the spawned-AI respawn question).
import time,sys
import reachlive as R
from reachlive import h,struct,entry,nm
out=open(sys.argv[1],'w',buffering=1)
def snap():
    d={}
    if not R.S: R.S=R.state()
    if not R.S: raise Exception('no game state')
    act=R.slot(1); n=h.i32(act+0x44)
    for i in range(n):
        e=entry(act,i); salt=struct.unpack('<H',h.rd(e,2))[0]
        if not salt: continue
        b=h.rd(e,0x40); u=struct.unpack_from('<I',b,0x1c)[0]; o=R.obj(u)
        if o:
            so=struct.unpack('<h',h.rd(o+0x184,2))[0]; off=struct.unpack('<H',h.rd(o+0x186,2))[0]
            hp=round(h.f32(o+off+0x10),2) if so>0 else None
            w=h.u32(o+0x350); dead=(h.u32(o+0x138)>>2)&1
            d[(salt<<16)|i]=(hex(u),nm(h.u32(o)),'team',b[0x2c],'hp',hp,'w',hex(w),'deadflag',dead,'flags6',hex(b[6]))
        else: d[(salt<<16)|i]=(hex(u),'NO UNIT','team',b[0x2c],'flags6',hex(b[6]))
    return d
prev={}
while True:
    try: prev=snap(); break
    except Exception: R.S=None; R._n=None; time.sleep(2)
out.write('start %s\n'%time.strftime('%H:%M:%S'))
for k,v in prev.items(): out.write('  %08x %s\n'%(k,v))
end=time.time()+1800
while time.time()<end:
    try: cur=snap()
    except Exception as e:
        out.write('err %s\n' % e); R.S = None; R._n = None; time.sleep(2); continue
    t=time.strftime('%H:%M:%S')
    for k in cur:
        if k not in prev: out.write('%s NEW %08x %s\n'%(t,k,cur[k]))
        elif cur[k]!=prev[k]: out.write('%s CHG %08x %s -> %s\n'%(t,k,prev[k],cur[k]))
    for k in prev:
        if k not in cur: out.write('%s GONE %08x %s\n'%(t,k,prev[k]))
    prev=cur; time.sleep(0.05)
