import pickle, re, sys
lines=pickle.load(open('text.pkl','rb'))
pat=re.compile(sys.argv[1])
ctx=int(sys.argv[2]) if len(sys.argv)>2 else 0
lim=int(sys.argv[3]) if len(sys.argv)>3 else 60
n=0
for i,(a,m,o) in enumerate(lines):
    if pat.search(m+' '+o):
        for j in range(max(0,i-ctx), min(len(lines), i+ctx+1)):
            b,mm,oo=lines[j]; print(('>> ' if j==i else '   ')+f'{b:08x}: {mm} {oo}')
        if ctx: print('--')
        n+=1
        if n>=lim: break
print('matches shown', n)
