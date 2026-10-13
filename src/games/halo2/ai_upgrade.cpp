// Halo 2: Upgrade AI, Random AI swap, Squads (see NOTES.md, "Upgrade AI and
// random AI teams (Reach)" and "Squads (Reach)": the same features, re-done on
// Halo 2's actors and squads).
//
// Upgrade AI and Random AI swap replace an AI by a fresh one: the new actor is
// made where the old one stands (same facing), moved into the old one's squad
// (so the mission's scripts still count it), given its team, and then the old
// one is erased. New before old, so the squad is never empty. Halo 2 has no
// encounters, only squads (OFFSET_HALO2_AI_RECORDS), and an actor knows its
// squad by index (actor +0x30).

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ai_upgrade_offsets.h"
#include "core/log.h"
#include "games/fun.h"
#include "games/squads.h"
#include "internal.h"
#include "tags.h"

using namespace Halo2Cheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kSpawnBackOff = 0.3f;  // as the spawn key: back along the ray from the hit,
    constexpr float kSpawnLift = 0.25f;    // and up, so nobody is inside the floor

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // A character with a unit to spawn as (templates like "generic" have none).
    bool HasUnit(uint32_t character) {
        __try {
            char* tag = Halo2Tags::Get(character);
            return tag && Field<uint32_t>(tag, HALO2_CHAR_UNIT) != 0xFFFFFFFF;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Characters that never take part: templates, turrets, story characters the
    // missions' scripts depend on (Cortana, the Monitor, the Prophets, Johnson,
    // Tartarus, the Councilor, Heretic leaders; the Arbiter himself), crews
    // and set-dressing marines. They're neither swapped nor made by squads.
    bool Protected(const std::string& name) {
        if (name.rfind("null", 0) == 0 || name.rfind("generic", 0) == 0) return true;
        for (const char* part : {"turret", "template", "cortana", "miranda", "monitor", "prophet", "dervish", "johnson",
                                 "crewman", "marine_wounded", "marine_dress", "tartarus", "councilor", "heretic_leader"})
            if (name.find(part) != std::string::npos) return true;
        return false;
    }

    // --- The map's characters (render thread builds, game thread copies) ------
    // Halo2Tags' name cache isn't locked, and it's reloaded on the render
    // thread at a map change, so the game thread never reads it: it takes a
    // copy of this list, which is only valid while the scenario it was made
    // for is the loaded one.
    std::mutex g_listLock;
    std::vector<Games::TagChoice> g_shared;  // spawnable, unprotected characters
    char* g_sharedScnr = nullptr;
    uint32_t g_sharedVersion = 0;

    // Render thread only.
    char* g_renderScnr = nullptr;
    uint64_t g_available[2] = {};

    void Rebuild(char* scnr) {
        Halo2Tags::LoadNames();
        std::vector<Games::TagChoice> list;
        for (auto& c : Halo2Tags::ListGroup(HALO2_TAG_CHAR))
            if (HasUnit(c.datum) && !Protected(c.name)) list.push_back(c);
        uint64_t mask[2] = {};
        Games::Status scratch;
        for (int i = 0; i < Games::kSquadCount; ++i) {
            const Games::Squad& squad = Games::kSquads[i];
            if (Games::SquadInTitle(squad, "Halo 2") && !Games::SquadTags(squad, list).empty())
                Games::SetSquadAvailable(scratch, i);
        }
        mask[0] = scratch.squadsAvailable[0];
        mask[1] = scratch.squadsAvailable[1];
        LOG("Halo 2: squads/upgrade: %zu spawnable characters in this map", list.size());
        std::lock_guard<std::mutex> lock(g_listLock);
        g_shared = list;
        g_sharedScnr = scnr;
        ++g_sharedVersion;
        g_available[0] = mask[0];
        g_available[1] = mask[1];
    }

    // --- Game thread ----------------------------------------------------------
    std::vector<Games::TagChoice> g_chars;  // copy of g_shared
    uint32_t g_localVersion = 0;
    struct UpgradeTo {
        uint32_t character;
        bool ally;  // a marine-family rank: needs "Allies too"
    };
    std::unordered_map<uint32_t, UpgradeTo> g_upgradeTo;
    std::unordered_map<uint32_t, std::string> g_nameOf;
    std::vector<uint32_t> g_pool;  // random swap's pool
    std::unordered_set<uint32_t> g_upgradeSeen, g_swapSeen;
    uint32_t g_upgradeSerial = 0, g_swapSerial = 0;
    bool g_serialsInit = false;
    uint32_t g_rng = 0x2545F491u;

    float Random01() {
        if (g_rng == 0x2545F491u) g_rng ^= (uint32_t)GetTickCount64();
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (g_rng >> 8) / 16777216.0f;
    }

    // Rank ladders by character name; a rank the map lacks is skipped, so the
    // next present one is the upgrade. The marine ladders are allies' (only with
    // "Allies too"). Jackal snipers, rangers, honor guards, the Flood's
    // combat humans and the like are single-rank classes: no upgrade.
    struct Ladder {
        std::vector<const char*> ranks;
        bool ally;
    };
    const Ladder kLadders[] = {
        {{"grunt", "grunt_major", "grunt_heavy", "grunt_ultra", "grunt_specops"}, false},
        {{"jackal", "jackal_major"}, false},
        {{"elite", "elite_major", "elite_ultra", "elite_specops", "elite_specops_commander", "elite_zealot"}, false},
        {{"elite_stealth", "elite_stealth_major"}, false},
        {{"brute", "brute_major", "brute_captain", "brute_honor_guard"}, false},
        {{"heretic", "heretic_major"}, false},
        {{"floodcombat_elite", "floodcombat_elite_shielded"}, false},
        {{"sentinel_aggressor", "sentinel_aggressor_major", "sentinel_aggressor_eliminator"}, false},
        {{"marine", "marine_sgt", "marine_odst"}, true},
        {{"marine_female", "marine_sgt"}, true},
    };

    void BuildUpgrades() {
        g_upgradeTo.clear();
        g_nameOf.clear();
        g_pool.clear();
        std::unordered_map<std::string, uint32_t> byName;
        for (auto& c : g_chars) {
            byName[c.name] = c.datum;
            g_nameOf[c.datum] = c.name;
            if (c.name != "flood_infection") g_pool.push_back(c.datum);  // swarm forms don't make a swap's target
        }
        for (const Ladder& ladder : kLadders) {
            uint32_t previous = 0xFFFFFFFF;
            const char* previousName = "";
            for (const char* name : ladder.ranks) {
                auto have = byName.find(name);
                if (have == byName.end()) continue;
                if (previous != 0xFFFFFFFF && !g_upgradeTo.count(previous)) {
                    g_upgradeTo[previous] = {have->second, ladder.ally};
                    LOG("Halo 2: upgrade AI: %s -> %s", previousName, name);
                }
                previous = have->second;
                previousName = name;
            }
        }
    }

    // Takes the render thread's list when it's for the loaded map. False if
    // there's nothing to work with.
    bool Sync() {
        char* scnr = Halo2Tags::First(HALO2_TAG_SCNR);
        std::lock_guard<std::mutex> lock(g_listLock);
        if (!scnr || scnr != g_sharedScnr) return false;
        if (g_sharedVersion != g_localVersion) {
            g_localVersion = g_sharedVersion;
            g_chars = g_shared;
            g_upgradeSeen.clear();
            g_swapSeen.clear();
            BuildUpgrades();
        }
        return !g_chars.empty();
    }

    int16_t TeamOf(char* unit) { return Field<int16_t>(unit, HALO2_UNIT_TEAM); }

    // Player 1's side: their own team, the players' team, and the humans (or,
    // when playing the Arbiter, the Covenant).
    bool IsAlly(char* unit, const Players& p) {
        int16_t team = TeamOf(unit);
        if (team == HALO2_TEAM_PLAYER) return true;
        if (p.count > 0) {
            int16_t mine = TeamOf(p.units[0]);
            if (team == mine) return true;
            bool arbiter = Halo2Tags::FullName(Field<uint32_t>(p.units[0], HALO2_OBJECT_TAG_DATUM)).find("dervish") !=
                           std::string::npos;
            return team == (arbiter ? HALO2_TEAM_COVENANT : HALO2_TEAM_HUMAN);
        }
        return team == HALO2_TEAM_HUMAN;
    }

    bool InRange(const Players& p, char* unit, float metres) {
        if (metres <= 0.0f || p.count == 0) return true;
        const float* a = &Field<float>(unit, HALO2_OBJECT_POSITION);
        const float* b = &Field<float>(p.units[0], HALO2_OBJECT_POSITION);
        float limit = metres / Fun::kMetresPerUnit;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz <= limit * limit;
    }

    Array Actors() { return Array{*(char**)(Halo2Internal::Module() + OFFSET_HALO2_ACTORS)}; }

    using ActorMigrateFn = void(__fastcall*)(uint32_t actor, int16_t squad);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor);

    // Replaces the AI behind `actorDatum` by a fresh `character`. Returns the
    // new actor datum, or -1 (the old one is then left alone).
    uint32_t ReplaceActor(const Players& p, const Objects& objects, uint32_t actorDatum, uint32_t character) {
        Array actors = Actors();
        char* actor = actors.header ? actors.Get(actorDatum) : nullptr;
        uint32_t unitDatum = actor ? Field<uint32_t>(actor, HALO2_ACTOR_UNIT) : 0xFFFFFFFF;
        char* unit = objects.Get(unitDatum);
        if (!unit || !Halo2Tags::Get(character)) return 0xFFFFFFFF;
        float position[3];
        memcpy(position, &Field<float>(unit, HALO2_OBJECT_POSITION), sizeof(position));
        position[2] += kSpawnLift;
        const float* forward = &Field<float>(unit, HALO2_OBJECT_FORWARD);
        float yaw = atan2f(forward[1], forward[0]);
        if (yaw < 0.0f) yaw += 2.0f * kPi;  // Halo 2 keeps yaw in 0..2pi
        int16_t team = TeamOf(unit);
        int32_t squad = Field<int32_t>(actor, HALO2_ACTOR_SQUAD_INDEX);

        uint32_t fresh = Halo2Internal::SpawnActor(p, character, position, yaw, 0);
        if (fresh == 0xFFFFFFFF) return fresh;
        char* base = Halo2Internal::Module();
        if (squad != -1) ((ActorMigrateFn)(base + OFFSET_HALO2_ACTOR_MIGRATE))(fresh, (int16_t)squad);
        Halo2Internal::SetActorTeam(fresh, team);  // attaching may have changed it
        Halo2Internal::Refollow(objects, unitDatum, fresh, character);  // a mod-spawned AI stays followed
        // ai_erase's per-actor core: the actor, then its unit (read first).
        ((ActorEraseFn)(base + OFFSET_HALO2_ACTOR_ERASE))(actorDatum);
        Halo2Internal::DeleteObject(unitDatum);
        return fresh;
    }

    // Walks the live AI (not players, not the dead, not seated) and replaces
    // those `choose` returns a character for. `all` is the button (everyone,
    // again); otherwise each AI once. Returns how many were replaced.
    template <typename Choose>
    int ReplaceAi(const Players& p, const Objects& objects, float range, bool all,
                  std::unordered_set<uint32_t>& seen, int* seated, int* failed, Choose choose) {
        Array actors = Actors();
        if (!actors.header) return 0;
        struct Pick { uint32_t actor, character; };
        std::vector<Pick> picks;  // collected first: replacing adds actors
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && seen.count(datum)) continue;
            char* unit = objects.Get(Field<uint32_t>(actor, HALO2_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            if (!InRange(p, unit, range)) continue;
            uint32_t character = choose(Field<uint32_t>(actor, HALO2_ACTOR_CHARACTER), unit);
            if (character == 0xFFFFFFFF) { seen.insert(datum); continue; }
            if (Field<uint32_t>(unit, HALO2_OBJECT_PARENT) != 0xFFFFFFFF) { if (seated) ++*seated; continue; }
            picks.push_back({datum, character});
        }
        int done = 0;
        for (const Pick& pick : picks) {
            uint32_t fresh = ReplaceActor(p, objects, pick.actor, pick.character);
            seen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { seen.insert(fresh); ++done; }
            else if (failed) ++*failed;
        }
        return done;
    }

    void UpgradeAi(const Games::Settings& s, const Players& p, const Objects& objects, bool all) {
        if (g_upgradeTo.empty()) {
            if (all) LOG("Halo 2: upgrade AI: no AI to upgrade in this map");
            return;
        }
        int seated = 0, failed = 0;
        int done = ReplaceAi(p, objects, s.upgradeAiRange, all, g_upgradeSeen, &seated, &failed,
                             [&](uint32_t character, char*) -> uint32_t {
                                 auto to = g_upgradeTo.find(character);
                                 if (to == g_upgradeTo.end() || (to->second.ally && !s.upgradeAiAllies))
                                     return 0xFFFFFFFF;
                                 return to->second.character;
                             });
        if (all || done || failed)
            LOG("Halo 2: upgrade AI (%s): %d upgraded, %d failed, %d seated skipped", all ? "button" : "auto", done,
                failed, seated);
    }

    // Random swap: each AI becomes a fresh one of a random character, where it
    // stands, in its squad and on its team. A failed pick is retried with
    // another, up to 3 times. Seated AI wait until they're out.
    void RandomSwap(const Games::Settings& s, const Players& p, const Objects& objects, bool all) {
        if (g_pool.size() < 2) {
            if (all) LOG("Halo 2: random swap: no AI to swap in this map");
            return;
        }
        Array actors = Actors();
        if (!actors.header) return;
        struct Pick { uint32_t actor, character; };
        std::vector<Pick> picks;
        int seated = 0;
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_swapSeen.count(datum)) continue;
            char* unit = objects.Get(Field<uint32_t>(actor, HALO2_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            uint32_t current = Field<uint32_t>(actor, HALO2_ACTOR_CHARACTER);
            // Only plain characters of the pool's kind (so not Johnson, Cortana, ...); allies on request.
            if (!g_nameOf.count(current) || g_nameOf[current] == "flood_infection" ||
                (!s.randomSwapAllies && IsAlly(unit, p))) {
                g_swapSeen.insert(datum);
                continue;
            }
            if (Field<uint32_t>(unit, HALO2_OBJECT_PARENT) != 0xFFFFFFFF) { ++seated; continue; }
            uint32_t pick = current;
            for (int tries = 0; tries < 8 && pick == current; ++tries)
                pick = g_pool[(size_t)(Random01() * g_pool.size()) % g_pool.size()];
            if (pick == current) { g_swapSeen.insert(datum); continue; }
            picks.push_back({datum, pick});
        }
        int done = 0, failed = 0;
        for (const Pick& pick : picks) {
            uint32_t fresh = ReplaceActor(p, objects, pick.actor, pick.character);
            for (int again = 0; fresh == 0xFFFFFFFF && again < 3; ++again)
                fresh = ReplaceActor(p, objects, pick.actor, g_pool[(size_t)(Random01() * g_pool.size()) % g_pool.size()]);
            g_swapSeen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { g_swapSeen.insert(fresh); ++done; }
            else ++failed;
        }
        if (all || done || failed)
            LOG("Halo 2: random swap (%s): %d swapped, %d failed, %d seated skipped, pool %zu", all ? "button" : "auto",
                done, failed, seated, g_pool.size());
    }
}

