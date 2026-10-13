# Finds pointers (in any module's image) into [target - span, target].
from h2mem import *
import ctypes, sys, numpy as np
from ctypes import wintypes as W
class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',W.DWORD),('PartitionId',W.WORD),('RegionSize',ctypes.c_size_t),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
k.VirtualQueryEx.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.POINTER(MBI),ctypes.c_size_t]
def scan(target, span, images_only=True):
    addr=0; mbi=MBI(); out=[]
    while addr<0x7FFFFFFFFFFF:
        if not k.VirtualQueryEx(H,ctypes.c_void_p(addr),ctypes.byref(mbi),ctypes.sizeof(mbi)): break
        base=mbi.BaseAddress or 0; size=mbi.RegionSize
        if mbi.State==0x1000 and mbi.Protect in (0x04,0x40,0x08) and size<0x40000000 and (mbi.Type==0x1000000 or not images_only):
            try:
                a=np.frombuffer(rd(base,size)[:size//8*8],dtype=np.uint64)
                for i in np.nonzero((a<=target)&(a>=target-span))[0][:20]:
                    out.append((base+i*8,int(a[i]),mbi.AllocationBase))
            except Exception: pass
        addr=base+size
    return out
if __name__=='__main__':
    t=int(sys.argv[1],16); span=int(sys.argv[2],16)
    for at,val,ab in scan(t,span, len(sys.argv)<4):
        print(hex(at),'->',hex(val),'(target-%#x)'%(t-val),'module base',hex(ab or 0))
