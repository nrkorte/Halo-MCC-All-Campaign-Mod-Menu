#pragma once

#include <cstdint>

// Halo 3 extras (extras.cpp): what other Halo 3 files may ask of it.
namespace Halo3Extras {
    // True while time stop is on and this object belongs to a frozen AI (its unit, held
    // weapons and equipment, a vehicle it rides unless a player is in it). The table is
    // rebuilt every game tick on the game thread. Game thread only.
    bool IsFrozenObject(uint32_t datum);
}
