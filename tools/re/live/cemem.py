import ctypes, struct
from ctypes import wintypes as W
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.restype=W.HANDLE
k.ReadProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
k.WriteProcessMemory.argtypes=[W.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
def _find(dll=b'halo1.dll'):
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
def wr(a,data): n=ctypes.c_size_t(); return k.WriteProcessMemory(H,ctypes.c_void_p(a),data,len(data),ctypes.byref(n))
def rd(a,n):
    b=ctypes.create_string_buffer(n); r=ctypes.c_size_t()
    if not k.ReadProcessMemory(H,ctypes.c_void_p(a),b,n,ctypes.byref(r)): raise OSError(ctypes.get_last_error(),hex(a))
    return b.raw
q=lambda a: struct.unpack('<Q',rd(a,8))[0]
i32=lambda a: struct.unpack('<i',rd(a,4))[0]
i16=lambda a: struct.unpack('<h',rd(a,2))[0]
def arr_entry(h,i): return h+i32(h+0x34)+struct.unpack('<H',rd(h+0x22,2))[0]*i
def obj(datum):
    h=q(BASE+0x1C42248); e=arr_entry(h,datum&0xffff)
    return q(BASE+0x2D9CDF8)+0x34+i32(e+8)
def player_unit():
    p=q(BASE+0x1C40480); return struct.unpack('<I',rd(arr_entry(p,0)+0x64,4))[0]
def tagdata(datum):
    ents=q(BASE+0x1C34FB0); e=ents+(datum&0xffff)*0x20
    a=i32(e+0x14); return q(BASE+0x2D9CE10)+(a-q(BASE+0x2EA3410))
def resolve(a): return q(BASE+0x2D9CE10)+(a-q(BASE+0x2EA3410))
