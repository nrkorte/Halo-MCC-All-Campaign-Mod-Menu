# Disassemble an HS evaluator and its first-level callees, showing only interesting lines.
import sys,re
from hs import *
SKIP={0x6f9e60,0x6f9f50}
def body(a,n=60):
    out=[];calls=[]
    for ins in md.disasm(img[a:a+n*15],B+a):
        out.append((ins.address-B,ins.mnemonic,ins.op_str))
        if ins.mnemonic=='call' and ins.op_str.startswith('0x18'): calls.append(int(ins.op_str,16)-B)
        if ins.mnemonic=='int3' or len(out)>=n: break
    return out,calls
for name in sys.argv[1:]:
    r=evaluator(name) if name in F else int(name,16)
    print('==',name,hex(r))
    o,c=body(r)
    for a,m,op in o:
        if re.search(r'\+ 0x[0-9a-f]{2,3}\]|rip|bts|btr|or |and ',op) and 'rsp' not in op: print('   %x: %s %s'%(a,m,op))
    for t in c:
        if t in SKIP: continue
        print('  -> callee',hex(t))
        o2,_=body(t,50)
        for a,m,op in o2:
            if re.search(r'\+ 0x[0-9a-f]{2,3}\]|rip|bts|btr|or |and ',op) and 'rsp' not in op: print('     %x: %s %s'%(a,m,op))
