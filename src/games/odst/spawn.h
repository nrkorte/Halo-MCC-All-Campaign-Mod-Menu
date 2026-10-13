#pragma once

#include <windows.h>

#include "cheat_util.h"
#include "games/game.h"

// ODST spawning (the Spawning page): the lists, and creating the selection
// where player 1 aims.
namespace OdstSpawn {
    void SetModule(HMODULE odst);  // nullptr on unload
    void Reset();                   // map gone: forget lists, followed AI, zones

    // Render thread: rebuilds the lists for a newly loaded scenario, and
    // points the menu at them (plus the latest spawn result).
    void RefreshLists(char* scnr);
    void FillStatus(Games::Status& status);

    // Game thread, once per frame: handles the spawn key, zone loads it's
    // waiting on, and spawned AI after death.
    void Tick(void** state, const OdstCheats::Players& p, Blam::DataArray* objects, const Games::Settings& s);
    // Game thread, once per tick: the Fun, Chaos and Skulls pages (games/fun.h).
    void FunTick(void** state, const OdstCheats::Players& p, Blam::DataArray* objects, const Games::Settings& s,
                 float dt);
}
