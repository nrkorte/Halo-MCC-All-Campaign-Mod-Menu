# HaloModMenu

An in-game cheat and sandbox menu for the **campaigns** of Halo: The Master Chief Collection (Steam). It covers Halo: CE, Halo 2, Halo 3, Halo 3: ODST, Halo: Reach and Halo 4, and it's controller and keyboard friendly. Multiplayer isn't supported and isn't planned.

It loads as a `version.dll` proxy next to the MCC executable, so there's no injector and nothing to run alongside the game. It needs **anti-cheat disabled** (launch "Play Halo: MCC (Anti-Cheat Disabled)"), so don't use it online.

Supports **MCC 1.3528.0.0 only**. On any other version it shows a warning and does nothing, because every offset is version specific.

## Windows Defender and antivirus

Defender may flag or quarantine the DLL. The detection seen so far is a machine-learning one (`Program:Win32/Contebrew.A!ml`), which is a generic guess, not a match on known malware. The cause is how the mod works, and none of it is hidden:

- It's a proxy `version.dll`, the same trick used by ReShade, ASI loaders and many other game mods, and also by malware (DLL side-loading), so it scores badly on heuristics.
- It patches game memory and installs hooks (MinHook) in the game's own code, as every trainer does.
- It's unsigned and new, so it has no download reputation yet.

What you can do:

- **Build it yourself.** The full source is here and the build is one command (see below). You don't have to trust a binary.
- Add a Defender exclusion for the **MCC `mcc\binaries\win64` folder only** (Windows Security > Virus & threat protection > Manage settings > Exclusions), not your whole drive. Remove the exclusion if you uninstall.
- If a release is quarantined, Defender's "Protection history" lets you restore it, and a false-positive report can be sent to Microsoft at the Security Intelligence submission page.

The mod only touches the MCC process, and it only writes `HaloModMenu.log` and `HaloModMenu.ini` next to the DLL. It makes no network connections.

## Download and verify

Releases on the [Releases page](../../releases) are built by GitHub Actions ([workflow](.github/workflows/build.yml)) from the tagged source, not on a personal machine. Each release lists the SHA-256 of `version.dll` and carries a signed build attestation, so you can confirm the file came from this repo's source:

```
gh attestation verify version.dll --repo nrkorte/Halo-MCC-All-Campaign-Mod-Menu
```

Unzip the release, copy `version.dll` into `mcc\binaries\win64` in your MCC folder (or use `install.ps1`), and launch with anti-cheat disabled.

## Build and install

Requires Visual Studio 2022 with the "Desktop development with C++" workload and the "C++ CMake tools for Windows" component.

```
build.bat                                  -> build\Release\version.dll
powershell -File install.ps1 -GameDir "<MCC>\mcc\binaries\win64"   (copies it into the game folder)
powershell -File install.ps1 -GameDir "<MCC>\mcc\binaries\win64" -Uninstall   (removes it)
```

`-GameDir` defaults to the author's Steam library path, so pass your own. Close MCC before installing, and remove the mod before playing with anti-cheat on.

**Desktop toggle:** `tools\ToggleHaloModMenu\build.bat` builds `Toggle-HaloModMenu.exe` and copies it to the Desktop. Each run flips the mod:
- **Off:** renames `version.dll` to `version.dll.disabled`.
- **On:** copies the latest `build\Release\version.dll`.

It finds MCC through Steam's library list, and it won't touch a `version.dll` that isn't HaloModMenu.

## Status

All six campaign titles have the core cheat set, spawning and the Fun page. Features were ported from title to title (Halo 3 first, then Reach, CE, ODST, Halo 4 and Halo 2), so test coverage varies:

| Title | State |
|---|---|
| Halo 3, Halo: Reach, Halo: CE, ODST, Halo 4 | Core cheats, vehicles and spawning confirmed working in game |
| Halo 2 | Core cheats, vehicles and spawning confirmed; some newer Fun/Chaos features still being tested |
| Newer additions (FOV slider, explosion size, grenade drop, reticle dot) | Rolling out title by title; check `NOTES.md` for what's done |

Not every option exists in every game. Options a title can't support are greyed out in the menu (for example Fly and equipment in titles without them).

