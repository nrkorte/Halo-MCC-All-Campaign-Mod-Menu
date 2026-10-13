// Halo 4: Upgrade AI, Random AI swap, Squads (see NOTES.md, "Upgrade AI and
// random AI teams (Reach)", "Squads (Reach)" and the Halo 3 port: the same
// features, re-done on Halo 4's actors and squads).
//
// Upgrade AI and Random AI swap replace an AI by a fresh one: the new actor is
// made where the old one stands (same facing), moved into the old one's squad
// (so the mission's scripts still count it), given the old team, and then the
// old one is erased. New before old, so the squad is never empty. An actor
// knows its squad by index (actor +0x20); squads are TLS slot 9.

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

using namespace Halo4Cheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kSpawnBackOff = 0.3f;  // as the spawn key: back along the ray from the hit,
    constexpr float kSpawnLift = 0.25f;    // and up, so nobody is inside the floor
    constexpr ULONGLONG kZoneLoadMs = 5000;
    constexpr int kLogFirst = 6;  // the first few engine calls are logged in detail

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // Characters that never take part: templates, story characters the
    // missions' scripts depend on, Spartans, pilots and crews, scientists and
    // civilians, null_ dummies (invisible turret / vehicle drivers) and the
    // space and cinematic variants. They're neither swapped nor made by squads.
    bool Protected(const std::string& name) {
        if (name.rfind("null", 0) == 0 || name.rfind("generic", 0) == 0) return true;
        for (const char* part : {"spartan", "pilot", "taxi", "space", "scientist", "civilian", "turret", "wraith_m",
                                 "fleet_officer"})
            if (name.find(part) != std::string::npos) return true;
        return false;
    }

    // Rank ladders by character name (the campaign's 'char' tags, read from
    // all the campaign maps); a rank the map lacks is skipped, so the next
    // present one is the upgrade. The marine ladder is allies' (only with
    // "Allies too"). The _aggressive / _defensive / _kamikaze / _m10 style
    // presets are behaviour variants of the same rank and have no ladder of
    // their own. Single-rank classes (hunters, jackal snipers, crawler
    // snipers, bishops, sentinels) don't upgrade. The Promethean ladder runs
    // from crawlers into knights.
    struct Ladder {
        std::vector<const char*> ranks;
        bool ally;
    };
    const Ladder kLadders[] = {
        {{"storm_grunt", "storm_grunt_imperial", "storm_grunt_ultra"}, false},
        {{"storm_jackal", "storm_jackal_major", "storm_jackal_ranger", "storm_jackal_ranger_shield"}, false},
        {{"storm_elite", "storm_elite_officer", "storm_elite_specops", "storm_elite_zealot", "storm_elite_general"},
         false},
        {{"storm_pawn", "storm_pawn_prime", "storm_knight", "storm_knight_ranger", "storm_knight_battlewagon",
          "storm_knight_commander"},
         false},
        {{"storm_marine", "storm_marine_oni"}, true},
    };

    // --- The map's characters (render thread builds, game thread copies) ------
    // Halo4Tags' name cache isn't locked, and it's reloaded on the render
    // thread at a map change, so the game thread never reads it: it takes a
    // copy of this list, which is only valid while the scenario it was made
    // for is the loaded one.
    std::mutex g_listLock;
    std::vector<Games::TagChoice> g_shared;  // spawnable, unprotected characters
    char* g_sharedScnr = nullptr;
    uint32_t g_sharedVersion = 0;

    // Render thread only.
    char* g_renderScnr = nullptr;
    size_t g_renderCount = 0;
    uint32_t g_renderFirst = 0, g_renderLast = 0;
    uint64_t g_available[4] = {};
    bool g_upgradeEnemies = false, g_upgradeAllies = false;  // some ladder has a next rank in this map

    void Rebuild(char* scnr, const std::vector<Games::TagChoice>& spawnable) {
        std::vector<Games::TagChoice> list;
        for (auto& c : spawnable)
            if (!Protected(c.name)) list.push_back(c);
        uint64_t available[4] = {};  // by squad index; Apply() copies the words Status has
        for (int i = 0; i < Games::kSquadCount && i < 256; ++i) {
            const Games::Squad& squad = Games::kSquads[i];
            if (Games::SquadInTitle(squad, "Halo 4") && !Games::SquadTags(squad, list).empty())
                available[i >> 6] |= 1ull << (i & 63);
        }
        bool enemies = false, allies = false;
        for (const Ladder& ladder : kLadders) {
            int present = 0;
            for (const char* name : ladder.ranks)
                for (const auto& c : list)
                    if (c.name == name) { ++present; break; }
            if (present >= 2) (ladder.ally ? allies : enemies) = true;
        }
        LOG("Halo 4: squads/upgrade: %zu spawnable characters in this map", list.size());
        std::lock_guard<std::mutex> lock(g_listLock);
        g_shared = list;
        g_upgradeEnemies = enemies;
        g_upgradeAllies = allies;
        g_sharedScnr = scnr;
        ++g_sharedVersion;
        for (int w = 0; w < 4; ++w) g_available[w] = available[w];
    }

    // --- Designer zones --------------------------------------------------------
    // spawn.cpp's DesignerZonesOf for characters (that file keeps it private):
    // the zones that name this character, directly or by palette index.
    uint64_t ActiveZones() { return *(uint64_t*)(Halo4Internal::Module() + OFFSET_HALO4_DESIGNER_ZONES_ACTIVE); }

    uint64_t ZonesOf(uint32_t tag) {
        char* scnr = Halo4Tags::Scenario();
        if (!scnr) return 0;
        int index = -1;
        int palette = Halo4Tags::BlockCount(scnr, HALO4_SCNR_CHARACTER_PALETTE);
        for (int i = 0; i < palette && i < 1024 && index < 0; ++i) {
            char* entry = Halo4Tags::BlockElement(scnr, HALO4_SCNR_CHARACTER_PALETTE, HALO4_PALETTE_ENTRY_SIZE, i);
            if (entry && Field<uint32_t>(entry, HALO4_PALETTE_ENTRY_DATUM) == tag) index = i;
        }
        uint64_t zones = 0;
        int count = Halo4Tags::BlockCount(scnr, HALO4_SCNR_DESIGNER_ZONES);
        for (int z = 0; z < count && z < 64; ++z) {
            char* zone = Halo4Tags::BlockElement(scnr, HALO4_SCNR_DESIGNER_ZONES, HALO4_SCNR_DESIGNER_ZONE_SIZE, z);
            if (!zone) continue;
            bool in = false;
            int tags = Halo4Tags::BlockCount(zone, HALO4_DESIGNER_ZONE_TAGS);
            for (int e = 0; e < tags && e < 1024 && !in; ++e) {
                char* entry = Halo4Tags::BlockElement(zone, HALO4_DESIGNER_ZONE_TAGS, HALO4_DESIGNER_ZONE_TAG_SIZE, e);
                in = entry && Field<uint32_t>(entry, HALO4_PALETTE_ENTRY_DATUM) == tag;
            }
            int entries = index >= 0 ? Halo4Tags::BlockCount(zone, HALO4_DESIGNER_ZONE_CHARACTERS) : 0;
            for (int e = 0; e < entries && e < 256 && !in; ++e) {
                char* entry = Halo4Tags::BlockElement(zone, HALO4_DESIGNER_ZONE_CHARACTERS, 2, e);
                in = entry && Field<int16_t>(entry, 0) == index;
            }
            if (in) zones |= 1ull << z;
        }
        return zones;
    }

    // True if the character only exists in designer zones and none is loaded now.
    bool NeedsZone(uint32_t tag) {
        uint64_t zones = ZonesOf(tag);
        return zones && !(zones & ActiveZones());
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
    std::vector<uint32_t> g_pool;  // random swap's pool (before the zone filter)
    std::unordered_set<uint32_t> g_upgradeSeen, g_swapSeen;
    uint32_t g_upgradeSerial = 0, g_swapSerial = 0;
    bool g_serialsInit = false;
    uint32_t g_rng = 0x2545F491u;
    int g_replaceLogged = 0;

    float Random01() {
        if (g_rng == 0x2545F491u) g_rng ^= (uint32_t)GetTickCount64();
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (g_rng >> 8) / 16777216.0f;
    }

    void BuildUpgrades() {
        g_upgradeTo.clear();
        g_nameOf.clear();
        g_pool.clear();
        std::unordered_map<std::string, uint32_t> byName;
        for (auto& c : g_chars) {
            byName[c.name] = c.datum;
            g_nameOf[c.datum] = c.name;
            g_pool.push_back(c.datum);
        }
        for (const Ladder& ladder : kLadders) {
            uint32_t previous = 0xFFFFFFFF;
            const char* previousName = "";
            for (const char* name : ladder.ranks) {
                auto have = byName.find(name);
                if (have == byName.end()) continue;
                if (previous != 0xFFFFFFFF && !g_upgradeTo.count(previous)) {
                    g_upgradeTo[previous] = {have->second, ladder.ally};
                    LOG("Halo 4: upgrade AI: %s -> %s", previousName, name);
                }
                previous = have->second;
                previousName = name;
            }
        }
    }

    // Takes the render thread's list when it's for the loaded map. False if
    // there's nothing to work with.
    bool Sync() {
        char* scnr = Halo4Tags::Scenario();
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

    int8_t TeamOf(char* unit) { return Field<int8_t>(unit, HALO4_UNIT_TEAM); }

    // Player 1's side: the player and human teams, their own team, and whatever
    // the game's allegiance table says is friends with it.
    bool IsAlly(void** state, char* unit, const Players& p) {
        int team = TeamOf(unit);
        if (team == HALO4_TEAM_PLAYER || team == HALO4_TEAM_HUMAN) return true;
        if (p.count > 0) {
            int mine = TeamOf(p.units[0]);
            if (team == mine) return true;
            return Halo4Internal::TeamsFriends(state, mine, team);
        }
        return false;
    }

    bool InRange(const Players& p, char* unit, float metres) {
        if (metres <= 0.0f || p.count == 0) return true;
        const float* a = &Field<float>(unit, HALO4_OBJECT_POSITION);
        const float* b = &Field<float>(p.units[0], HALO4_OBJECT_POSITION);
        float limit = metres / Fun::kMetresPerUnit;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz <= limit * limit;
    }

    using ActorMigrateFn = void(__fastcall*)(uint32_t actor, int16_t squad);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor, bool);

    // Replaces the AI behind `actorDatum` by a fresh `character`. Returns the
    // new actor datum, or -1 (the old one is then left alone).
    uint32_t ReplaceActor(void** state, const Array& objects, uint32_t actorDatum, uint32_t character) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        char* actor = actors.Valid() ? actors.Get(actorDatum) : nullptr;
        uint32_t unitDatum = actor ? Field<uint32_t>(actor, HALO4_ACTOR_UNIT) : 0xFFFFFFFF;
        char* unit = ObjectFromDatum(objects, unitDatum);
        if (!unit || !Halo4Tags::Get(character)) return 0xFFFFFFFF;
        float position[3];
        memcpy(position, &Field<float>(unit, HALO4_OBJECT_POSITION), sizeof(position));
        position[2] += kSpawnLift;
        const float* forward = &Field<float>(unit, HALO4_OBJECT_FORWARD);
        float yaw = atan2f(forward[1], forward[0]);
        if (yaw < 0.0f) yaw += 2.0f * kPi;
        int16_t team = TeamOf(unit);
        int32_t squad = Field<int32_t>(actor, HALO4_ACTOR_SQUAD_INDEX);

        uint32_t fresh = Halo4Internal::SpawnActor(state, position, yaw, character, 0);
        if (fresh == 0xFFFFFFFF) return fresh;
        char* base = Halo4Internal::Module();
        if (squad != -1) ((ActorMigrateFn)(base + OFFSET_HALO4_ACTOR_MIGRATE))(fresh, (int16_t)squad);
        Halo4Internal::SetActorTeam(fresh, team);  // attaching may have changed it
        Halo4Internal::Refollow(state, objects, unitDatum, fresh, character);  // a mod-spawned AI stays followed
        if (g_replaceLogged < kLogFirst) {
            ++g_replaceLogged;
            Array again{(char*)state[HALO4_TLS_SLOT_ACTORS]};
            char* made = again.Get(fresh);
            LOG("Halo 4: replace actor %08X -> %08X: squad %d, new actor's squad %d, team %d", actorDatum, fresh, squad,
                made ? (int)Field<int32_t>(made, HALO4_ACTOR_SQUAD_INDEX) : -2, (int)team);
        }
        // ai_erase's per-actor core: frees the actor and deletes its unit.
        ((ActorEraseFn)(base + OFFSET_HALO4_ACTOR_ERASE))(actorDatum, false);
        return fresh;
    }

    // Walks the live AI (not players, not the dead, not seated) and replaces
    // those `choose` returns a character for. `all` is the button (everyone,
    // again); otherwise each AI once. Returns how many were replaced.
    template <typename Choose>
    int ReplaceAi(void** state, const Players& p, const Array& objects, float range, bool all,
                  std::unordered_set<uint32_t>& seen, int* seated, int* failed, Choose choose) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return 0;
        struct Pick { uint32_t actor, character; };
        std::vector<Pick> picks;  // collected first: replacing adds actors
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && seen.count(datum)) continue;
            char* unit = ObjectFromDatum(objects, Field<uint32_t>(actor, HALO4_ACTOR_UNIT));
            float* health = unit ? Health(unit) : nullptr;
            if (!unit || p.Contains(unit) || !health || *health <= 0.0f) continue;
            if (!InRange(p, unit, range)) continue;
            uint32_t character = choose(Field<uint32_t>(actor, HALO4_ACTOR_CHARACTER), unit);
            if (character == 0xFFFFFFFF)  // a preset variant (trooper_*, *_aggressive...): go by its base character
                character = choose(Field<uint32_t>(actor, HALO4_ACTOR_BASE_CHARACTER), unit);
            if (character == 0xFFFFFFFF) { seen.insert(datum); continue; }
            if (Field<uint32_t>(unit, HALO4_OBJECT_PARENT) != 0xFFFFFFFF) { if (seated) ++*seated; continue; }
            picks.push_back({datum, character});
        }
        int done = 0;
        for (const Pick& pick : picks) {
            uint32_t fresh = ReplaceActor(state, objects, pick.actor, pick.character);
            seen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { seen.insert(fresh); ++done; }
            else if (failed) ++*failed;
        }
        return done;
    }

    void UpgradeAi(void** state, const Games::Settings& s, const Players& p, const Array& objects, bool all) {
        if (g_upgradeTo.empty()) {
            if (all) { LOG("Halo 4: upgrade AI: no AI to upgrade in this map"); Halo4Internal::Toast("No AI to upgrade in this map"); }
            return;
        }
        int seated = 0, failed = 0;
        int done = ReplaceAi(state, p, objects, s.upgradeAiRange, all, g_upgradeSeen, &seated, &failed,
                             [&](uint32_t character, char*) -> uint32_t {
                                 auto to = g_upgradeTo.find(character);
                                 if (to == g_upgradeTo.end() || (to->second.ally && !s.upgradeAiAllies))
                                     return 0xFFFFFFFF;
                                 return to->second.character;
                             });
        if (all || done || failed)
            LOG("Halo 4: upgrade AI (%s): %d upgraded, %d failed, %d seated skipped", all ? "button" : "auto", done,
                failed, seated);
        if (all) {
            char text[96];
            if (done) sprintf_s(text, "Upgraded %d AI", done);
            else sprintf_s(text, seated ? "No AI to upgrade (%d in vehicles)" : "No AI to upgrade", seated);
            Halo4Internal::Toast(text);
        }
    }

    // Random swap: each AI becomes a fresh one of a random character, where it
    // stands, in its squad and on its team. A failed pick is retried with
    // another, up to 3 times. Seated AI wait until they're out. Characters
    // that live in an unloaded designer zone aren't picked (a spawn there would
    // fail anyway, and loading zones for a swap is unwanted).
    void RandomSwap(void** state, const Games::Settings& s, const Players& p, const Array& objects, bool all) {
        if (g_pool.size() < 2) {
            if (all) { LOG("Halo 4: random swap: no AI to swap in this map"); Halo4Internal::Toast("No AI to swap in this map"); }
            return;
        }
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        std::vector<uint32_t> pool;
        bool poolBuilt = false;
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
            char* unit = ObjectFromDatum(objects, Field<uint32_t>(actor, HALO4_ACTOR_UNIT));
            float* health = unit ? Health(unit) : nullptr;
            if (!unit || p.Contains(unit) || !health || *health <= 0.0f) continue;
            uint32_t current = Field<uint32_t>(actor, HALO4_ACTOR_CHARACTER);
            // Only plain characters of the pool's kind (so not Spartans, pilots, ...); allies on request.
            auto name = g_nameOf.find(current);
            if (name == g_nameOf.end()) {  // a preset variant: go by its base character
                current = Field<uint32_t>(actor, HALO4_ACTOR_BASE_CHARACTER);
                name = g_nameOf.find(current);
            }
            if (name == g_nameOf.end() || (!s.randomSwapAllies && IsAlly(state, unit, p))) {
                g_swapSeen.insert(datum);
                continue;
            }
            if (Field<uint32_t>(unit, HALO4_OBJECT_PARENT) != 0xFFFFFFFF) { ++seated; continue; }
            if (!poolBuilt) {
                poolBuilt = true;
                for (uint32_t c : g_pool)
                    if (!NeedsZone(c)) pool.push_back(c);
            }
            if (pool.size() < 2) { g_swapSeen.insert(datum); continue; }
            uint32_t pick = current;
            for (int tries = 0; tries < 8 && pick == current; ++tries)
                pick = pool[(size_t)(Random01() * pool.size()) % pool.size()];
            if (pick == current) { g_swapSeen.insert(datum); continue; }
            picks.push_back({datum, pick});
        }
        int done = 0, failed = 0;
        for (const Pick& pick : picks) {
            uint32_t fresh = ReplaceActor(state, objects, pick.actor, pick.character);
            for (int again = 0; fresh == 0xFFFFFFFF && again < 3; ++again)
                fresh = ReplaceActor(state, objects, pick.actor, pool[(size_t)(Random01() * pool.size()) % pool.size()]);
            g_swapSeen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { g_swapSeen.insert(fresh); ++done; }
            else ++failed;
        }
        if (all || done || failed)
            LOG("Halo 4: random swap (%s): %d swapped, %d failed, %d seated skipped, pool %zu", all ? "button" : "auto",
                done, failed, seated, pool.size());
        if (all) {
            char text[96];
            if (picks.empty()) sprintf_s(text, seated ? "No AI to swap (%d in vehicles)" : "No AI to swap", seated);
            else sprintf_s(text, "Swapped %d AI", done);
            Halo4Internal::Toast(text);
        }
    }

    // --- Squads: members waiting for a designer zone ---------------------------
    struct SquadMember {
        uint32_t tag;
        float position[3];
        float yaw;
    };
    std::vector<SquadMember> g_pending;
    ULONGLONG g_squadDeadline = 0;
    int g_squadTeam = 0;
    std::string g_squadName;
    int g_squadMade = 0, g_squadTotal = 0;

    void ContinueSquadWait(void** state, const Array& objects) {
        if (g_pending.empty()) return;
        ULONGLONG now = GetTickCount64();
        for (size_t i = 0; i < g_pending.size();) {
            SquadMember& m = g_pending[i];
            if (NeedsZone(m.tag)) { ++i; continue; }  // its zone hasn't loaded yet
            uint32_t actor = Halo4Internal::SpawnActor(state, m.position, m.yaw, m.tag, g_squadTeam);
            if (actor != 0xFFFFFFFF) {
                Halo4Internal::Follow(state, objects, actor, m.tag, g_squadTeam);
                ++g_squadMade;
            }
            g_pending.erase(g_pending.begin() + i);  // loaded: made, or can't be
        }
        if (g_pending.empty() || now > g_squadDeadline) {
            char text[96];
            sprintf_s(text, g_squadMade == g_squadTotal ? "Spawned %s" : "Spawned %s (%d of %d)", g_squadName.c_str(),
                      g_squadMade, g_squadTotal);
            Halo4Internal::Toast(text);
            g_pending.clear();
        }
    }
}

