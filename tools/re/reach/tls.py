# Finds TLS slot reads (gs:[0x58] + tls index var + const offset) inside a function and its direct callees.
import pickle,re,sys,bisect
from hs import *
L=pickle.load(open('text.pkl','rb')); idx={a:i for i,(a,m,o) in enumerate(L)}
TLSVAR=0xC17B18
def ript(i):
    a,m,o=L[i]; mm=re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]',o)
    return L[i+1][0]+int(mm.group(2),16)*(1 if mm.group(1)=='+' else -1) if mm else None
def fn_end(a):
    i=idx[a]
    while i<len(L) and L[i][1]!='int3': i+=1
    return i
def slots(a,depth=1,seen=None,indent=''):
    seen=seen or set()
    if a in seen or a not in idx: return
    seen.add(a)
    i=idx[a]; e=min(fn_end(a),i+600)
    consts={}
    for j in range(i,e):
        b,m,o=L[j]
        mm=re.match(r'(e\w\w|r\w+d), (0x[0-9a-f]+)$',o) if m=='mov' else None
        if mm: consts[mm.group(1)]=int(mm.group(2),16)
        if ript(j)==TLSVAR:
            # look ahead for slot
            for k in range(j,min(j+12,e)):
                bb,mm2,oo=L[k]
                m3=re.search(r'\[(r\w+) \+ (r\w+)\]',oo) if mm2=='mov' else None
                m4=re.search(r'\[r\w+ \+ (0x[0-9a-f]+)\]$',oo) if mm2=='mov' and k>j+1 else None
                if m3:
                    for r in (m3.group(1),m3.group(2)):
                        r32={'r8':'r8d','r9':'r9d','r10':'r10d','r11':'r11d','rcx':'ecx','rdx':'edx','rax':'eax'}.get(r,r)
                        if r32 in consts and consts[r32]>0x10: print(f'{indent}{bb:x} slot 0x{consts[r32]:x} ({consts[r32]//8})'); break
                if m4 and int(m4.group(1),16)%8==0 and int(m4.group(1),16)>8: print(f'{indent}{bb:x} slot? 0x{int(m4.group(1),16):x} ({int(m4.group(1),16)//8})'); break
        if depth>0 and m=='call' and o.startswith('0x18'):
            t=int(o,16)-B
            if t!=0x865e8 and t!=0x85de0: slots(t,depth-1,seen,indent+'  ')
if __name__=='__main__':
    for n in sys.argv[1:]:
        print('==',n); slots(evaluator(n) if n in F else int(n,16), 2)
