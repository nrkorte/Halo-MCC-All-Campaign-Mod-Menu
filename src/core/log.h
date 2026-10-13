#pragma once

#include <windows.h>

// Appends lines to HaloModMenu.log next to the DLL (the game's win64 folder).
namespace Log {
    void Init(HMODULE self);
    void Write(const char* fmt, ...);
}

#define LOG(...) Log::Write(__VA_ARGS__)

// SEH filter that logs what faulted and where, then handles it. Logs at most
// a few times per `where` string so a repeating fault can't flood the log:
//   __except (Log::Exception(GetExceptionInformation(), "noclip")) { ... }
namespace Log {
    int Exception(EXCEPTION_POINTERS* info, const char* where);
}