namespace Halo2Upgrade {
    void Reset() {
        // The map changed: the actors and the character list are the old map's.
        g_chars.clear();
        g_upgradeTo.clear();
        g_nameOf.clear();
        g_pool.clear();
        g_upgradeSeen.clear();
        g_swapSeen.clear();
        g_localVersion = 0;
    }

    void Tick(const Games::Settings& s, const Players& p, const Objects& objects) {
        if (!g_serialsInit) {  // a button pressed before this session isn't pressed now
            g_serialsInit = true;
            g_upgradeSerial = s.upgradeAiSerial;
            g_swapSerial = s.randomSwapSerial;
        }
        bool upgradeButton = s.upgradeAiSerial != g_upgradeSerial;
        bool swapButton = s.randomSwapSerial != g_swapSerial;
        g_upgradeSerial = s.upgradeAiSerial;
        g_swapSerial = s.randomSwapSerial;
        if (!upgradeButton && !swapButton && !s.upgradeAiAuto && !s.randomSwapAuto) return;
        if (!Sync()) return;
        if (!objects.Valid()) return;

        if (upgradeButton) UpgradeAi(s, p, objects, true);
        if (s.upgradeAiAuto) UpgradeAi(s, p, objects, false);
        if (swapButton) RandomSwap(s, p, objects, true);
        if (s.randomSwapAuto) RandomSwap(s, p, objects, false);
    }