`NOTES.md` is the detailed handoff: per-title differences, what was tested, known issues and the reverse-engineering notes.

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
- **Reset:** on the main page, "Reset all" resets everything for the current game; inside a category, "Reset page" resets just that page.
- A log is written to `HaloModMenu.log` in `mcc\binaries\win64` (the previous session's is kept as `HaloModMenu.prev.log`). Please attach it to bug reports.
- Cheat settings are saved per game to `HaloModMenu.ini` in the same folder and come back the next time MCC starts. A game you haven't modded starts with everything off. Delete the file to start from defaults.

## Menu pages

The main page lists categories; each is a submenu.

| Page | What's on it |
|---|---|
| Player | God mode, speed, jump, fly, noclip, third person and camera distance, no HUD, FOV, change your team |
| Weapons | Infinite ammo, bottomless clip, infinite grenades, ammo type, give weapon, full-auto, rapid fire, no spread, no recoil, no overheat, aimbot |
| Combat | One-hit kill, damage multiplier, friendly fire and betrayal options |
| Equipment | Infinite equipment, no despawn, give equipment (titles that have equipment) |
| AI | Enemies can't see, hear or shoot you, upgrade AI, random AI teams, squads, nuke and despawn custom AI |
| World | Gravity, game speed, remove boundaries, time stop, checkpoints (save, load, lock, history) |
| Vehicles | Vehicle speed, god mode, rapid fire, no overheat, infinite boost, cannot be hijacked, shut down or ejected |
| Spawning | Spawn AI, squads, weapons and vehicles from the map's own tags, as ally or enemy, with a team picker |
| Fun / Chaos / Skulls | Launch on hit, giant and tiny AI, double or infinite jump, poison touch, recoil push, teleport, perfect camo and other joke options, plus skull toggles |
| Co-op | Local splitscreen for every title (see below) |
| Menu Settings | Theme, colours, width, font and size, where the menu opens, button bindings, overlay options |

## Local co-op

The **Co-op** page turns on splitscreen for every campaign title (2-4 local players, a controller each, optional keyboard and mouse for player 1). It is ported from [AlphaRing](https://github.com/WinterSquire/AlphaRing) by WinterSquire; see the **Credits** page in the menu. Restart the mission or load a checkpoint after changing it. The menu also opens on MCC's own screens (same buttons) with just the co-op settings, which apply to every title; do that before starting a CE mission.

## Core cheats

The table describes the Halo 3 versions; `NOTES.md` covers how each title differs.

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
| Noclip | Holds you in place with the engine's position call each tick. Hold RB (or G) to fly where you're looking, through anything. Shift for fast |
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
| Camera distance | Moves the third-person camera back by a slider amount |
| No HUD | Hides the HUD, the same as the game's chud_show script command |

Every cheat restores the original values when switched off. Tag edits are applied again after a map or checkpoint reload.

Cheats that call engine functions (noclip, damage, give weapon) run on the game's main thread. The mod reaches that thread through hooks on each title's main loop or tick, or through MCC's input polling (`get_key_state`).

The code patches are found by byte pattern (`core/patch.cpp`), so they survive small updates. Several come from a Cheat Engine table made for the Windows Store build; the patterns also match the Steam build.

## Finding offsets

Offsets in each title's `offsets.h` note the DLL address (RVA) where they were confirmed. Most came from disassembling the engine's own HaloScript functions:
- **Function table:** the engine keeps a table of every script function (name and evaluator at `+0x20`). For Halo 3 it's at RVA `0x7F6AD0`, with 1,379 entries in the order of Assembly's `H3MCC_U14_Scripting.xml`. The other titles are similar (Halo 4's is built at runtime and is dumped from the running game).
- **Globals table:** RVA `0x7D36A0` in Halo 3, 1,246 entries.

The retail `cheat_*` HaloScript globals have no storage (their address field is 0), so they can't be used, except in Halo: CE. Scripts for this are in `tools/re/`.

## Layout

| Folder | What's in it |
|---|---|
| `src/core/` | `VERSION.dll` proxy (forwards every export to System32), patching and logging |
| `src/mcc/` | Hooks into MCC itself, shared by all titles: title DLL load/unload, input blocking through the game manager's `get_key_state`, and local co-op |
| `src/games/` | One class per title (`game.h`). `games.cpp` lists all six and routes to whichever is running. `blam.h` holds engine structures shared by Halo 3 and later |
| `src/games/halo3/` | Halo 3: `halo3.cpp` has the hooks, `cheats.cpp` the cheat logic, `tags.cpp` tag lookup, and `offsets.h` the offsets |
| `src/games/halo1/`, `halo2/`, `odst/`, `reach/`, `halo4/` | The other titles, laid out the same way (`odst/` is a port of `halo3/`, `halo4/` of `reach/`) |
| `src/ui/` | D3D11 `Present` hook with the ImGui menu (`overlay.cpp`), XInput controller reading (`gamepad.cpp`), saved settings and menu configuration |
| `tools/re/` | Reverse-engineering scripts per title (disassembly, script-table readers, live memory helpers in `tools/re/live/`). Needs your own memory dumps; `python mkimg.py` builds them from a running game |
| `tools/ToggleHaloModMenu/` | The desktop on/off toggle |

**Adding a title:**
1. Create `src/games/<title>/` with a `Game` subclass and its offsets.
2. Add it to `games.cpp`, and add the `.cpp` files to `CMakeLists.txt`.

Offsets for the main loop and a few other hooks came from AlphaRing's 1.3528.0.0 tables. After a game update everything must be found again.

## Credits

- Local co-op and several engine offsets: [AlphaRing](https://github.com/WinterSquire/AlphaRing) by WinterSquire.
- Code patches: a community Cheat Engine table for MCC.
- Tag layouts: the Assembly plugin files for each title.

Third-party code, vendored in `third_party/`: Dear ImGui v1.91.9, MinHook (master, 2026-09-08) and miniz.
