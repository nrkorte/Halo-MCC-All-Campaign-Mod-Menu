#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "MinHook.h"

#include "core/log.h"
#include "core/proxy.h"
#include "mcc/mcc.h"
#include "mcc/offsets.h"
#include "ui/overlay.h"
#include "ui/settings_file.h"

// Reads a module's file version from its version resource. (We can't call
// GetFileVersionInfo here: we *are* version.dll.)
static bool GetModuleVersion(HMODULE module, wchar_t* out, size_t outLen) {
    HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(1), RT_VERSION);
    if (!res) return false;
    HGLOBAL data = LoadResource(module, res);
    auto bytes = (const BYTE*)LockResource(data);
    DWORD size = SizeofResource(module, res);
    if (!bytes) return false;

    for (DWORD i = 0; i + sizeof(VS_FIXEDFILEINFO) <= size; i += 4) {
        auto info = (const VS_FIXEDFILEINFO*)(bytes + i);
        if (info->dwSignature != 0xFEEF04BD) continue;
        swprintf_s(out, outLen, L"%u.%u.%u.%u",
                   HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                   HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        return true;
    }
    return false;
}

static DWORD WINAPI InitThread(LPVOID) {
    wchar_t version[32] = L"unknown";
    GetModuleVersion(GetModuleHandleW(nullptr), version, _countof(version));
    LOG("HaloModMenu starting. MCC version %ls (supported: %ls)", version, SUPPORTED_MCC_VERSION);

    if (wcscmp(version, SUPPORTED_MCC_VERSION) != 0) {
        LOG("Version mismatch; mod disabled. Offsets need updating for this game version.");
        MessageBoxW(nullptr,
                    L"HaloModMenu was built for MCC " SUPPORTED_MCC_VERSION L" and this game is a different "
                    L"version.\n\nThe mod is disabled for this session so it can't crash the game.",
                    L"HaloModMenu", MB_OK | MB_ICONWARNING);
        return 0;
    }

    if (MH_Initialize() != MH_OK) {
        LOG("MinHook failed to initialize");
        return 0;
    }

    MCC::InstallHooks();
    Overlay::InstallHooks();
    return 0;
}

static bool IsMccProcess() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    return name && _wcsicmp(name + 1, L"MCC-Win64-Shipping.exe") == 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        if (!Proxy::Load()) return FALSE;

        // Anything else in this folder that loads version.dll just gets the proxy.
        if (IsMccProcess()) {
            Log::Init(module);
            SettingsFile::Init(module);
            CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        }
    }
    return TRUE;
}
