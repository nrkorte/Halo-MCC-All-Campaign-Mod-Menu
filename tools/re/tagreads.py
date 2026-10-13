import pickle, re, sys
lines=pickle.load(open('text.pkl','rb'))
TAGBASE=0x1FCF4C8
tagbase_at=set()
for i,(a,m,o) in enumerate(lines):
    mm=re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]',o)
    if mm and i+1<len(lines):
        t=lines[i+1][0]+int(mm.group(2),16)*(1 if mm.group(1)=='+' else -1)
        if t==TAGBASE: tagbase_at.add(i)
for off in sys.argv[1:]:
    pat=re.compile(r'\+ '+off+r'\]')
    hits=[a for i,(a,m,o) in enumerate(lines) if pat.search(o) and any(j in tagbase_at for j in range(i-15,i))]
    print(off, 'tag-based accesses:', len(hits), ' e.g.', ', '.join(f'{h:x}' for h in hits[:5]))
