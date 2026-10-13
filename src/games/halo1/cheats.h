#pragma once

#include <windows.h>

#include "games/game.h"

// Halo: CE cheat logic.
namespace Halo1Cheats {
    // halo1.dll loaded: find the code patches (missing ones are logged).
    void Load(HMODULE halo1);
    // halo1.dll unloading: undo code patches, forget everything else.
    void Unload();

    // Render thread, every frame: memory, tag and patch edits. Returns false
    // if game state couldn't be read (e.g. mid level load).
    bool Apply(const Games::Settings& settings, Games::Status& status);

    // Game thread, once per game tick (before the tick runs): cheats that
    // call engine functions (noclip, damage multiplier, give weapon).
    void MainThread(const Games::Settings& settings, const Games::FrameInput& input);
}
