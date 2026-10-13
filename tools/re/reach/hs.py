import re,struct,sys,pickle
from capstone import *
xml=open('xml/ReachMCC_U13_Scripting.xml',encoding='ascii',errors='ignore').read()
F={m.group(2):int(m.group(1),16) for m in re.finditer(r'<function opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)"',xml)}
G={m.group(2):int(m.group(1),16) for m in re.finditer(r'<global opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)"',xml)}
img=open('reach.img','rb').read(); B=0x180000000
q=lambda a: struct.unpack_from('<Q',img,a)[0]
T=0xa28d40; GT=0xb42b68
md=Cs(CS_ARCH_X86,CS_MODE_64)
def defn(name): return q(T+8*F[name])-B
def evaluator(name):
    return q(defn(name)+0x20)-B
def glob(name):
    e=GT+0x18*G[name]; return struct.unpack_from('<I',img,e+8)[0], (q(e+0x10)-B if q(e+0x10) else 0)
def dis(rva,n=60,stop=True):
    out=[]
    for ins in md.disasm(img[rva:rva+n*15],B+rva):
        out.append(f"  {ins.address-B:08x}: {ins.mnemonic} {ins.op_str}")
        if len(out)>=n or (stop and ins.mnemonic in('int3',)): break
    return '\n'.join(out)
if __name__=='__main__':
    for a in sys.argv[1:]:
        if a in G: print(a,'global type/addr',[hex(x) for x in glob(a)]); continue
        d=defn(a); print(a,'opcode',hex(F[a]),'def',hex(d)); print(img[d:d+0x30].hex(' '))
def ev(name,n=60): return dis(evaluator(name),n)
