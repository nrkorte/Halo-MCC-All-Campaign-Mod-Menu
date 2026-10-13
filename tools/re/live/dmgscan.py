# ODST: counts, per object word offset (0xD0..0x1A0), how often it changes across all AI bipeds (type 0, actor set)
# during <seconds>. Run once idle, once while shooting an AI, and compare. Usage: dmgscan.py <seconds> <label>
import os,struct,time,sys,collections
os.environ['H3MEM_DLL']='odst'
import h3mem as m
st=m.state()
def sl(n): return m.q(st+8*n)
objs=sl(4); base=m.q(objs+0x48); es=m.i32(objs+0x24)
cnt=collections.Counter(); prev={}; t0=time.time(); ex={}
while time.time()-t0<float(sys.argv[1]):
    for i in range(2048):
        try:
            e=base+es*i
            if not struct.unpack('<H',m.rd(e,2))[0]: prev.pop(i,None); continue
            o=m.q(e+0x10)
            if not o or m.rd(o+0x96,1)[0]!=0 or m.i32(o+0x160)==-1: continue
            d=m.rd(o+0xD0,0xD0)
            p=prev.get(i)
            if p:
                for k in range(0,len(d),4):
                    if d[k:k+4]!=p[k:k+4]:
                        cnt[0xD0+k]+=1; ex[0xD0+k]=(i,p[k:k+4].hex(),d[k:k+4].hex())
            prev[i]=d
        except Exception: pass
    time.sleep(0.03)
print(sys.argv[2],{hex(k):v for k,v in sorted(cnt.items())})
print({hex(k):v for k,v in ex.items()})
