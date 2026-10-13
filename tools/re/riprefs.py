import pickle, re, sys
lines=pickle.load(open('text.pkl','rb'))
targets={int(t,16) for t in sys.argv[1].split(',')}
wonly = len(sys.argv)>2 and sys.argv[2]=='w'
for i,(a,m,o) in enumerate(lines):
    mm=re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]',o)
    if not mm: continue
    nxt=lines[i+1][0] if i+1<len(lines) else a
    t=nxt+(int(mm.group(2),16)*(1 if mm.group(1)=='+' else -1))
    if t in targets:
        is_w = o.startswith('qword ptr [rip') or o.startswith('dword ptr [rip')
        if wonly and not is_w: continue
        print(f'{a:08x}: {m} {o}   -> {t:x}')
