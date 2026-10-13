import pickle,sys,re
lines=pickle.load(open('text.pkl','rb'))
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
rx=re.compile(r'rip ([+-]) (0x[0-9a-f]+)')
for k,(ad,m,o) in enumerate(lines[:-1]):
    mm=rx.search(o)
    if not mm: continue
    d=int(mm.group(2),16)*(1 if mm.group(1)=='+' else -1)
    t=lines[k+1][0]+d
    if lo<=t<hi: print('%08x: %s %s  -> %x'%(ad,m,o,t))
