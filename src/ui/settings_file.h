#pragma once

#include <windows.h>

#include "games/game.h"
#include "menu_config.h"

// Menu settings kept across restarts in HaloModMenu.ini next to the DLL (the
// game's win64 folder): a [section] per game, one "name=value" line per
// setting. Each game keeps its own settings; a game with no section starts
// with everything off. The menu's own settings (look, bindings) are in a
// [Mod Menu] section shared by all games. Tag choices (give weapon, ammo type) aren't saved:
// their IDs change from map to map.
namespace SettingsFile {
    void Init(HMODULE self);
    void Load(Menu::Config& menu);
    // The running game changed (or the first one started): keeps `settings`
    // as the previous game's, then replaces them with `title`'s. Button
    // counters (give, save, load, spawn) carry over so nothing re-triggers.
    void SwitchGame(const char* title, Games::Settings& settings);
    // Writes the file if the current game's settings or the menu's differ
    // from what was last written.
    void SaveIfChanged(const Games::Settings& settings, const Menu::Config& menu);
}
