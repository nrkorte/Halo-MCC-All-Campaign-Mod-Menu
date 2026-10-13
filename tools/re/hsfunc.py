import struct, sys
from capstone import *
img=open('halo3.img','rb').read(); BASE=0x180000000
def q(a): return struct.unpack_from('<Q',img,a)[0]
T=0x7f6ad0
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=False
def defn(op):
    p=q(T+8*op)-BASE
    return p, img[p:p+0x30]
def dis(rva, n=40):
    out=[]
    for ins in md.disasm(img[rva:rva+400], BASE+rva):
        out.append(f"  {ins.address-BASE:08x}: {ins.mnemonic} {ins.op_str}")
        if len(out)>=n or ins.mnemonic in ('ret','int3'): break
    return "\n".join(out)
if __name__=='__main__':
    for a in sys.argv[1:]:
        op=int(a,16); p,raw=defn(op)
        print(hex(op), 'def@', hex(p), raw.hex(' ',8))
