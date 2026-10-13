# Direct call/jmp sites (E8/E9 rel32) to an RVA, by raw byte scan of .text.
import sys,struct
import numpy as np
img=open('reach.img','rb').read()
lo,hi=0x1000,0x91e000
buf=np.frombuffer(img[lo:hi+8],dtype=np.uint8)
def xrefs(t):
    out=[]
    rel=np.frombuffer(img[lo+1:hi+5],dtype=np.int32) if False else None
    for op in (0xE8,0xE9):
        idx=np.nonzero(buf[:hi-lo]==op)[0]
        for i in idx:
            a=lo+i
            r=struct.unpack_from('<i',img,a+1)[0]
            if a+5+r==t: out.append((a,'call' if op==0xE8 else 'jmp'))
    return out
if __name__=='__main__':
    for x in sys.argv[1:]:
        print(x,[(hex(a),k) for a,k in xrefs(int(x,16))])
