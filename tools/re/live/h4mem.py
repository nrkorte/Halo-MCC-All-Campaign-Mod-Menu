# Live reader/writer for halo4.dll in a running MCC (same idea as h2mem.py).
import ctypes, struct, math
from ctypes import wintypes as W
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.restype=W.HANDLE
k.ReadProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
k.WriteProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
def _find(dll=b'halo4.dll'):
    class PE(ctypes.Structure):
        _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('th32ProcessID',W.DWORD),('th32DefaultHeapID',ctypes.c_void_p),('th32ModuleID',W.DWORD),('cntThreads',W.DWORD),('th32ParentProcessID',W.DWORD),('pcPriClassBase',ctypes.c_long),('dwFlags',W.DWORD),('szExeFile',ctypes.c_char*260)]
    class ME(ctypes.Structure):
        _fields_=[('dwSize',W.DWORD),('th32ModuleID',W.DWORD),('th32ProcessID',W.DWORD),('GlblcntUsage',W.DWORD),('ProccntUsage',W.DWORD),('modBaseAddr',ctypes.c_void_p),('modBaseSize',W.DWORD),('hModule',W.HMODULE),('szModule',ctypes.c_char*256),('szExePath',ctypes.c_char*260)]
    k.CreateToolhelp32Snapshot.restype=W.HANDLE
    s=k.CreateToolhelp32Snapshot(2,0); e=PE(); e.dwSize=ctypes.sizeof(e); pid=None
    ok=k.Process32First(s,ctypes.byref(e))
    while ok:
        if e.szExeFile.lower()==b'mcc-win64-shipping.exe': pid=e.th32ProcessID
        ok=k.Process32Next(s,ctypes.byref(e))
    s=k.CreateToolhelp32Snapshot(0x18,pid); m=ME(); m.dwSize=ctypes.sizeof(m)
    ok=k.Module32First(s,ctypes.byref(m))
    while ok:
        if m.szModule.lower()==dll: return pid,m.modBaseAddr,m.modBaseSize
        ok=k.Module32Next(s,ctypes.byref(m))
    return pid,None,0
PID,BASE,SIZE=_find(); H=k.OpenProcess(0x8|0x20|0x10|0x400,False,PID)
def rd(a,n):
    b=ctypes.create_string_buffer(n); r=ctypes.c_size_t()
    if not k.ReadProcessMemory(H,ctypes.c_void_p(a),b,n,ctypes.byref(r)): raise OSError(ctypes.get_last_error(),hex(a))
    return b.raw
def rdp(a,n):
    # partial read: unreadable pages come back as zeros
    out=bytearray(n); off=0
    while off<n:
        c=min(0x1000-((a+off)&0xfff),n-off)
        b=ctypes.create_string_buffer(c); r=ctypes.c_size_t()
        if k.ReadProcessMemory(H,ctypes.c_void_p(a+off),b,c,ctypes.byref(r)): out[off:off+c]=b.raw
        off+=c
    return bytes(out)
def wr(a,data): n=ctypes.c_size_t(); return k.WriteProcessMemory(H,ctypes.c_void_p(a),data,len(data),ctypes.byref(n))
q=lambda a: struct.unpack('<Q',rd(a,8))[0]
i32=lambda a: struct.unpack('<i',rd(a,4))[0]
u32=lambda a: struct.unpack('<I',rd(a,4))[0]
i16=lambda a: struct.unpack('<h',rd(a,2))[0]
f32=lambda a: struct.unpack('<f',rd(a,4))[0]
def fs(a,n=3): return struct.unpack('<%df'%n,rd(a,4*n))
def cstr(a,n=128):
    b=rd(a,n); return b[:b.find(b'\0')].decode('ascii','replace')
def dump(path='h4live.img'):
    # The module as loaded (pointers still use the live base).
    open(path,'wb').write(rdp(BASE,SIZE))
if __name__=='__main__':
    print('pid',PID,'base',hex(BASE or 0),'size',hex(SIZE))

class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_ulonglong),('AllocationBase',ctypes.c_ulonglong),('AllocationProtect',W.DWORD),('__a',W.DWORD),('RegionSize',ctypes.c_ulonglong),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD),('__b',W.DWORD)]
k.VirtualQueryEx.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t]
def regions(min_addr=0x10000,max_addr=0x7fffffffffff,private_only=False):
    a=min_addr; m=MBI()
    while a<max_addr and k.VirtualQueryEx(H,ctypes.c_void_p(a),ctypes.byref(m),ctypes.sizeof(m)):
        ok=m.State==0x1000 and (m.Protect&0xEE) and not (m.Protect&0x100)
        if ok and (not private_only or m.Type==0x20000): yield m.BaseAddress,m.RegionSize
        a=m.BaseAddress+m.RegionSize
def search(pattern,limit=50,private_only=False,align=1):
    out=[]
    for base,size in regions(private_only=private_only):
        if size>0x40000000: continue
        try: data=rdp(base,size)
        except Exception: continue
        i=data.find(pattern)
        while i!=-1:
            if i%align==0:
                out.append(base+i)
                if len(out)>=limit: return out
            i=data.find(pattern,i+1)
    return out

