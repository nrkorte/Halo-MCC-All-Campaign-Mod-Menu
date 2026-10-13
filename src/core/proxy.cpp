#include "proxy.h"

#include <windows.h>

#include <cstdlib>

// Order must match the STUB indices in proxy_stubs.asm.
static const char* const kProxyExports[] = {
    "GetFileVersionInfoA",
    "GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA",
    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW",
    "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",
    "VerFindFileA",
    "VerFindFileW",
    "VerInstallFileA",
    "VerInstallFileW",
    "VerLanguageNameA",
    "VerLanguageNameW",
    "VerQueryValueA",
    "VerQueryValueW",
};

extern "C" void* g_proxyTargets[_countof(kProxyExports)] = {};

bool Proxy::Load() {
    wchar_t path[MAX_PATH];
    UINT len = GetSystemDirectoryW(path, MAX_PATH);
    if (len == 0 || len > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\version.dll");

    HMODULE real = LoadLibraryW(path);
    if (!real) return false;

    for (size_t i = 0; i < _countof(kProxyExports); ++i)
        g_proxyTargets[i] = (void*)GetProcAddress(real, kProxyExports[i]);

    return true;
}
