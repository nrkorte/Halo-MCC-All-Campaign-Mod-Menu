#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "games/game.h"

// Reads Halo 2 tag data from memory. Callers must be inside SEH: pointers
// are only valid while a map is loaded.
namespace Halo2Tags {
    void SetModule(HMODULE halo2);  // nullptr on unload

    char* Get(uint32_t datum);

    // A loaded tag's group magic (e.g. 'vehi'), or 0.
    uint32_t GroupOf(uint32_t datum);
    // Its full path from the .map ("objects\characters\dervish\dervish"), or "".
    std::string FullName(uint32_t datum);
    // A string id's text from the .map ("troop"), or "". LoadNames reads them.
    std::string StringName(uint32_t id);
    char* First(uint32_t groupMagic);

    // Reads tag names from the loaded map's .map file, decompressing the
    // chunks that hold them. Cached per map.
    bool LoadNames();

    // Every tag of one group with its short name, sorted by name.
    std::vector<Games::TagChoice> ListGroup(uint32_t groupMagic);

    char* BlockElement(char* tag, int blockOffset, int elementSize, int index);
    int BlockCount(char* tag, int blockOffset);
}
