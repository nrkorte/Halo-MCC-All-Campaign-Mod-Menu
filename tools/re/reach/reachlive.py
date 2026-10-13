# Live helpers for Reach (like ../live/h4mem.py, using its process access).
import sys,struct; sys.path.insert(0,'../live'); import h4mem as h
pid,BASE,size=h._find(b'haloreach.dll')
def state():
    idx=h.u32(BASE+0xC17B18)
    for tid in h._threads():
        try:
            teb=h._teb(tid); tls=h.q(teb+0x58)
            if not tls: continue
            blk=h.q(tls+8*idx)
            if blk and h.q(blk+8*2) and h.q(blk+8*3): return blk
        except Exception: pass
S=state()
def slot(n): return h.q(S+8*n)
def entry(arr,i): return h.q(arr+0x50)+h.q(arr+0x20)*i
def obj(d):
    e=entry(slot(2),d&0xffff); return h.q(e+0x10) if struct.unpack('<H',h.rd(e,2))[0]==(d>>16) else 0
def tag_addr(d):
    inst=h.q(BASE+0xC1A600); g,salt,addr=struct.unpack('<hHI',h.rd(inst+8*(d&0xffff),8))
    return h.q(BASE+0x4E39F20+8*(addr>>28))+addr*4 if addr else 0
def mapname():
    return h.rd(h.q(BASE+0x25BDA30)+0x10+0xC0,32).split(b'\0')[0].decode()
_n=None
def names():
    global _n
    if _n: return _n
    p=r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\haloreach\maps\%s.map'%mapname()
    f=open(p,'rb'); hd=f.read(0xA000); u=lambda o: struct.unpack_from('<I',hd,o)[0]
    m=u(0x4cc); cnt=u(0x20)
    f.seek((u(0x2c)+m)&0xffffffff); offs=struct.unpack('<%di'%cnt,f.read(4*cnt))
    f.seek((u(0x24)+m)&0xffffffff); s=f.read(u(0x28))
    _n=[s[o:s.find(b'\0',o)].decode('latin1') if 0<=o<len(s) else '' for o in offs]; return _n
def nm(d): return names()[d&0xffff].split(chr(92))[-1] if d!=0xffffffff else '-'
