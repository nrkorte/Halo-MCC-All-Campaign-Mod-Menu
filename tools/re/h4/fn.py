# Function boundaries from .pdata: fn.of(rva) -> (start, end)
import struct,bisect,sys
img=open(__file__.replace('fn.py','h4.img'),'rb').read()
P,PS=0x4995000,0x70b3c
F=sorted(struct.unpack_from('<3I',img,P+12*i)[:2] for i in range(PS//12))
S=[a for a,b in F]
def of(rva):
    i=bisect.bisect_right(S,rva)-1
    while i>=0:
        a,b=F[i]
        if a<=rva<b: return a,b
        i-=1
        if rva-F[i][0]>0x20000: break
    return None
if __name__=='__main__':
    for x in sys.argv[1:]:
        r=of(int(x,16)); print(x, r and (hex(r[0]),hex(r[1])))
