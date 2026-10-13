import sys,re
from hs import *
HELP={0x865e8,0x85de0}
def body(a,n=50):
    out=[];calls=[]
    for ins in md.disasm(img[a:a+n*15],B+a):
        out.append(f"  {ins.address-B:08x}: {ins.mnemonic} {ins.op_str}")
        if ins.mnemonic=='call' and ins.op_str.startswith('0x18'):
            t=int(ins.op_str,16)-B
            if t not in HELP: calls.append(t)
        if len(out)>=n or ins.mnemonic=='int3': break
    return out,calls
for a in sys.argv[1:]:
    n=int(a.split(':')[1]) if ':' in a else 40; a=a.split(':')[0]
    r=evaluator(a) if a in F else int(a,16)
    print('==',a,hex(r)); o,c=body(r,n); print('\n'.join(o))
