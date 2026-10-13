#pragma once

// Structures shared by the Blam engine titles (Halo 3 and later use the same
// layouts; verify before reusing for another title).

#include <windows.h>
#include <intrin.h>

#include <cstdint>

namespace Blam {
    // "Data array": a fixed pool of same-sized entries. Each entry starts with
    // a 16-bit salt; 0 means the slot is free. A datum (handle) is
    // (salt << 16) | index.
    struct DataArray {
        char name[0x20];
        int32_t maxCount;     // 0x20
        int32_t entrySize;    // 0x24
        char pad0[0x14];
        int32_t highWater;    // 0x3C  entries in use are below this
        int32_t count;        // 0x40
        char pad1[0x4];
        char* data;           // 0x48

        char* Entry(int index) const { return data + (int64_t)entrySize * index; }
    };

    // Object table entries point at the real object data.
    struct ObjectEntry {
        uint16_t salt;
        char pad[0xE];
        char* object;         // 0x10
    };

    // Resolves an object datum to its data, or nullptr if stale/invalid.
    inline char* ObjectFromDatum(const DataArray* objects, uint32_t datum) {
        if (datum == 0xFFFFFFFF) return nullptr;
        uint16_t index = datum & 0xFFFF;
        if (index >= objects->highWater) return nullptr;
        auto entry = (const ObjectEntry*)objects->Entry(index);
        if (entry->salt != (datum >> 16)) return nullptr;
        return entry->object;
    }

    // These titles keep game state in thread-local storage. This is the TLS
    // slot index the module was assigned by the loader.
    inline int32_t ModuleTlsIndex(HMODULE module) {
        auto base = (char*)module;
        auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if (dir.VirtualAddress == 0) return -1;
        auto tls = (IMAGE_TLS_DIRECTORY*)(base + dir.VirtualAddress);
        return *(int32_t*)tls->AddressOfIndex;
    }

    // The calling thread's TLS block for that module.
    inline void** ThreadTlsBlock(int32_t tlsIndex) {
        auto tlsArray = (void***)__readgsqword(0x58);  // TEB->ThreadLocalStoragePointer
        return tlsArray[tlsIndex];
    }
}
