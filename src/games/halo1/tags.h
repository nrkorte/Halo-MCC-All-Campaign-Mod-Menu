#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "games/game.h"

// Reads Halo: CE tag data from memory. Callers must be inside SEH: pointers
// are only valid while a map is loaded.
namespace Halo1Tags {
    void SetModule(HMODULE halo1);  // nullptr on unload

    // Tag data for a tag datum, or nullptr.
    char* Get(uint32_t datum);

    // A loaded tag's group magic (e.g. 'vehi'), or 0.
    uint32_t GroupOf(uint32_t datum);

    // Converts a tag-space address (in tag entries and blocks) to a pointer.
    char* Resolve(int32_t address);

    // The loaded scenario and the first tag of a group (e.g. 'matg').
    char* Scenario();
    char* First(uint32_t groupMagic);

    // Every tag of one group with its short name, sorted by name. CE keeps
    // tag names in memory, so no .map file is read. Safe to call any time.
    std::vector<Games::TagChoice> ListGroup(uint32_t groupMagic);

    char* BlockElement(char* tag, int blockOffset, int elementSize, int index);
    int BlockCount(char* tag, int blockOffset);
}
