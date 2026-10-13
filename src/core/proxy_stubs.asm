; Each exported stub jumps straight to the matching function in the real
; System32\version.dll. Pointers are filled in by Proxy::Load (proxy.cpp),
; in the same order as kProxyExports.

EXTERN g_proxyTargets:QWORD

STUB MACRO name, idx
name PROC
    jmp QWORD PTR [g_proxyTargets + idx * 8]
name ENDP
ENDM

.code
STUB Stub_GetFileVersionInfoA,        0
STUB Stub_GetFileVersionInfoByHandle, 1
STUB Stub_GetFileVersionInfoExA,      2
STUB Stub_GetFileVersionInfoExW,      3
STUB Stub_GetFileVersionInfoSizeA,    4
STUB Stub_GetFileVersionInfoSizeExA,  5
STUB Stub_GetFileVersionInfoSizeExW,  6
STUB Stub_GetFileVersionInfoSizeW,    7
STUB Stub_GetFileVersionInfoW,        8
STUB Stub_VerFindFileA,               9
STUB Stub_VerFindFileW,               10
STUB Stub_VerInstallFileA,            11
STUB Stub_VerInstallFileW,            12
STUB Stub_VerLanguageNameA,           13
STUB Stub_VerLanguageNameW,           14
STUB Stub_VerQueryValueA,             15
STUB Stub_VerQueryValueW,             16
END
