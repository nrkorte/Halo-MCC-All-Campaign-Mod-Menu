# HaloModMenu

A Halo MCC (Steam) runtime mod: an in-game ImGui cheat menu that works across every MCC title. Halo: CE, Halo 2, Halo 3, Halo 3: ODST, Halo: Reach and Halo 4 campaigns have cheats; Halo 2: Anniversary multiplayer appears in the menu as "not supported yet". The table below describes the Halo 3 versions; `NOTES.md` covers how each title differs.

Supports **MCC 1.3528.0.0 only**. On any other version it shows a warning and does nothing.

## Build and install

```
build.bat               -> build\Release\version.dll
powershell -File install.ps1              (copies it into the game folder)
powershell -File install.ps1 -Uninstall   (removes it)
```

Launch MCC with **anti-cheat disabled**. Remove the mod before playing with anti-cheat on.

**Desktop toggle:** `tools\ToggleHaloModMenu\build.bat` builds `Toggle-HaloModMenu.exe` and copies it to the Desktop. Each run flips the mod:
- **Off:** renames `version.dll` to `version.dll.disabled`.
- **On:** copies the latest `build\Release\version.dll`.

It finds MCC through Steam's library list, and it won't touch a `version.dll` that isn't HaloModMenu.

## Use

| | Controller | Keyboard / mouse |
|---|---|---|
| Open menu | LT + right stick click | F6 |
| Move | D-pad or left stick | mouse, arrow keys |
| Select / toggle | X | click, Space |
| Back to the main page | Y | Backspace |
| Close menu | B | F6 |

These are the defaults; the open, select, back and close buttons can be rebound in **Menu Settings** (the open shortcut can be 1 to 4 buttons or keys pressed together).

- While the menu is open, the game gets no input: keyboard, mouse and controller are all blocked.
- After the menu closes, input stays blocked until you let go of every button, so the B press doesn't reach the game.
- A startup banner shows for 15 seconds, and the corner lists the cheats that are on (both can be hidden in Menu Settings).
- **Menu Settings** (last category on the main page): theme and colours, width, max height, font and size, where the menu opens, button bindings, and the overlay options. Shared by all games.
- **Reset:** on the main page, "Reset all" resets everything for the current game; inside a category, "Reset page" resets just that page.
- A log is written to `HaloModMenu.log` in `mcc\binaries\win64`.
- Cheat settings are saved per game to `HaloModMenu.ini` in the same folder (a section per game) and come back the next time MCC starts. A game you haven't modded starts with everything off. The menu's own settings go in its `[Mod Menu]` section. Delete the file to start from defaults.

## Local co-op

The **Co-op** page turns on splitscreen for every title (2-4 local players, a controller each, optional keyboard and mouse for player 1). It is ported from [AlphaRing](https://github.com/WinterSquire/AlphaRing) by WinterSquire; see the **Credits** page. Restart the mission or load a checkpoint after changing it. The menu also opens on MCC's own screens (same buttons) with just the co-op settings, which apply to every title; do that before starting a CE mission.

## Halo 3 cheats

| Cheat | How it works |
|---|---|
| God mode | Sets the player unit's "cannot take damage" flag |
| Infinite ammo | Keeps the player's reserve rounds full, using the weapon tag's magazine sizes |
| Bottomless clip | Keeps the loaded rounds full. Energy weapons: resets battery drain |
| Infinite grenades | Keeps each grenade type at the globals tag's maximum |
| One-hit kill | Enemy AI bipeds get no shield and 1% health. Allies (player and human teams) and scripted-invulnerable characters are skipped |
| Speed | Scales the walk, run and acceleration values in the globals tag's Player Information block |
| Jump height | Scales the player biped tag's jump velocity |
| Fly | Turns on MCC's Acrophobia skull (built-in flight) |
| Noclip | Holds you in place with the engine's teleport each tick. Hold RB (or G) to fly where you're looking, through anything. Shift for fast |
| Gravity | Scales the world gravity constant (affects everything) |
| Game speed | Scales game time (slow-mo or fast-forward) |
| Remove boundaries *(experimental)* | Hides the scenario's kill trigger volumes and makes soft ceilings ignore everything |
| Damage to enemies | Scales health and shield lost by enemy AI each tick. Calls the engine's `unit_kill` when a scaled hit is fatal |
| Aimbot | Maxes your weapons' built-in autoaim angle, range and deviation, so shots bend onto targets |
| Aimbot strength | 0% = bending only. Higher pulls your view, while you're firing, onto the enemy nearest your crosshair; 100% snaps to any enemy on screen (walls aren't checked) |
| Ammo type | Your held weapon's barrels fire the chosen projectile (every weapon of that type does, until switched off) |
| Give weapon | Creates the chosen weapon with the engine's own functions and puts it in place of your held one |
| Enemies can't see you | Sets the AI-disregard flag on your unit, and applies the Cheat Engine table's perception patch |
| AI can't shoot | Code patch from the Cheat Engine table that makes AI skip firing. Also turns off AI grenades |
| AI can't hear | Code patch that gives AI a hearing range of zero, so gunfire and footsteps don't alert them (applies to every sound) |
| Full-auto | Semi-auto weapons keep firing while the trigger is held. Your weapons only |
| Rapid fire | Fires every game tick, ignoring the weapon's rate of fire. Your weapons only |
| No spread | Keeps bullet spread at its minimum. Your weapons only |
| No recoil | Code patch that skips the view kick after a shot (recoil only ever applies to players) |
| No overheat | Keeps plasma weapon heat at zero. Your weapons only |
| Infinite equipment | Your equipment stays in your hands after you use it (usable again once its effect ends) |
| No equipment despawn | Equipment you deploy (bubble shield, regenerator, ...) doesn't expire. Enemies' equipment is unaffected |
| Third person | Switches your camera to the game's third-person following camera; cutscene and death cameras are left alone |
| Camera distance | Broken for now: doesn't move the camera yet |
| No HUD | Hides the HUD, the same as the game's chud_show script command |

