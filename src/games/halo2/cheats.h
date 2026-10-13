#pragma once

#include <windows.h>

#include "games/fun.h"
#include "games/game.h"

// Halo 2 cheat logic.
namespace Halo2Cheats {
    void Load(HMODULE halo2);
    void Unload();

    // Render thread, every frame: memory, tag and patch edits.
    bool Apply(const Games::Settings& settings, const Games::FrameInput& input, Games::Status& status);

    // Game thread, once per game tick (before it runs): noclip, damage
    // multiplier, give weapon, rapid fire.
    void MainThread(const Games::Settings& settings, const Games::FrameInput& input);

    // Projectile-creation hook, around each barrel's shot: the chaos gun and
    // recoil push (games/fun.h), for the players' weapons and their vehicles'.
    void BeforeShot(uint32_t weapon, int16_t barrel, Fun::Shot& shot);

    // Game thread, with this tick's player input before the game applies it:
    // the aimbot's view pull.
}
