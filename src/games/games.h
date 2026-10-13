#pragma once

#include <windows.h>

#include "game.h"

// Tracks which MCC title is running and routes calls to it.
namespace Games {
    void OnModuleLoad(HMODULE module);
    void OnModuleUnload(HMODULE module);

    // Called every frame from the overlay.
    Status Tick(const Settings& settings, const FrameInput& input);

    // The title MCC is playing, or nullptr outside a game (for per-game
    // settings, chosen before Tick).
    const char* RunningTitle();

    // Called from MCC's input polling on the running title's main thread.
    void MainThreadTick();
}
