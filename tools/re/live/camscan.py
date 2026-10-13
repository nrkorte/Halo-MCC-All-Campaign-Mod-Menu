# ODST: snapshots the player's whole unit (0x600) and carried equipment object (0x200) twice a second and
# reports, per word, how it changed over the run (counters stand out). Usage: camscan.py <seconds>
import os,struct,time,sys,collections
os.environ['H3MEM_DLL']='odst'
import h3mem as m
st=m.state()
def sl(n): return m.q(st+8*n)
def ent(arr,i): return m.q(arr+0x48)+m.i32(arr+0x24)*i
def obj(d):
    e=ent(sl(4),d&0xffff)
    return m.q(e+0x10) if struct.unpack('<H',m.rd(e,2))[0]==(d>>16) else 0
hist=collections.defaultdict(list); t0=time.time(); flags=[]
while time.time()-t0<float(sys.argv[1]):
    try:
        pu=m.u32(ent(sl(2),0)+0x28); u=obj(pu)
        eq=m.u32(u+0x29c); e=obj(eq) if eq!=0xffffffff else 0
        gt=m.i32(sl(25)+0xC)
        flags.append((round(time.time()-t0,1),gt,m.u32(u+0x168)))
        for name,base,size in (('unit',u,0x600),('eq',e,0x200)):
            if not base: continue
            d=m.rd(base,size)
            for i in range(0,size,4):
                w=struct.unpack_from('<I',d,i)[0]
                h=hist[(name,i)]
                if not h or h[-1][1]!=w: h.append((gt,w))
    except Exception as ex: print('err',ex,flush=True)
    time.sleep(0.5)
print('flags timeline (t,tick,+0x168):'); prev=None
for f in flags:
    if f[2]!=prev: print(' ',f); prev=f[2]
print('words with 2-14 changes (tick, value):')
for (name,i),h in sorted(hist.items()):
    if 2<=len(h)<=14: print(name,hex(i),[(g,hex(w)) for g,w in h])