namespace Halo4Upgrade {
    void Reset() {
        // The map changed: the actors and the character list are the old map's.
        g_chars.clear();
        g_upgradeTo.clear();
        g_nameOf.clear();
        g_pool.clear();
        g_upgradeSeen.clear();
        g_swapSeen.clear();
        g_pending.clear();
        g_localVersion = 0;
    }

    void Tick(void** state, const Games::Settings& s, const Players& p, const Array& objects) {
        if (!g_serialsInit) {  // a button pressed before this session isn't pressed now
            g_serialsInit = true;
            g_upgradeSerial = s.upgradeAiSerial;
            g_swapSerial = s.randomSwapSerial;
        }
        bool upgradeButton = s.upgradeAiSerial != g_upgradeSerial;
        bool swapButton = s.randomSwapSerial != g_swapSerial;
        g_upgradeSerial = s.upgradeAiSerial;
        g_swapSerial = s.randomSwapSerial;

        Guard("Halo 4 squad wait", [&] {
            if (!g_pending.empty() && state && objects.Valid() && Halo4Internal::Module())
                ContinueSquadWait(state, objects);
        });
        if (!upgradeButton && !swapButton && !s.upgradeAiAuto && !s.randomSwapAuto) return;
        if (!state || !objects.Valid() || !Halo4Internal::Module()) return;
        Guard("Halo 4 AI upgrade/swap", [&] {
            if (!state[HALO4_TLS_SLOT_ACTORS] || !Sync()) return;
            if (upgradeButton) UpgradeAi(state, s, p, objects, true);
            if (s.upgradeAiAuto) UpgradeAi(state, s, p, objects, false);
            if (swapButton) RandomSwap(state, s, p, objects, true);
            if (s.randomSwapAuto) RandomSwap(state, s, p, objects, false);
        });
    }

