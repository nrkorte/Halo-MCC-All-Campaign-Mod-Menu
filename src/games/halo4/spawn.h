#pragma once

#include <windows.h>

#include "cheat_util.h"
#include "games/game.h"

// Halo 4 spawning (the Spawning page): the lists, and creating the selection
// where player 1 aims. A port of Halo 3's (halo3/spawn.cpp) with Reach's
// actor requests.
namespace Halo4Spawn {
    void SetModule(HMODULE halo4);  // nullptr on unload
    void Reset();                   // map gone: forget lists and followed AI

    // Render thread: rebuilds the lists for a newly loaded scenario, and
    // points the menu at them (plus the latest spawn result).
    void RefreshLists(char* scnr);
    void FillStatus(Games::Status& status);

    // Game thread, once per tick: handles the spawn key and spawned AI after
    // death.
    void Tick(void** state, const Halo4Cheats::Players& p, const Halo4Cheats::Array& objects,
              const Games::Settings& s);
    // Game thread, once per tick: the Fun, Chaos and Skulls pages (games/fun.h).
    void FunTick(void** state, const Halo4Cheats::Players& p, const Halo4Cheats::Array& objects,
                 const Games::Settings& s, float dt);
}
