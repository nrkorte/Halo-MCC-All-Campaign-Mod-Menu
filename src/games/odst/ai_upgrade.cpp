// ODST: Upgrade AI, Random AI swap, Squads (see NOTES.md, "Upgrade AI and
// random AI teams (Reach)", "Squads (Reach)", the Halo 2 / Halo 3 ports: the
// same features, re-done on ODST's actors and squads; this is the Halo 3 file
// with ODST's offsets, names and ranks).
//
// Upgrade AI and Random AI swap replace an AI by a fresh one: the new actor is
// made where the old one stands (same facing), moved into the old one's squad
// (so the mission's scripts still count it), given the old team, and then the
// old one is erased. New before old, so the squad is never empty. An actor
// knows its squad by index (actor +0x30); squads are TLS slot 10.

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
#include <algorithm>
#include "internal.h"
#include "tags.h"

using namespace OdstCheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kSpawnBackOff = 0.3f;  // as the spawn key: back along the ray from the hit,
    constexpr float kSpawnLift = 0.25f;    // and up, so nobody is inside the floor
    constexpr ULONGLONG kZoneLoadMs = 5000;

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // A character with a unit somewhere up its parent chain (templates such
    // as "generic" have none and can't be spawned). Same test as the spawn list.
    bool HasUnit(uint32_t character) {
        __try {
            for (int depth = 0; depth < 8 && character != 0xFFFFFFFF; ++depth) {
                char* tag = OdstTags::Get(character);
                if (!tag) return false;
                if (Field<uint32_t>(tag, ODST_CHAR_UNIT) != 0xFFFFFFFF) return true;
                character = Field<uint32_t>(tag, ODST_CHAR_PARENT);
            }
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Characters that never take part: templates, turrets, pilots, wounded and
    // set-dressing humans, the hero sergeant and the ONI operative the missions'
    // scripts depend on, cinematic engineers, civilians (police), and the Flood
    // and sentinels, whose characters ship in ODST's data but no campaign
    // mission uses. They're neither swapped nor made by squads.
    bool Protected(const std::string& name) {
        if (name.rfind("null", 0) == 0 || name.rfind("generic", 0) == 0) return true;
        for (const char* part : {"turret", "pilot", "wounded", "civilian", "civilain", "hero", "oni_op", "_cin", "flood",
                                 "pureform", "sentinel"})
            if (name.find(part) != std::string::npos) return true;
        return false;
    }

    // --- The map's characters (render thread builds, game thread copies) ------
    // OdstTags' name cache isn't locked, and it's reloaded on the render
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
        OdstTags::LoadNames();
        std::vector<Games::TagChoice> list;
        for (auto& c : OdstTags::ListGroup(ODST_TAG_CHAR))
            if (HasUnit(c.datum) && !Protected(c.name)) list.push_back(c);
        Games::Status scratch;
        for (int i = 0; i < Games::kSquadCount; ++i) {
            const Games::Squad& squad = Games::kSquads[i];
            if (Games::SquadInTitle(squad, "Halo 3: ODST") && !Games::SquadTags(squad, list).empty())
                Games::SetSquadAvailable(scratch, i);
        }
        LOG("ODST: squads/upgrade: %zu spawnable characters in this map", list.size());
        std::lock_guard<std::mutex> lock(g_listLock);
        g_shared = list;
        g_sharedScnr = scnr;
        ++g_sharedVersion;
        g_available[0] = scratch.squadsAvailable[0];
        g_available[1] = scratch.squadsAvailable[1];
    }

    // --- Designer zones --------------------------------------------------------
    // spawn.cpp's DesignerZonesOf for characters (that file keeps it private):
    // the zones whose character list names this palette entry.
    uint32_t ActiveZones() { return *(uint32_t*)(OdstInternal::Module() + OFFSET_ODST_DESIGNER_ZONES_ACTIVE); }

    uint32_t ZonesOf(uint32_t tag) {
        char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
        if (!scnr) return 0;
        int index = -1;
        int palette = OdstTags::BlockCount(scnr, ODST_SCNR_CHARACTER_PALETTE);
        for (int i = 0; i < palette && i < 1024 && index < 0; ++i) {
            char* entry = OdstTags::BlockElement(scnr, ODST_SCNR_CHARACTER_PALETTE, ODST_PALETTE_ENTRY_SIZE, i);
            if (entry && Field<uint32_t>(entry, ODST_PALETTE_ENTRY_DATUM) == tag) index = i;
        }
        if (index < 0) return 0;
        uint32_t zones = 0;
        int count = OdstTags::BlockCount(scnr, ODST_SCNR_DESIGNER_ZONES);
        for (int z = 0; z < count && z < 32; ++z) {
            char* zone = OdstTags::BlockElement(scnr, ODST_SCNR_DESIGNER_ZONES, ODST_SCNR_DESIGNER_ZONE_SIZE, z);
            int entries = OdstTags::BlockCount(zone, ODST_DESIGNER_ZONE_CHARACTERS);
            for (int e = 0; e < entries && e < 256; ++e) {
                char* entry = OdstTags::BlockElement(zone, ODST_DESIGNER_ZONE_CHARACTERS, 2, e);
                if (entry && Field<int16_t>(entry, 0) == index) zones |= 1u << z;
            }
        }
        return zones;
    }

    // True if the character only exists in designer zones and none is loaded now.
    bool NeedsZone(uint32_t tag) {
        uint32_t zones = ZonesOf(tag);
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

    float Random01() {
        if (g_rng == 0x2545F491u) g_rng ^= (uint32_t)GetTickCount64();
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (g_rng >> 8) / 16777216.0f;
    }

    // Rank ladders by character name (the campaign's 'char' tags, read from the
    // maps' name tables); a rank the map lacks is skipped, so the next present one is
    // the upgrade. The marine ladders are allies' (only with "Allies too").
    // Single-rank classes (jackal snipers, hunters, engineers, brute bodyguards /
    // stalkers / trackers / phantoms, the named ODSTs) don't upgrade.
    struct Ladder {
        std::vector<const char*> ranks;
        bool ally;
    };
    const Ladder kLadders[] = {
        {{"grunt", "grunt_major", "grunt_heavy", "grunt_ultra", "grunt_specops"}, false},
        {{"jackal", "jackal_major"}, false},
        {{"elite", "elite_major", "elite_specops", "elite_specops_commander"}, false},
        {{"brute", "brute_major", "brute_ultra", "brute_captain", "brute_captain_major", "brute_captain_ultra",
          "brute_chieftain_armor", "brute_chieftain_weapon"},
         false},
        {{"brute_jumppack", "brute_jumppack_major", "brute_jumppack_ultra"}, false},
        {{"bugger", "bugger_major", "bugger_ultra", "bugger_captain", "bugger_captain_major"}, false},
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
                    LOG("ODST: upgrade AI: %s -> %s", previousName, name);
                }
                previous = have->second;
                previousName = name;
            }
        }
    }

    // Takes the render thread's list when it's for the loaded map. False if
    // there's nothing to work with.
    bool Sync() {
        char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
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

    int16_t TeamOf(char* unit) { return Field<int16_t>(unit, ODST_UNIT_TEAM); }

    // Player 1's side: the player and human teams, their own team, and whatever
    // the game's allegiance table says is friends with it.
    bool IsAlly(void** state, char* unit, const Players& p) {
        int team = TeamOf(unit);
        if (team == ODST_TEAM_PLAYER || team == ODST_TEAM_HUMAN) return true;
        if (p.count > 0) {
            int mine = TeamOf(p.units[0]);
            if (team == mine) return true;
            return OdstInternal::TeamsFriends(state, mine, team);
        }
        return false;
    }

    bool InRange(const Players& p, char* unit, float metres) {
        if (metres <= 0.0f || p.count == 0) return true;
        const float* a = &Field<float>(unit, ODST_OBJECT_POSITION);
        const float* b = &Field<float>(p.units[0], ODST_OBJECT_POSITION);
        float limit = metres / Fun::kMetresPerUnit;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz <= limit * limit;
    }

    using ActorMigrateFn = void(__fastcall*)(uint32_t actor, int16_t squad);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor, bool);

    Blam::DataArray* Actors(void** state) { return (Blam::DataArray*)state[ODST_TLS_SLOT_ACTORS]; }

    char* ActorOf(Blam::DataArray* actors, uint32_t datum) {
        uint16_t index = datum & 0xFFFF;
        if (!actors || datum == 0xFFFFFFFF || index >= actors->highWater) return nullptr;
        char* actor = actors->Entry(index);
        return *(uint16_t*)actor == (datum >> 16) ? actor : nullptr;
    }

    // Replacements waiting to be seated in the vehicle their predecessor was in.
    struct Seating {
        uint32_t unit, vehicle;
        int seat;
        ULONGLONG deadline, nextTry;
    };
    std::vector<Seating> g_seating;
    // Characters whose spawn was refused on this map (resources not loaded here): not picked again.
    std::unordered_set<uint32_t> g_badChars;

    void ContinueSeating(Blam::DataArray* objects) {
        ULONGLONG now = GetTickCount64();
        for (size_t i = 0; i < g_seating.size();) {
            Seating& t = g_seating[i];
            char* unit = Blam::ObjectFromDatum(objects, t.unit);
            bool done = !unit || !Blam::ObjectFromDatum(objects, t.vehicle) ||
                        Field<uint32_t>(unit, ODST_OBJECT_PARENT) != 0xFFFFFFFF;
            if (!done && now > t.deadline) {
                LOG("ODST: swapped AI %08X never got into vehicle %08X seat %d", t.unit, t.vehicle, t.seat);
                done = true;
            } else if (!done && now >= t.nextTry) {
                t.nextTry = now + 400;
                if (OdstVehicles::SeatAiUnit(t.unit, t.vehicle, t.seat))
                    LOG("ODST: seating swapped AI %08X in vehicle %08X seat %d", t.unit, t.vehicle, t.seat);
            }
            if (done) g_seating.erase(g_seating.begin() + i);
            else ++i;
        }
    }

    // Replaces the AI behind `actorDatum` by a fresh `character`. Returns the
    // new actor datum, or -1 (the old one is then left alone).
    uint32_t ReplaceActor(void** state, Blam::DataArray* objects, uint32_t actorDatum, uint32_t character) {
        Blam::DataArray* actors = Actors(state);
        char* actor = ActorOf(actors, actorDatum);
        uint32_t unitDatum = actor ? Field<uint32_t>(actor, ODST_ACTOR_UNIT) : 0xFFFFFFFF;
        char* unit = Blam::ObjectFromDatum(objects, unitDatum);
        if (!unit || !OdstTags::Get(character)) return 0xFFFFFFFF;
        float position[3];
        memcpy(position, &Field<float>(unit, ODST_OBJECT_POSITION), sizeof(position));
        position[2] += kSpawnLift;
        const float* forward = &Field<float>(unit, ODST_OBJECT_FORWARD);
        float yaw = atan2f(forward[1], forward[0]);
        if (yaw < 0.0f) yaw += 2.0f * kPi;
        int16_t team = TeamOf(unit);
        int32_t squad = Field<int32_t>(actor, ODST_ACTOR_SQUAD_INDEX);
        // A seated AI: its replacement is put back into the same seat once the old one has left it.
        uint32_t vehicle = Field<uint32_t>(unit, ODST_OBJECT_PARENT);
        int seat = Field<int16_t>(unit, ODST_UNIT_SEAT);

        uint32_t fresh = OdstInternal::SpawnActor(state, position, yaw, character, 0);
        if (fresh == 0xFFFFFFFF) return fresh;
        char* base = OdstInternal::Module();
        if (squad != -1) ((ActorMigrateFn)(base + OFFSET_ODST_ACTOR_MIGRATE))(fresh, (int16_t)squad);
        OdstInternal::SetActorTeam(fresh, team);  // attaching may have changed it
        OdstInternal::Refollow(state, objects, unitDatum, fresh, character);  // a mod-spawned AI stays followed
        // ai_erase's per-actor core: frees the actor and deletes its unit.
        ((ActorEraseFn)(base + OFFSET_ODST_ACTOR_ERASE))(actorDatum, false);
        if (vehicle != 0xFFFFFFFF && seat >= 0)
            if (char* freshActor = ActorOf(actors, fresh))
                g_seating.push_back({Field<uint32_t>(freshActor, ODST_ACTOR_UNIT), vehicle, seat,
                                     GetTickCount64() + 4000, 0});
        return fresh;
    }

    // Walks the live AI (not players, not the dead, not seated) and replaces
    // those `choose` returns a character for. `all` is the button (everyone,
    // again); otherwise each AI once. Returns how many were replaced.
    template <typename Choose>
    int ReplaceAi(void** state, const Players& p, Blam::DataArray* objects, float range, bool all,
                  std::unordered_set<uint32_t>& seen, int* seated, int* failed, Choose choose) {
        Blam::DataArray* actors = Actors(state);
        if (!actors) return 0;
        struct Pick { uint32_t actor, character; };
        std::vector<Pick> picks;  // collected first: replacing adds actors
        int count = actors->highWater;
        for (int i = 0; i < count; ++i) {
            char* actor = actors->Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && seen.count(datum)) continue;
            char* unit = Blam::ObjectFromDatum(objects, Field<uint32_t>(actor, ODST_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, ODST_OBJECT_HEALTH) <= 0.0f) continue;
            if (!InRange(p, unit, range)) continue;
            uint32_t character = choose(Field<uint32_t>(actor, ODST_ACTOR_CHARACTER), unit);
            if (character == 0xFFFFFFFF) { seen.insert(datum); continue; }
            if (Field<uint32_t>(unit, ODST_OBJECT_PARENT) != 0xFFFFFFFF) { if (seated) ++*seated; continue; }  // seated AI wait until they're out (as in the other games)
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

    void UpgradeAi(void** state, const Games::Settings& s, const Players& p, Blam::DataArray* objects, bool all) {
        if (g_upgradeTo.empty()) {
            if (all) { LOG("ODST: upgrade AI: no AI to upgrade in this map"); OdstInternal::Toast("No AI to upgrade in this map"); }
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
            LOG("ODST: upgrade AI (%s): %d upgraded, %d failed, %d seated skipped", all ? "button" : "auto", done,
                failed, seated);
        if (all) {
            char text[96];
            if (done) sprintf_s(text, "Upgraded %d AI", done);
            else sprintf_s(text, seated ? "No AI to upgrade (%d in vehicles)" : "No AI to upgrade", seated);
            OdstInternal::Toast(text);
        }
    }

    // Random swap: each AI becomes a fresh one of a random character, where it
    // stands, in its squad and on its team. A failed pick is retried with
    // another, up to 3 times. Seated AI wait until they're out. Characters
    // that live in an unloaded designer zone aren't picked (a spawn there would
    // fail anyway, and loading zones for a swap is unwanted).
    void RandomSwap(void** state, const Games::Settings& s, const Players& p, Blam::DataArray* objects, bool all) {
        if (g_pool.size() < 2) {
            if (all) { LOG("ODST: random swap: no AI to swap in this map"); OdstInternal::Toast("No AI to swap in this map"); }
            return;
        }
        Blam::DataArray* actors = Actors(state);
        if (!actors) return;
        std::vector<uint32_t> pool;
        bool poolBuilt = false;
        struct Pick { uint32_t actor, character; };
        std::vector<Pick> picks;
        int seated = 0;
        int count = actors->highWater;
        for (int i = 0; i < count; ++i) {
            char* actor = actors->Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_swapSeen.count(datum)) continue;
            char* unit = Blam::ObjectFromDatum(objects, Field<uint32_t>(actor, ODST_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, ODST_OBJECT_HEALTH) <= 0.0f) continue;
            uint32_t current = Field<uint32_t>(actor, ODST_ACTOR_CHARACTER);
            // Only plain characters of the pool's kind (so not Johnson, Cortana, ...); allies on request.
            auto name = g_nameOf.find(current);
            if (name == g_nameOf.end()) {
                g_swapSeen.insert(datum);
                continue;
            }
            // Not marked as seen: an AI that changes sides later (or "allies too" being switched on) is
            // looked at again.
            if (!s.randomSwapAllies && IsAlly(state, unit, p)) continue;
            if (Field<uint32_t>(unit, ODST_OBJECT_PARENT) != 0xFFFFFFFF) { ++seated; continue; }  // seated AI wait until they're out
            if (!poolBuilt) {
                poolBuilt = true;
                for (uint32_t c : g_pool)
                    if (!NeedsZone(c) && !g_badChars.count(c)) pool.push_back(c);
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
            uint32_t tried = pick.character;
            for (int again = 0; fresh == 0xFFFFFFFF && again < 8; ++again) {
                g_badChars.insert(tried);
                pool.erase(std::remove(pool.begin(), pool.end(), tried), pool.end());
                if (pool.empty()) break;
                tried = pool[(size_t)(Random01() * pool.size()) % pool.size()];
                fresh = ReplaceActor(state, objects, pick.actor, tried);
            }
            g_swapSeen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { g_swapSeen.insert(fresh); ++done; }
            else ++failed;
        }
        if (all || done || failed)
            LOG("ODST: random swap (%s): %d swapped, %d failed, %d seated skipped, pool %zu", all ? "button" : "auto",
                done, failed, seated, pool.size());
        if (all) {
            char text[96];
            if (picks.empty()) sprintf_s(text, seated ? "No AI to swap (%d in vehicles)" : "No AI to swap", seated);
            else sprintf_s(text, "Swapped %d AI", done);
            OdstInternal::Toast(text);
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

    void ContinueSquadWait(void** state, Blam::DataArray* objects) {
        if (g_pending.empty()) return;
        ULONGLONG now = GetTickCount64();
        for (size_t i = 0; i < g_pending.size();) {
            SquadMember& m = g_pending[i];
            if (NeedsZone(m.tag)) { ++i; continue; }  // its zone hasn't loaded yet
            uint32_t actor = OdstInternal::SpawnActor(state, m.position, m.yaw, m.tag, g_squadTeam);
            if (actor != 0xFFFFFFFF) {
                OdstInternal::Follow(state, objects, actor, m.tag, g_squadTeam);
                ++g_squadMade;
            }
            g_pending.erase(g_pending.begin() + i);  // loaded: made, or can't be
        }
        if (g_pending.empty() || now > g_squadDeadline) {
            char text[96];
            sprintf_s(text, g_squadMade == g_squadTotal ? "Spawned %s" : "Spawned %s (%d of %d)", g_squadName.c_str(),
                      g_squadMade, g_squadTotal);
            OdstInternal::Toast(text);
            g_pending.clear();
        }
    }
}

namespace OdstUpgrade {
    void Reset() {
        // The map changed: the actors and the character list are the old map's.
        g_chars.clear();
        g_upgradeTo.clear();
        g_nameOf.clear();
        g_pool.clear();
        g_upgradeSeen.clear();
        g_swapSeen.clear();
        g_seating.clear();
        g_badChars.clear();
        g_pending.clear();
        g_localVersion = 0;
    }

    void Tick(void** state, const Games::Settings& s, const Players& p, Blam::DataArray* objects) {
        if (!g_serialsInit) {  // a button pressed before this session isn't pressed now
            g_serialsInit = true;
            g_upgradeSerial = s.upgradeAiSerial;
            g_swapSerial = s.randomSwapSerial;
        }
        // "Allies too" changed: AI skipped before for being allies get another look.
        static bool s_upAllies = false, s_swapAllies = false;
        if (s.upgradeAiAllies != s_upAllies) { s_upAllies = s.upgradeAiAllies; g_upgradeSeen.clear(); }
        if (s.randomSwapAllies != s_swapAllies) { s_swapAllies = s.randomSwapAllies; g_swapSeen.clear(); }
        bool upgradeButton = s.upgradeAiSerial != g_upgradeSerial;
        bool swapButton = s.randomSwapSerial != g_swapSerial;
        g_upgradeSerial = s.upgradeAiSerial;
        g_swapSerial = s.randomSwapSerial;

        Guard("ODST squad wait", [&] {
            if (!g_pending.empty() && objects && OdstInternal::Module()) ContinueSquadWait(state, objects);
        });
        Guard("ODST AI seating", [&] {
            if (!g_seating.empty() && objects && OdstInternal::Module()) ContinueSeating(objects);
        });
        if (!upgradeButton && !swapButton && !s.upgradeAiAuto && !s.randomSwapAuto) return;
        if (!state || !objects || !OdstInternal::Module()) return;
        Guard("ODST AI upgrade/swap", [&] {
            if (!Sync()) return;
            if (upgradeButton) UpgradeAi(state, s, p, objects, true);
            if (s.upgradeAiAuto) UpgradeAi(state, s, p, objects, false);
            if (swapButton) RandomSwap(state, s, p, objects, true);
            if (s.randomSwapAuto) RandomSwap(state, s, p, objects, false);
        });
    }

    // Which squads this map can make (greyed out otherwise). Only recomputed
    // when the loaded scenario changes.
    void Apply(const Games::Settings&, Games::Status& status) {
        Guard("ODST squads available", [&] {
            char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
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
    // single AI. Members the map lacks are skipped; members whose character is
    // in an unloaded designer zone wait for it (5 s), as a single spawn does.
    void SpawnSquad(void** state, const Players& p, Blam::DataArray* objects, int index, int team) {
        Guard("ODST squad spawn", [&] {
            if (index < 0 || index >= Games::kSquadCount) return;
            const Games::Squad& squad = Games::kSquads[index];
            if (!Games::SquadInTitle(squad, "Halo 3: ODST")) return;
            if (p.count == 0) { OdstInternal::Toast("No player to spawn for"); return; }
            if (!Sync()) { LOG("ODST: squad %s: this map's characters aren't known yet", squad.name); return; }
            std::vector<uint32_t> tags = Games::SquadTags(squad, g_chars);
            if (tags.empty()) { OdstInternal::Toast("This squad isn't in this map"); return; }

            float at[3], dir[3], facing;
            if (!OdstInternal::AimPoint(state, p, at, dir, &facing)) {
                OdstInternal::Toast("Nothing in range to spawn on");
                return;
            }
            float center[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                               at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
            float spacing = squad.large ? 1.5f : 0.9f;  // world units between neighbours

            g_pending.clear();
            int made = 0;
            bool loading = false;
            for (size_t i = 0; i < tags.size(); ++i) {
                float radius = spacing * sqrtf((float)i);
                float angle = (float)i * 2.39996323f;  // the golden angle
                SquadMember m{tags[i], {center[0] + cosf(angle) * radius, center[1] + sinf(angle) * radius, center[2]},
                              facing};
                if (NeedsZone(m.tag)) {
                    loading |= OdstInternal::LoadZoneFor(m.tag);
                    g_pending.push_back(m);
                    continue;
                }
                uint32_t actor = OdstInternal::SpawnActor(state, m.position, m.yaw, m.tag, team);
                if (actor == 0xFFFFFFFF) continue;
                OdstInternal::Follow(state, objects, actor, m.tag, team);
                ++made;
            }
            LOG("ODST: squad %s at %.2f %.2f %.2f: %d of %zu made, %zu waiting for a zone", squad.name, center[0],
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
            (void)loading;
            OdstInternal::Toast(text);
        });
    }
}
