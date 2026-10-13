#pragma once

#include <windows.h>

#include <string>

// Local co-op / splitscreen for every title. Ported from AlphaRing by
// WinterSquire (github.com/WinterSquire/AlphaRing), with the controller
// support and saved settings from nrkorte's continuation
// (github.com/nrkorte/AlphaRing-Local-Co-Op-Mod). It does two things:
//  - per-title code patches that let more than one local player join, and
//  - hooks on MCC's game manager that give players 2-4 their own controller
//    and identity (player 1 keeps the real profile).
// Settings are shared by all titles and saved in HaloModMenu.ini ([Co-op]).
namespace Coop {
    struct Settings {
        bool enabled = false;
        int players = 2;          // 1-4 local players
        bool p1KeyMouse = false;  // player 1 on keyboard + mouse, players 2-4 take controllers 1-3
        int controller[4] = {0, 1, 2, 3};  // per player: 0-3 = controller 1-4, 4 = none
        bool removeBlackBars = true;       // Halo 3, ODST, Reach: use the whole screen
    };

    Settings& Get();
    void Reset();  // back to defaults (the same defaults as a fresh install)
    void AssignDefaultControllers();  // player N -> controller N (or shifted for K/M)

    std::string Serialize();
    void Parse(const std::string& text);

    // Game manager table slots and the player-index function, hooked once by
    // mcc.cpp when the first title loads.
    bool InstallHooks(void** gameManagerTable);

    // A title's DLL loaded or unloaded (patches go in and out with them).
    void OnModuleLoad(HMODULE module);
    void OnModuleUnload(HMODULE module);

    // Asks the game to re-read each player's profile and button layout (they
    // are loaded when a player joins; after that this picks up changes).
    void ReloadSettings();

    // True when co-op is on and the hooks are in place.
    bool Active();

    // Called every frame from the overlay: brings the code patches in line
    // with the settings.
    void Sync();

    // Replaces the game's get_key_state while co-op is on. `original` is the
    // game's own function (or the next hook in the chain).
    using GetKeyStateFn = bool(__fastcall*)(void* self, DWORD index, void* input);
    bool GetKeyState(void* self, DWORD index, void* input, GetKeyStateFn original);
}
