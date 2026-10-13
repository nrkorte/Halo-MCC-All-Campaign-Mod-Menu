#pragma once

// Halo 4 pieces shared between spawn.cpp (spawning, AI teams) and the feature
// files that build on them (ai_upgrade.cpp, teams.cpp, extras.cpp,
// fun_extras.cpp, equipment.cpp, checkpoints.cpp, vehicles.cpp). Everything
// here runs on the game thread inside SEH, like the rest of the cheat code.
// Same shape as halo3/internal.h; Halo 4 runs Reach's engine, so the
// implementations follow reach/main_thread.cpp.

#include <cstdint>
#include <vector>

#include "cheat_util.h"
#include "games/game.h"

namespace Halo4Internal {
    // halo4.dll's base address.
    char* Module();

    // An AI from a character tag as a squadless actor (the spawn key's own path:
    // actor request, actor_set_active, team). `team` is the AI team picker's
    // choice (Settings::spawnTeam: 0 own, 1 ally, 2 Covenant, 3 Flood (treated
    // as 0), 4 rogue, 5 random). Returns the actor datum or -1.
    uint32_t SpawnActor(void** state, const float* position, float yaw, uint32_t character, int team);

    // Resolves a picker choice to the game's team number (-1 = leave the AI on its own team).
    int ResolveSpawnTeam(void** state, int choice);

    // Sets an actor's team to a raw game team number (the engine's actor_set_team).
    void SetActorTeam(uint32_t actor, int16_t team);

    // Starts following an actor the mod made, so "Enable respawn" and
    // "Despawn all custom AI" know it. `team` is the picker choice.
    void Follow(void** state, const Halo4Cheats::Array& objects, uint32_t actor, uint32_t character, int team);

    // `oldUnit` is about to be replaced by `newActor` (upgrade/swap): if the old unit
    // was a followed spawn, the new one takes over its slot (same team choice).
    void Refollow(void** state, const Halo4Cheats::Array& objects, uint32_t oldUnit, uint32_t newActor,
                  uint32_t newCharacter);

    // Writes up to `max` unit datums of followed (mod-spawned) AI, alive or dead; returns how many.
    // Despawn custom AI walks these, then calls ForgetFollowed.
    int FollowedUnits(uint32_t* out, int max);
    void ForgetFollowed();

    // A toast in the middle of the screen (the spawn result line).
    void Toast(const char* text);

    // Deletes an object (object_delete's core).
    void DeleteObject(uint32_t object);

    // True if team `a` isn't hostile to team `b` right now (the game's allegiance
    // table, so it follows "Your team"). A team counts as its own friend.
    bool TeamsFriends(void** state, int teamA, int teamB);

    // The spot player 1 is aiming at (the spawn key's ray): the hit point, the
    // look direction (unit vector) and the yaw an AI placed there should face
    // (towards the player, 0..2pi). False if nothing is in range.
    bool AimPoint(void** state, const Halo4Cheats::Players& p, float* at, float* dir, float* facing);

    // The scenario's character tags the Spawning page can make (the AI list),
    // for squads / swaps. Valid until the next map.
    const std::vector<Games::TagChoice>& Characters();

    // Loads the designer zone a character needs (spawn code's zone loader) if it is
    // in one that isn't active; true if a load was started (retry in a moment).
    bool LoadZoneFor(uint32_t tag);
}

// Work items built on the above. Each is called from the wiring in cheats.cpp /
// main_thread.cpp (done by the coordinator, not the feature files); all of them
// must be safe to call every tick/frame with nothing enabled.
namespace Halo4Teams {  // teams.cpp: Nuke, Despawn custom AI, Random AI teams, Your team, no betrayal
    void Load(char* module);
    void Unload();
    void Reset();  // map change / unload: forget per-map state
    // Game thread, once per game tick, inside SEH.
    void Tick(void** state, const Games::Settings&, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects);
}
namespace Halo4Extras {  // extras.cpp: friendly fire, time stop (hooks), AI stay dead
    void Load(char* module);
    void Unload();
    void Reset();
    // Game thread, once per game tick, inside SEH.
    void Tick(void** state, const Games::Settings&, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects);
}
namespace Halo4Checkpoints {  // checkpoints.cpp: lock checkpoints, checkpoint history
    void Load(char* module);
    void Unload();
    void Reset();
    void Tick(void** state, const Games::Settings&, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects);
    // Render thread, every frame: fills Status::checkpoints (history).
    void Apply(const Games::Settings&, Games::Status&);
    // The mod's own "Save checkpoint" button brackets its save call with these, so "Lock checkpoints" lets it through.
    void BeginOwnSave();
    void EndOwnSave();
}
namespace Halo4Fun {  // fun_extras.cpp: launch on hit, AI size, double jump, poison touch
    void Load(char* module);
    void Unload();
    void Reset();
    void Tick(void** state, const Games::Settings&, const Games::FrameInput&, const Halo4Cheats::Players&,
              const Halo4Cheats::Array& objects);
}
namespace Halo4Equipment {  // equipment.cpp: give equipment, perfect camo, equipment extras
    void Load(char* module);
    void Unload();
    void Reset();
    void Tick(void** state, const Games::Settings&, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects);
    // Render thread, every frame: fills Status::equipment (the list).
    void Apply(const Games::Settings&, Games::Status&);
}
namespace Halo4Upgrade {  // ai_upgrade.cpp: Upgrade AI, Random AI swap, Squads
    void Reset();
    // Render thread: does this map have a rank ladder to move enemies (allies = false) or marines (true) up?
    bool UpgradeApplicable(bool allies);
    // Game thread, once per game tick, inside SEH.
    void Tick(void** state, const Games::Settings&, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects);
    // Render thread, every frame: fills Status::squadsAvailable (games/squads.h helpers).
    void Apply(const Games::Settings&, Games::Status&);
    // Game thread: the spawn key with a squad selected (Settings::spawnSquad >= 0).
    void SpawnSquad(void** state, const Halo4Cheats::Players&, const Halo4Cheats::Array& objects, int squad, int team);
}
namespace Halo4Vehicles {  // vehicles.cpp: pilot AI-only vehicles, upgrade vehicle, take pilot seat
    void Load(char* module);
    void Unload();
    void Reset();
    void Tick(void** state, const Games::Settings&, const Games::FrameInput&, const Halo4Cheats::Players&,
              const Halo4Cheats::Array& objects);
}
