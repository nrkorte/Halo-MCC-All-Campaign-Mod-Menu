from h2mem import *
import ctypes, sys
from ctypes import wintypes as W
class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',W.DWORD),('PartitionId',W.WORD),('RegionSize',ctypes.c_size_t),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
k.VirtualQueryEx.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.POINTER(MBI),ctypes.c_size_t]
needle=sys.argv[1].encode().replace(b'/',bytes([92]))
addr=0; mbi=MBI(); hits=[]
while addr<0x7FFFFFFFFFFF and len(hits)<10:
    if not k.VirtualQueryEx(H,ctypes.c_void_p(addr),ctypes.byref(mbi),ctypes.sizeof(mbi)): break
    base=mbi.BaseAddress or 0; size=mbi.RegionSize
    if mbi.State==0x1000 and mbi.Protect in (0x02,0x04,0x40,0x20) and size<0x40000000:
        try:
            buf=rd(base,size); i=buf.find(needle)
            while i!=-1 and len(hits)<10:
                hits.append(base+i); i=buf.find(needle,i+1)
        except Exception: pass
    addr=base+size
for h in hits: print(hex(h), rd(h-40,120))
