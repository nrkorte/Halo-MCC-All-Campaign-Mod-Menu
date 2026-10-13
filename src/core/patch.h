#pragma once

#include <windows.h>

#include <cstdint>

// Byte-pattern search and reversible code patches, for cheats that change
// game code rather than data (patterns like Cheat Engine's aobscanmodule).
namespace Patch {
    // Finds a pattern like "41 0F 2F ?? 24 14" in a module's code. Returns
    // nullptr if it's missing or appears more than once.
    uint8_t* FindUnique(HMODULE module, const char* pattern);

    // Replaces `size` bytes at `address` with NOPs (or given bytes) and can
    // put them back.
    class Nop {
    public:
        bool Init(uint8_t* address, size_t size);  // false if address is null or size too big
        bool Init(uint8_t* address, const uint8_t* replacement, size_t size);
        void Apply(bool on);                       // idempotent
        bool Ready() const { return m_address != nullptr; }
        void Forget() { m_address = nullptr; m_applied = false; }  // module unloaded

    private:
        uint8_t* m_address = nullptr;
        size_t m_size = 0;
        uint8_t m_original[16] = {};
        uint8_t m_patch[16] = {};
        bool m_applied = false;
    };
}
