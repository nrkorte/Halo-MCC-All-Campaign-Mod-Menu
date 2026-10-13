#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "games/game.h"

// Reads ODST tag data (the loaded map's definitions: weapons, bipeds,
// globals, scenario) from memory. Callers must be inside SEH: pointers are
// only valid while a map is loaded.
namespace OdstTags {
    void SetModule(HMODULE odst);  // nullptr on unload

    // Tag data for a tag datum (e.g. an object's definition), or nullptr.
    char* Get(uint32_t datum);

    // Tag data for a map-wide tag such as 'matg' or 'scnr', or nullptr.
    char* GetGlobal(uint32_t groupMagic);

    // Reads tag names from the loaded map's .map file (they aren't kept in
    // memory). Cached per map. Returns false if the names couldn't be read.
    bool LoadNames();

    // Every tag of one group ('weap', 'proj', ...) with its short name,
    // sorted by name. Safe to call any time.
    std::vector<Games::TagChoice> ListGroup(uint32_t groupMagic);

    // A loaded tag's group magic ('vehi'), or 0.
    uint32_t GroupOf(uint32_t datum);
    // Its full path from the .map, or "" (LoadNames reads them).
    std::string FullName(uint32_t datum);
    // A string id's text ("gauss"), or "" (LoadNames reads them).
    std::string StringName(uint32_t stringId);

    // Element `index` of a tag block at `tag + blockOffset`, or nullptr.
    char* BlockElement(char* tag, int blockOffset, int elementSize, int index);
    int BlockCount(char* tag, int blockOffset);
}