Every cheat restores the original values when switched off. Tag edits are applied again after a map or checkpoint reload.

Cheats that call engine functions (noclip, damage, give weapon) run on the game's main thread. The mod gets called there through MCC's input polling (`get_key_state`), which each title calls every tick.

The code patches are found by byte pattern (`core/patch.cpp`), so they survive small updates. They come from the Cheat Engine table in `Documents\My Cheat Tables\mcc-win64-shipping.CT`, which was made for the Windows Store build. It has scripts for every title.

## Finding offsets

Offsets in `src/games/halo3/offsets.h` each note the `halo3.dll` address (RVA) where they were confirmed. Most came from disassembling the engine's own HaloScript functions:
- **Function table:** RVA `0x7F6AD0`, 1,379 entries in the order of Assembly's `H3MCC_U14_Scripting.xml`.
- **Globals table:** RVA `0x7D36A0`, 1,246 entries.

Each function definition keeps its evaluator at `+0x20`. The retail `cheat_*` HaloScript globals have no storage (their address field is 0), so they can't be used.

## Layout

| Folder | What's in it |
|---|---|
| `src/core/` | `VERSION.dll` proxy (forwards every export to System32) and logging |
| `src/mcc/` | Hooks into MCC itself, shared by all titles: title DLL load/unload, plus input blocking through the game manager's `get_key_state` (every title reads input through it) |
| `src/games/` | One class per title (`game.h`). `games.cpp` lists all seven and routes to whichever is running. `blam.h` holds engine structures shared by Halo 3 and later |
| `src/games/halo3/` | Halo 3: `halo3.cpp` has the hooks, `cheats.cpp` the cheat logic, `tags.cpp` tag lookup, and `offsets.h` the offsets |
| `src/games/halo1/`, `halo2/`, `odst/`, `reach/`, `halo4/` | The other titles, laid out the same way (`halo4/` is a port of `reach/`) |
| `tools/re/` | Reverse-engineering scripts per title (disassembly images, script-table readers, live memory helpers in `tools/re/live/`) |
| `src/ui/` | D3D11 `Present` hook with the ImGui menu (`overlay.cpp`), and XInput controller reading (`gamepad.cpp`) |

**Adding a title:**
1. Create `src/games/<title>/` with a `Game` subclass and its offsets.
2. Swap it in for the placeholder `Game` in `games.cpp`, and add the `.cpp` to `CMakeLists.txt`.

Offsets come from AlphaRing's 1.3528.0.0 tables (`AlphaRing-fork/lib/game/inc/1.3528.0.0`), which also cover the other titles. After a game update they must be found again.

Third-party code, vendored in `third_party/`: Dear ImGui v1.91.9 and MinHook (master, 2026-09-08).
