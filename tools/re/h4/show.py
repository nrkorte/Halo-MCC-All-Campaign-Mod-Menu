import sys,struct
from capstone import *
B=0x180000000
img=open('h4.img','rb').read()
md=Cs(CS_ARCH_X86,CS_MODE_64)
def dis(rva,n=40):
    out=[]
    for i in md.disasm(img[rva:rva+n*15],B+rva):
        out.append(f'  {i.address-B:08x}: {i.mnemonic} {i.op_str}')
        if len(out)>=n or i.mnemonic=='int3': break
    return '\n'.join(out)
if __name__=='__main__':
    for a in sys.argv[1:]:
        r,_,n=a.partition(':'); print('==',r); print(dis(int(r,16),int(n) if n else 40))
