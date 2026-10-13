#pragma once

// Halo 2 pieces shared between main_thread.cpp (spawning, AI teams) and the
// feature files that build on them (ai_upgrade.cpp, extras.cpp). Everything
// here runs on the game thread inside SEH, like the rest of the cheat code.

#include <cstdint>

#include "cheat_util.h"
#include "games/game.h"

namespace Halo2Internal {
    // halo2.dll's base address.
    char* Module();

    // An AI from a character tag as a squadless actor (the spawn key's own path:
    // loadout picker, actor_create, actor_set_active, team). `team` is the AI
    // team picker's choice (Settings::spawnTeam: 0 own, 1 ally, 2 Covenant,
    // 3 Flood, 4 rogue, 5 random). Returns the actor datum or -1.
    uint32_t SpawnActor(const Halo2Cheats::Players& p, uint32_t character, const float* position, float yaw, int team);

    // Resolves a picker choice to the game's team number (0 = leave the AI on its own team).
    int16_t ResolveSpawnTeam(const Halo2Cheats::Players& p, int choice);

    // Sets an actor's team to a raw game team number (actor and unit, like actor_set_team).
    void SetActorTeam(uint32_t actor, int16_t team);

    // Starts following an actor the mod made, so "Enable respawn" and
    // "Despawn all custom AI" know it.
    void Follow(const Halo2Cheats::Objects& objects, uint32_t actor, uint32_t character, int team);

    // A toast in the middle of the screen (the spawn result line).
    void Toast(const char* text);

    // `oldUnit` is about to be replaced by `newActor` (upgrade/swap): if the old unit
    // was a followed spawn, the new one takes over its slot (same team choice).
    void Refollow(const Halo2Cheats::Objects& objects, uint32_t oldUnit, uint32_t newActor, uint32_t newCharacter);

    // Deletes an object (object_delete's core).
    void DeleteObject(uint32_t object);

    // True if team `a` isn't hostile to team `b` right now (the game's allegiance
    // table, so it follows "Your team"). A team is its own friend, except the
    // rogue team (8).
    bool TeamsFriends(int teamA, int teamB);

    // The spot player 1 is aiming at (the spawn key's ray): the hit point, the
    // look direction (unit vector) and the yaw an AI placed there should face
    // (towards the player, 0..2pi). False if nothing is in range.
    bool AimPoint(const Halo2Cheats::Players& p, float* at, float* dir, float* facing);
}

// Work items built on the above. Each is called from the wiring in cheats.cpp /
// main_thread.cpp; all of them must be safe to call every tick/frame with
// nothing enabled.
namespace Halo2Extras {  // extras.cpp: time stop, launch on hit, AI size, double jump, poison touch, friendly fire, checkpoint lock, ...
    void Load(char* module);   // halo2.dll loaded: find patches, install hooks
    void Unload();             // halo2.dll unloading: remove hooks and patches
    void Reset();              // map change / unload: forget per-map state
    // Game thread, once per game tick before the tick runs, inside SEH.
    void Tick(const Games::Settings&, const Games::FrameInput&, const Halo2Cheats::Players&, const Halo2Cheats::Objects&);
    // Render thread, every frame, inside SEH (memory/tag edits, status).
    void Apply(const Games::Settings&, const Halo2Cheats::Players&, const Halo2Cheats::Objects&, Games::Status&);
    // The mod's own "Save checkpoint" button brackets its save call with these, so "Lock checkpoints" lets it through.
    void BeginOwnSave();
    void EndOwnSave();
}
namespace Halo2Fun {  // fun_extras.cpp: launch on hit, AI size, double jump, poison touch
    void Load(char* module);
    void Unload();
    void Reset();
    // Game thread, once per game tick before the tick runs, inside SEH.
    void Tick(const Games::Settings&, const Games::FrameInput&, const Halo2Cheats::Players&, const Halo2Cheats::Objects&);
}
namespace Halo2Upgrade {  // ai_upgrade.cpp: Upgrade AI, Random AI swap, Squads
    void Reset();
    // Game thread, once per game tick, inside SEH.
    void Tick(const Games::Settings&, const Halo2Cheats::Players&, const Halo2Cheats::Objects&);
    // Render thread, every frame: fills Status::squadsAvailable (games/squads.h helpers).
    void Apply(const Games::Settings&, Games::Status&);
    // Game thread: the spawn key with a squad selected (Settings::spawnSquad >= 0).
    void SpawnSquad(const Halo2Cheats::Players&, const Halo2Cheats::Objects&, int squad, int team);
}
