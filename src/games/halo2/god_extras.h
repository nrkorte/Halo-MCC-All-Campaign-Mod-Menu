#pragma once

#include "cheat_util.h"

// Halo 2 god mode extras: the player (and the vehicle they're in) survive
// scenario kill volumes (falling out of the world). See god_extras_offsets.h.
namespace Halo2God {
    void Load(char* module);
    void Unload();
    // Render thread, every frame: who is protected right now.
    void Tick(bool godMode, const Halo2Cheats::Players& players, const Halo2Cheats::Objects& objects);
}
