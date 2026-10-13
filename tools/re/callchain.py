import pickle, re, bisect, sys
lines=pickle.load(open('text.pkl','rb'))
starts=[lines[i+1][0] for i in range(len(lines)-1) if lines[i][1]=='int3' and lines[i+1][1]!='int3']
def fstart(a):
    k=bisect.bisect_right(starts,a)-1
    return starts[k]
callers={}
for a,m,o in lines:
    if m in ('call','jmp') and o.startswith('0x18'):
        t=int(o,16)-0x180000000
        callers.setdefault(t,[]).append(a)
def chain(f,depth,seen):
    cs=callers.get(f,[])
    print('  '*depth+f'{f:x} ({len(cs)} callers)')
    if depth>=6: return
    for c in cs[:3]:
        g=fstart(c)
        if g in seen: continue
        seen.add(g); chain(g,depth+1,seen)
for f in sys.argv[1:]: chain(int(f,16),0,set())
