import pickle, sys
lines=pickle.load(open('text.pkl','rb'))
idx={a:i for i,(a,m,o) in enumerate(lines)}
a=int(sys.argv[1],16); before=int(sys.argv[2]); after=int(sys.argv[3])
i=idx[a]
for b,m,o in lines[i-before:i+after]: print(('>> ' if b==a else '   ')+f'{b:08x}: {m} {o}')
