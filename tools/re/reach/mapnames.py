import struct,sys,os
D=r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\haloreach\maps'
def names(m):
    f=open(os.path.join(D,m+'.map'),'rb'); hd=f.read(0xA000); u=lambda o: struct.unpack_from('<I',hd,o)[0]
    mk=u(0x4cc); cnt=u(0x20)
    f.seek((u(0x2c)+mk)&0xffffffff); offs=struct.unpack('<%di'%cnt,f.read(4*cnt))
    f.seek((u(0x24)+mk)&0xffffffff); s=f.read(u(0x28))
    return [s[o:s.find(b'\0',o)].decode('latin1') if 0<=o<len(s) else '' for o in offs]
if __name__=='__main__':
    for m in sys.argv[2:]:
        n=names(m); hits=sorted(set(x for x in n if sys.argv[1] in x.lower() and x.startswith('objects')))
        print(m,len(hits)); [print('  ',x) for x in hits[:60]]
