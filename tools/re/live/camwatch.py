# ODST: logs, once a second and on every change, the player's camo flag (unit +0x168), camo level (+0x374),
# carried equipment datum and its object words (+0x160..+0x1A0), and the tick. Usage: camwatch.py <seconds>
import os,struct,time,sys
os.environ['H3MEM_DLL']='odst'
import h3mem as m
st=m.state()
def sl(n): return m.q(st+8*n)
def ent(arr,i): return m.q(arr+0x48)+m.i32(arr+0x24)*i
def obj(d):
    e=ent(sl(4),d&0xffff)
    return m.q(e+0x10) if struct.unpack('<H',m.rd(e,2))[0]==(d>>16) else 0
t0=time.time(); prev={}
while time.time()-t0<float(sys.argv[1]):
    try:
        pu=m.u32(ent(sl(2),0)+0x28); u=obj(pu)
        gt=m.i32(sl(25)+0xC)
        eq=m.u32(u+0x29c); e=obj(eq) if eq!=0xffffffff else 0
        rows={'unit':m.rd(u+0x360,0x40),'u168':m.rd(u+0x168,4),'u290':m.rd(u+0x290,0x20)}
        if e: rows['eq']=m.rd(e+0x160,0x40)
        out=[]
        for k,v in rows.items():
            if prev.get(k)!=v:
                out.append((k,[(hex(i),v[i:i+4].hex()) for i in range(0,len(v),4) if prev.get(k) is None or prev[k][i:i+4]!=v[i:i+4]][:10]))
        prev=rows
        print(f'{time.time()-t0:6.1f} tick={gt} flags={m.u32(u+0x168):#x} lvl={m.f32(u+0x374):.2f} eq={eq:#x}', out if out else '',flush=True)
    except Exception as ex: print('err',ex,flush=True)
    time.sleep(0.5)