    // True if Upgrade AI has something to do in this map: `allies` asks about the marines'
    // ladder (storm_marine > storm_marine_oni, only in maps that have ONI marines), otherwise
    // about the enemies' ladders. Render thread; false until the map's characters are known.
    bool UpgradeApplicable(bool allies) {
        std::lock_guard<std::mutex> lock(g_listLock);
        return allies ? g_upgradeAllies : g_upgradeEnemies;
    }

    // Which squads this map can make (greyed out otherwise). Only recomputed
    // when the loaded scenario or the spawn list changes.
    void Apply(const Games::Settings&, Games::Status& status) {
        Guard("Halo 4 squads available", [&] {
            char* scnr = Halo4Tags::Scenario();
            const auto& spawnable = Halo4Internal::Characters();
            size_t count = spawnable.size();
            uint32_t first = count ? spawnable.front().datum : 0, last = count ? spawnable.back().datum : 0;
            if (scnr != g_renderScnr || count != g_renderCount || first != g_renderFirst || last != g_renderLast) {
                g_renderScnr = scnr;
                g_renderCount = count;
                g_renderFirst = first;
                g_renderLast = last;
                if (scnr && count) {
                    Rebuild(scnr, spawnable);
                } else {
                    std::lock_guard<std::mutex> lock(g_listLock);
                    g_shared.clear();
                    g_upgradeEnemies = g_upgradeAllies = false;
                    g_sharedScnr = nullptr;
                    ++g_sharedVersion;
                    for (uint64_t& word : g_available) word = 0;
                }
            }
        });
        std::lock_guard<std::mutex> lock(g_listLock);
        for (int w = 0; w < 4; ++w)
            if (w * 64 < Games::kSquadMaskBits) status.squadsAvailable[w] = g_available[w];
    }

