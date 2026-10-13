# Finds the camera's view: floats matching player 1's look minus the aimbot
# offset (the raw MCC angle), as a yaw/pitch pair or a forward vector.
from h2mem import *
import ctypes, math, sys, numpy as np
from ctypes import wintypes as W
class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',W.DWORD),('PartitionId',W.WORD),('RegionSize',ctypes.c_size_t),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
k.VirtualQueryEx.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.POINTER(MBI),ctypes.c_size_t]
U=obj(player_unit())
oy,op=float(sys.argv[1]),float(sys.argv[2])
x,y,z=struct.unpack('<3f',rd(U+0x168,12)); uy=math.atan2(y,x); up=math.asin(z)
ry,rp=uy-oy,up-op
fwd=(math.cos(ry)*math.cos(rp),math.sin(ry)*math.cos(rp),math.sin(rp))
print('unit',uy,up,'raw',ry,rp,'fwd',fwd)
yaws=[ry%(2*math.pi), (ry+math.pi)%(2*math.pi)-math.pi]
addr=0; mbi=MBI(); pair=[]; vec=[]
while addr<0x7FFFFFFFFFFF:
    if not k.VirtualQueryEx(H,ctypes.c_void_p(addr),ctypes.byref(mbi),ctypes.sizeof(mbi)): break
    base=mbi.BaseAddress or 0; size=mbi.RegionSize
    if mbi.State==0x1000 and mbi.Protect in (0x04,0x40) and size<0x40000000:
        try:
            a=np.frombuffer(rd(base,size)[:size//4*4],dtype=np.float32)
            with np.errstate(invalid='ignore'):
                ym=np.zeros(len(a)-2,bool)
                for yy in yaws: ym|=np.abs(a[:-2]-yy)<0.005
                m=ym&(np.abs(a[1:-1]-rp)<0.005)
                pair+= [base+i*4 for i in np.nonzero(m)[0][:50]]
                v=(np.abs(a[:-2]-fwd[0])<0.005)&(np.abs(a[1:-1]-fwd[1])<0.005)&(np.abs(a[2:]-fwd[2])<0.005)
                vec+= [base+i*4 for i in np.nonzero(v)[0][:50]]
        except Exception: pass
    addr=base+size
print('yaw/pitch pairs',[hex(h) for h in pair][:40])
print('forward vectors',[hex(h) for h in vec][:40])
