import pickle,sys
lines=pickle.load(open('text.pkl','rb'))
a=int(sys.argv[1],16); b=int(sys.argv[2],16)
for ad,m,o in lines:
    if a<=ad<b: print('%08x: %s %s'%(ad,m,o))
