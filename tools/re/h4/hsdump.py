# Halo 4's script function definitions are built on the heap at startup:
# C++ objects: vtable, return type, name, help, ..., evaluator at +0x60. Scans the
# running game's memory for them and writes hs_funcs.txt (name, evaluator
# RVA, definition address). Needs MCC running with halo4.dll loaded.
import sys,struct,re
import numpy as np
sys.path.insert(0,'../live')
import h4mem as h
B=h.BASE
xml=open('xml/H4MCC_U7_Scripting.xml',encoding='ascii',errors='ignore').read()
names=set(re.findall(r'<function opcode="0x[0-9A-Fa-f]+" name="([^"]+)"',xml))
img=open('h4live.img','rb').read()
def s_at(rva):
    e=img.find(b'\0',rva,rva+100); return img[rva:e].decode('ascii','replace') if e>0 else ''
out={}
for base,size in h.regions(private_only=True):
    if size>0x40000000: continue
    data=h.rdp(base,size)
    a=np.frombuffer(data[:len(data)//8*8],dtype=np.uint64)
    if len(a)<8: continue
    # object: vtable +0, return type +8, name +0x10, help +0x18, evaluator +0x60
    if len(a)<16: continue
    m=(a[2:-10]>=B+0xb7a000)&(a[2:-10]<B+0xe3b000)&(a[:-12]>=B+0xb7a000)&(a[:-12]<B+0xe3b000)&(a[12:]>=B+0x1000)&(a[12:]<B+0xb7a000)
    for i in np.nonzero(m)[0]:
        n=s_at(int(a[i+2])-B)
        if n in names and n not in out: out[n]=(int(a[i+12])-B, base+8*int(i))
open('hs_funcs.txt','w').write(''.join(f'{n} {v[0]:x} {v[1]:x}\n' for n,v in sorted(out.items())))
print(len(out),'of',len(names))