# --- Game state (TLS) --------------------------------------------------------
TLS_INDEX_VAR=0x1057218  # RVA of halo4.dll's TLS index
def _threads():
    class TE(ctypes.Structure):
        _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('th32ThreadID',W.DWORD),('th32OwnerProcessID',W.DWORD),('tpBasePri',ctypes.c_long),('tpDeltaPri',ctypes.c_long),('dwFlags',W.DWORD)]
    s=k.CreateToolhelp32Snapshot(4,0); t=TE(); t.dwSize=ctypes.sizeof(t); out=[]
    ok=k.Thread32First(s,ctypes.byref(t))
    while ok:
        if t.th32OwnerProcessID==PID: out.append(t.th32ThreadID)
        ok=k.Thread32Next(s,ctypes.byref(t))
    return out
def _teb(tid):
    nt=ctypes.WinDLL('ntdll'); k.OpenThread.restype=W.HANDLE
    th=k.OpenThread(0x40,False,tid)
    class TBI(ctypes.Structure):
        _fields_=[('ExitStatus',ctypes.c_long),('TebBaseAddress',ctypes.c_ulonglong),('pid',ctypes.c_ulonglong),('tid',ctypes.c_ulonglong),('Aff',ctypes.c_ulonglong),('Pri',ctypes.c_long),('BasePri',ctypes.c_long)]
    b=TBI(); nt.NtQueryInformationThread(th,0,ctypes.byref(b),ctypes.sizeof(b),None); k.CloseHandle(th)
    return b.TebBaseAddress
_state=None
def state(slot_check=3):
    # The TLS block (game state slots) of the thread whose players/objects are set.
    global _state
    if _state: return _state
    idx=u32(BASE+TLS_INDEX_VAR); best=None
    for tid in _threads():
        try:
            teb=_teb(tid); tls=q(teb+0x58)
            if not tls: continue
            blk=q(tls+8*idx)
            if blk and q(blk+8*slot_check) and q(blk+8*1): best=blk; break
        except Exception: pass
    _state=best; return best
def slot(n): return q(state()+8*n)
# data arrays: entry size? data +0x50
def arr_entry(h,i,size): return q(h+0x50)+size*i
def obj(datum):
    e=q(slot(3)+0x50)+0x18*(datum&0xffff); return q(e+0x10)
def player_unit(i=0): return u32(q(slot(1)+0x50)+0xF18*i+0x2C)

# --- Tags ---------------------------------------------------------------------
def tag_resolve(addr):
    return q(BASE+0x496A180+8*(addr>>28))+addr*4 if addr else 0
def tag_get(datum):
    g,salt,addr=struct.unpack('<hHI',rd(BASE+0x28BCD50+8*(datum&0xffff),8))
    return tag_resolve(addr) if salt==(datum>>16) else 0
def _ih(): return q(q(BASE+0x28BCD30)+0x10+0x2E0)
def group_of(datum):
    g=i16(BASE+0x28BCD50+8*(datum&0xffff)); return struct.pack('>I',u32(q(_ih()+8)+16*g)).decode('latin1')
def block(tag,off,size,i):
    n=i32(tag+off); return tag_resolve(u32(tag+off+4))+size*i if 0<=i<n else 0
def sections(o):
    size=i16(o+0x1D0); off=struct.unpack('<H',rd(o+0x1D2,2))[0]
    return [o+off+0x18*k for k in range(size//0x18)]
_names=None
def mapname(): return rd(q(BASE+0x28BCD30)+0x10+0xB8,32).split(bytes(1))[0].decode()
def names(mapname=None):
    # Tag names by index from the loaded map's .map file (header +0x20.., mask +0x4C4).
    global _names
    if _names: return _names
    mapname = mapname or globals()['mapname']()
    p=r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo4\maps\%s.map'%mapname
    f=open(p,'rb'); hd=f.read(0x1E000); u=lambda o: struct.unpack_from('<I',hd,o)[0]
    m=u(0x4c4); cnt=u(0x20)
    f.seek((u(0x2c)+m)&0xffffffff); offs=struct.unpack('<%di'%cnt,f.read(4*cnt))
    f.seek((u(0x24)+m)&0xffffffff); s=f.read(u(0x28))
    _names=[s[o:s.find(b'\0',o)].decode('latin1') if 0<=o<len(s) else '' for o in offs]; return _names
def tags_of(group):
    cnt=i32(_ih()+0x10); d=rd(BASE+0x28BCD50,8*cnt); gt=q(_ih()+8); out=[]
    gm={}
    for i in range(cnt):
        g,salt,addr=struct.unpack_from('<hHI',d,8*i)
        if g<0 or not addr: continue
        if g not in gm: gm[g]=struct.pack('>I',u32(gt+16*g)).decode('latin1')
        if gm[g]==group: out.append(((salt<<16)|i, names()[i], tag_resolve(addr)))
    return out
