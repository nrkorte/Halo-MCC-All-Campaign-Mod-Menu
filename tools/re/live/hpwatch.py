# ODST: logs every change of health (+0xE8) / shield (+0xEC) of any biped with an actor, with the tick. Usage: hpwatch.py <seconds>
import os,struct,time,sys
os.environ['H3MEM_DLL']='odst'
import h3mem as m
st=m.state()
def sl(n): return m.q(st+8*n)
objs=sl(4); base=m.q(objs+0x48); es=m.i32(objs+0x24); hw=m.i32(objs+0x3c) if False else 2048
prev={}; t0=time.time()
while time.time()-t0<float(sys.argv[1]):
    gt=m.i32(sl(25)+0xC)
    for i in range(hw):
        try:
            e=base+es*i
            salt=struct.unpack('<H',m.rd(e,2))[0]
            if not salt: prev.pop(i,None); continue
            o=m.q(e+0x10)
            if not o or m.rd(o+0x96,1)[0]!=0: continue   # biped = type 0
            if m.i32(o+0x160)==-1: continue                # needs an actor
            h=m.f32(o+0xE8); s=m.f32(o+0xEC)
            p=prev.get(i)
            if p and (abs(p[0]-h)>1e-4 or abs(p[1]-s)>1e-4):
                print(f'tick {gt} obj {i} health {p[0]:.4f}->{h:.4f} shield {p[1]:.4f}->{s:.4f}',flush=True)
            prev[i]=(h,s)
        except Exception: pass
    time.sleep(0.05)
print('end')