    // Squads: every member is made around the aim point, a loose cluster (a
    // sunflower spiral, so any size fits), through the same actor spawn as a
    // single AI. Members the map lacks are skipped; members whose character is
    // in an unloaded designer zone wait for it (5 s), as a single spawn does.
    void SpawnSquad(void** state, const Players& p, const Array& objects, int index, int team) {
        Guard("Halo 4 squad spawn", [&] {
            if (index < 0 || index >= Games::kSquadCount) return;
            const Games::Squad& squad = Games::kSquads[index];
            if (!Games::SquadInTitle(squad, "Halo 4")) return;
            if (p.count == 0) { Halo4Internal::Toast("No player to spawn for"); return; }
            if (!Sync()) { LOG("Halo 4: squad %s: this map's characters aren't known yet", squad.name); return; }
            std::vector<uint32_t> tags = Games::SquadTags(squad, g_chars);
            if (tags.empty()) { Halo4Internal::Toast("This squad isn't in this map"); return; }

            float at[3], dir[3], facing;
            if (!Halo4Internal::AimPoint(state, p, at, dir, &facing)) {
                Halo4Internal::Toast("Nothing in range to spawn on");
                return;
            }
            float center[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                               at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
            float spacing = squad.large ? 1.5f : 0.9f;  // world units between neighbours

            g_pending.clear();
            int made = 0;
            for (size_t i = 0; i < tags.size(); ++i) {
                float radius = spacing * sqrtf((float)i);
                float angle = (float)i * 2.39996323f;  // the golden angle
                SquadMember m{tags[i], {center[0] + cosf(angle) * radius, center[1] + sinf(angle) * radius, center[2]},
                              facing};
                if (NeedsZone(m.tag)) {
                    Halo4Internal::LoadZoneFor(m.tag);
                    g_pending.push_back(m);
                    continue;
                }
                uint32_t actor = Halo4Internal::SpawnActor(state, m.position, m.yaw, m.tag, team);
                if (actor == 0xFFFFFFFF) continue;
                Halo4Internal::Follow(state, objects, actor, m.tag, team);
                ++made;
            }
            LOG("Halo 4: squad %s at %.2f %.2f %.2f: %d of %zu made, %zu waiting for a zone", squad.name, center[0],
                center[1], center[2], made, tags.size(), g_pending.size());
            g_squadName = squad.name;
            g_squadTeam = team;
            g_squadMade = made;
            g_squadTotal = (int)tags.size();
            char text[96];
            if (!g_pending.empty()) {
                g_squadDeadline = GetTickCount64() + kZoneLoadMs;
                sprintf_s(text, "Loading the rest of %s...", squad.name);
            } else if (made == 0) {
                sprintf_s(text, "Couldn't spawn %s here", squad.name);
            } else {
                sprintf_s(text, made == g_squadTotal ? "Spawned %s" : "Spawned %s (%d of %d)", squad.name, made,
                          g_squadTotal);
            }
            Halo4Internal::Toast(text);
        });
    }
}
