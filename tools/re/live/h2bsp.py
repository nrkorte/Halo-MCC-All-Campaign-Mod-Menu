# Loads the live structure BSP's collision geometry and finds surfaces near a point.
from h2mem import *
TB=q(BASE+0x15E4B38)
def blk(base,off,size):
    c,o=struct.unpack('<ii',rd(base+off,8)); return c,TB+o,size
def arr(b):
    c,a,s=b; return rd(a,c*s)
def load(sbsp):
    col=TB+struct.unpack('<i',rd(sbsp+0x18,4))[0]
    planes=arr(blk(col,0x8,0x10)); surfs=arr(blk(col,0x28,8)); edges=arr(blk(col,0x30,0xC)); verts=arr(blk(col,0x38,0x10))
    P=[struct.unpack_from('<4f',planes,i*16) for i in range(len(planes)//16)]
    S=[struct.unpack_from('<hhBBh',surfs,i*8) for i in range(len(surfs)//8)]
    E=[struct.unpack_from('<6h',edges,i*12) for i in range(len(edges)//12)]
    V=[struct.unpack_from('<3f',verts,i*16) for i in range(len(verts)//16)]
    return col,P,S,E,V
def poly(si,S,E,V):
    # walk the surface's edge ring
    out=[]; e=S[si][1]; first=e
    for _ in range(64):
        sv,ev,fw,rv,ls,rs=E[e&0xffff]
        if ls==si: out.append(V[sv]); e=fw
        else: out.append(V[ev]); e=rv
        if e==first: break
    return out
def inst(sb,i):
    dc,da,_=blk(sb,0x138,0xC8); ic,ia,_=blk(sb,0x140,0x58)
    b=rd(ia+i*0x58,0x58); sc=struct.unpack_from('<f',b,0)[0]
    F=struct.unpack_from('<3f',b,4);L=struct.unpack_from('<3f',b,0x10);U=struct.unpack_from('<3f',b,0x1c);pos=struct.unpack_from('<3f',b,0x28)
    di,=struct.unpack_from('<h',b,0x34); d=da+di*0xC8
    planes=arr(blk(d,0x78,0x10)); surfs=arr(blk(d,0x98,8)); edges=arr(blk(d,0xa0,0xC)); verts=arr(blk(d,0xa8,0x10))
    P=[struct.unpack_from('<4f',planes,k*16) for k in range(len(planes)//16)]
    S=[struct.unpack_from('<hhBBh',surfs,k*8) for k in range(len(surfs)//8)]
    E=[struct.unpack_from('<6h',edges,k*12) for k in range(len(edges)//12)]
    V=[struct.unpack_from('<3f',verts,k*16) for k in range(len(verts)//16)]
    def tolocal(p):
        r=[p[k]-pos[k] for k in range(3)]
        return tuple(sum(r[k]*A[k] for k in range(3))/sc for A in (F,L,U))
    return di,d,(P,S,E,V),tolocal
def near(geo,lp,n=12):
    P,S,E,V=geo; out=[]
    for i,(pl,fe,fl,br,mi) in enumerate(S):
        a,b,c,dd=P[pl&0x7fff]; dist=a*lp[0]+b*lp[1]+c*lp[2]-dd
        vs=poly(i,S,E,V)
        mn=[min(v[k] for v in vs) for k in range(3)]; mx=[max(v[k] for v in vs) for k in range(3)]
        gap=sum(max(0,mn[k]-lp[k],lp[k]-mx[k])**2 for k in range(3))**.5
        out.append((round(max(gap,abs(dist)),2),i,hex(fl),mi,(round(a,2),round(b,2),round(c,2)),[round(x,1) for x in mn],[round(x,1) for x in mx]))
    out.sort(); return out[:n]
