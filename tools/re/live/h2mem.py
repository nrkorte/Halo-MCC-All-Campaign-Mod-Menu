import ctypes, struct, math
from ctypes import wintypes as W
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.restype=W.HANDLE
k.ReadProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
k.WriteProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
def _find(dll=b'halo2.dll'):
    # MCC's PID and the title DLL's base, via a toolhelp snapshot.
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
        if m.szModule.lower()==dll: return pid,m.modBaseAddr
        ok=k.Module32Next(s,ctypes.byref(m))
    return pid,None
PID,BASE=_find(); H=k.OpenProcess(0x8|0x20|0x10|0x400,False,PID)
def rd(a,n):
    b=ctypes.create_string_buffer(n); r=ctypes.c_size_t()
    if not k.ReadProcessMemory(H,ctypes.c_void_p(a),b,n,ctypes.byref(r)): raise OSError(ctypes.get_last_error(),hex(a))
    return b.raw
def wr(a,data): n=ctypes.c_size_t(); return k.WriteProcessMemory(H,ctypes.c_void_p(a),data,len(data),ctypes.byref(n))
q=lambda a: struct.unpack('<Q',rd(a,8))[0]
i32=lambda a: struct.unpack('<i',rd(a,4))[0]
f32=lambda a: struct.unpack('<f',rd(a,4))[0]
def entry(h,i): return h+q(h+0x48)+i32(h+0x24)*i
def objtable(): return q(BASE+0x18B7398)
def objheap(): return (q(BASE+0x18B7360)+0x57)&~0xF
def obj(datum):
    e=entry(objtable(),datum&0xffff); return objheap()+i32(e+8)
def player_unit(i=0):
    p=q(BASE+0xE80A28); return struct.unpack('<I',rd(entry(p,i)+0x2C,4))[0]
def tagentry(i): return q(BASE+0x15E4B30)+i*16
def tagdata(datum): return q(BASE+0x15E4B38)+i32(tagentry(datum&0xffff)+8)
