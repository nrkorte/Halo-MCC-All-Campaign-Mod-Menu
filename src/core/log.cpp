#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <mutex>

static FILE* g_file = nullptr;
static std::mutex g_mutex;

void Log::Init(HMODULE self) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"HaloModMenu.log");

    // The previous session's log is kept as HaloModMenu.prev.log, so a crash's
    // log survives MCC being restarted.
    wchar_t prev[MAX_PATH];
    wcscpy_s(prev, path);
    wchar_t* dot = wcsrchr(prev, L'.');
    if (dot) {
        wcscpy_s(dot, MAX_PATH - (dot - prev), L".prev.log");
        MoveFileExW(path, prev, MOVEFILE_REPLACE_EXISTING);
    }

    // Fresh log each launch. Shared for reading so it can be opened while MCC runs.
    g_file = _wfsopen(path, L"w", _SH_DENYWR);
}

int Log::Exception(EXCEPTION_POINTERS* info, const char* where) {
    static const char* s_where[32];
    static int s_count[32];

    int slot = 0;
    while (slot < 31 && s_where[slot] && s_where[slot] != where) ++slot;
    s_where[slot] = where;
    if (s_count[slot]++ < 3) {
        auto rec = info->ExceptionRecord;
        HMODULE module = nullptr;
        wchar_t name[MAX_PATH] = L"?";
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)rec->ExceptionAddress, &module);
        if (module) GetModuleFileNameW(module, name, MAX_PATH);
        const wchar_t* slash = wcsrchr(name, L'\\');
        const wchar_t* shortName = slash ? slash + 1 : name;
        Write("Exception in %s: code %08X at %ls+0x%llX (access %s %p)", where, (unsigned)rec->ExceptionCode, shortName,
              module ? (unsigned long long)((char*)rec->ExceptionAddress - (char*)module) : 0ull,
              rec->NumberParameters >= 2 ? (rec->ExceptionInformation[0] ? "write" : "read") : "-",
              rec->NumberParameters >= 2 ? (void*)rec->ExceptionInformation[1] : nullptr);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void Log::Write(const char* fmt, ...) {
    if (!g_file) return;

    std::lock_guard<std::mutex> lock(g_mutex);

    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);

    fputc('\n', g_file);
    fflush(g_file);
}