    // Which squads this map can make (greyed out otherwise). Only recomputed
    // when the loaded scenario changes.
    void Apply(const Games::Settings&, Games::Status& status) {
        Guard("Halo 2 squads available", [&] {
            char* scnr = Halo2Tags::First(HALO2_TAG_SCNR);
            if (scnr != g_renderScnr) {
                g_renderScnr = scnr;
                if (scnr) {
                    Rebuild(scnr);
                } else {
                    std::lock_guard<std::mutex> lock(g_listLock);
                    g_shared.clear();
                    g_sharedScnr = nullptr;
                    ++g_sharedVersion;
                    g_available[0] = g_available[1] = 0;
                }
            }
        });
        std::lock_guard<std::mutex> lock(g_listLock);
        status.squadsAvailable[0] = g_available[0];
        status.squadsAvailable[1] = g_available[1];
    }

    // Squads: every member is made around the aim point, a loose cluster (a
    // sunflower spiral, so any size fits), through the same actor spawn as a
    // single AI. Members the map lacks are skipped.
    void SpawnSquad(const Players& p, const Objects& objects, int index, int team) {
        if (index < 0 || index >= Games::kSquadCount) return;
        const Games::Squad& squad = Games::kSquads[index];
        if (!Games::SquadInTitle(squad, "Halo 2")) return;
        if (p.count == 0) { LOG("Halo 2: squad %s: no player to spawn for", squad.name); return; }
        if (!Sync()) { LOG("Halo 2: squad %s: this map's characters aren't known yet", squad.name); return; }
        std::vector<uint32_t> tags = Games::SquadTags(squad, g_chars);
        if (tags.empty()) { LOG("Halo 2: squad %s isn't in this map", squad.name); return; }

        float at[3], dir[3], facing;
        if (!Halo2Internal::AimPoint(p, at, dir, &facing)) {
            LOG("Halo 2: squad %s: nothing in range to spawn on", squad.name);
            return;
        }
        float center[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                           at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        float spacing = squad.large ? 1.5f : 0.9f;  // world units between neighbours

        int made = 0;
        for (size_t i = 0; i < tags.size(); ++i) {
            float radius = spacing * sqrtf((float)i);
            float angle = (float)i * 2.39996323f;  // the golden angle
            float position[3] = {center[0] + cosf(angle) * radius, center[1] + sinf(angle) * radius, center[2]};
            uint32_t actor = Halo2Internal::SpawnActor(p, tags[i], position, facing, team);
            if (actor == 0xFFFFFFFF) continue;
            Halo2Internal::Follow(objects, actor, tags[i], team);
            ++made;
        }
        LOG("Halo 2: squad %s at %.2f %.2f %.2f: %d of %zu made", squad.name, center[0], center[1], center[2], made,
            tags.size());
        char text[96];
        if (made > 0) sprintf_s(text, "Spawned %s (%d AI)", squad.name, made);
        else sprintf_s(text, "Couldn't spawn %s here", squad.name);
        Halo2Internal::Toast(text);
    }
}
