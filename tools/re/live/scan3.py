from h2mem import *
import ctypes, math, numpy as np
from ctypes import wintypes as W
class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',W.DWORD),('PartitionId',W.WORD),('RegionSize',ctypes.c_size_t),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
k.VirtualQueryEx.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.POINTER(MBI),ctypes.c_size_t]
U=obj(player_unit())
def look():
    x,y,z=struct.unpack('<3f',rd(U+0x168,12)); return math.atan2(y,x), math.asin(max(-1,min(1,z)))
def match(a,b,yaw,pitch):
    return (abs(a-yaw)<0.01 or abs(a-(yaw+2*math.pi))<0.01) and abs(b-pitch)<0.01
addr=0; mbi=MBI(); good=[]
while addr<0x7FFFFFFFFFFF:
    if not k.VirtualQueryEx(H,ctypes.c_void_p(addr),ctypes.byref(mbi),ctypes.sizeof(mbi)): break
    base=mbi.BaseAddress or 0; size=mbi.RegionSize
    if mbi.State==0x1000 and mbi.Protect in (0x04,0x40) and size<0x40000000:
        try:
            yaw,pitch=look(); buf=rd(base,size)
            a=np.frombuffer(buf[:len(buf)//4*4],dtype=np.float32)
            with np.errstate(invalid='ignore'):
                m=((np.abs(a[:-1]-yaw)<0.01)|(np.abs(a[:-1]-yaw-2*math.pi)<0.01))&(np.abs(a[1:]-pitch)<0.01)
            for i in np.nonzero(m)[0][:20]:
                h=base+i*4; yaw2,pitch2=look()
                if match(f32(h),f32(h+4),yaw2,pitch2): good.append(h)
        except Exception: pass
    addr=base+size
print('consistent hits',[hex(h) for h in good])
