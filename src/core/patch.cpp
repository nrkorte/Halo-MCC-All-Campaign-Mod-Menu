#include "patch.h"

#include <cstdlib>
#include <cstring>

namespace {
    // Parses "41 0F ?? 4C" into bytes plus a wildcard mask. Returns the length.
    size_t Parse(const char* pattern, uint8_t* bytes, bool* wild, size_t max) {
        size_t n = 0;
        for (const char* p = pattern; *p && n < max;) {
            if (*p == ' ') { ++p; continue; }
            if (*p == '?') {
                wild[n] = true;
                bytes[n++] = 0;
                while (*p == '?') ++p;
                continue;
            }
            char hex[3] = {p[0], p[1], 0};
            wild[n] = false;
            bytes[n++] = (uint8_t)strtoul(hex, nullptr, 16);
            p += 2;
        }
        return n;
    }
}

uint8_t* Patch::FindUnique(HMODULE module, const char* pattern) {
    uint8_t bytes[64];
    bool wild[64];
    size_t len = Parse(pattern, bytes, wild, sizeof(bytes));
    if (!module || len == 0) return nullptr;

    auto base = (uint8_t*)module;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    uint8_t* found = nullptr;
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* start = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize;
        for (size_t i = 0; i + len <= size; ++i) {
            size_t j = 0;
            while (j < len && (wild[j] || start[i + j] == bytes[j])) ++j;
            if (j < len) continue;
            if (found) return nullptr;  // not unique
            found = start + i;
        }
    }
    return found;
}

bool Patch::Nop::Init(uint8_t* address, size_t size) {
    uint8_t nops[sizeof(m_patch)];
    memset(nops, 0x90, sizeof(nops));
    return Init(address, nops, size);
}

bool Patch::Nop::Init(uint8_t* address, const uint8_t* replacement, size_t size) {
    if (!address || size > sizeof(m_original)) return false;
    m_address = address;
    m_size = size;
    m_applied = false;
    memcpy(m_original, address, size);
    memcpy(m_patch, replacement, size);
    return true;
}

void Patch::Nop::Apply(bool on) {
    if (!m_address || on == m_applied) return;

    DWORD old;
    if (!VirtualProtect(m_address, m_size, PAGE_EXECUTE_READWRITE, &old)) return;
    if (on) memcpy(m_address, m_patch, m_size);
    else memcpy(m_address, m_original, m_size);
    VirtualProtect(m_address, m_size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), m_address, m_size);
    m_applied = on;
}
