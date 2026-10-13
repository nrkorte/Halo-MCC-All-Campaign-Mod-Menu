#pragma once

#include <windows.h>

#include "games/fun.h"
#include "games/game.h"

// Halo 4 cheat logic (a port of the Reach code; see offsets.h).
namespace Halo4Cheats {
    // halo4.dll loaded: find the code patches. Missing patches just leave
    // their cheats inactive (logged).
    void Load(HMODULE halo4);
    // halo4.dll unloading: undo code patches, forget everything else.
    void Unload();

    // Render thread, every frame: memory and tag edits. Returns false if game
    // state couldn't be read (e.g. mid level load).
    bool Apply(void** gameState, const Games::Settings& settings, Games::Status& status);

    // Game thread, once per game tick (before the tick runs): cheats that
    // call engine functions (noclip, damage multiplier, give weapon, aimbot pull).
    void MainThread(void** gameState, const Games::Settings& settings, const Games::FrameInput& input);

    // Barrel-fire hook: false if this weapon may not fire (AI can't shoot).
    bool WeaponMayFire(void** gameState, uint32_t weapon);

    // Flip-eject hook (the flip check's eject action): false to drop it
    // ("Cannot be ejected" keeps players in a flipped vehicle).
    bool FlipEjectAllowed(void** gameState, uint32_t unit);

    // Barrel-fire hook, around each shot: the chaos gun and recoil push
    // (games/fun.h), for the players' weapons and their vehicles' guns.
    void BeforeShot(void** gameState, uint32_t weapon, int16_t barrel, Fun::Shot& shot);

    // Projectile-lookup hook (0x6826E0): the chaos gun's random projectile
    // for a player's weapon, or -1 to keep the weapon's own.
    uint32_t ChaosProjectileFor(void** gameState, uint32_t weapon);

    // Teleport moved player 1: noclip holds them at the new spot.
    void MoveNoclipAnchor(const float* position);
}
