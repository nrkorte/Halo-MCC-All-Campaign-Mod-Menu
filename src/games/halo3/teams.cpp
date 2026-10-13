// Halo 3 teams and killing: Nuke, Despawn all custom AI, Random AI teams, Your
// team and "Allies never turn against you". Ported from Halo 2's
// (halo2/main_thread.cpp: Nuke, DespawnCustomAi, RandomTeams, SetPlayerTeam,
// NoBetrayal); the offsets are in teams_offsets.h. Runs on the game thread,
// once per tick, inside SEH.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "core/log.h"
#include "internal.h"
#include "teams_offsets.h"

using namespace Halo3Cheats;

namespace {
    constexpr float kMetresPerUnit = 3.048f;  // a world unit is 10 feet

    // Halo 3's team numbers (scnr squad Team enum; live: brutes, grunts,
    // jackals and hunters are 7 on 070_waste, marines 2).
    constexpr int kTeamHuman = 2;
    constexpr int kTeamCovenant = 3;
    constexpr int kTeamFlood = 4;
    constexpr int kTeamSentinel = 5;
    constexpr int kTeamHeretic = 6;
    constexpr int kTeamProphet = 7;
    constexpr int kTeamGuilty = 8;  // allied with every faction (live): a neutral "scripted" team
    constexpr int kRogueChoice = 4;  // the picker's "Rogue" (a random unused team, made hostile to all)

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    using UnitKillFn = void(__fastcall*)(uint32_t unit);
    using AllegianceFn = void(__fastcall*)(int16_t a, int16_t b);
    using AllegianceRemoveFn = bool(__fastcall*)(int16_t a, int16_t b);
    using RecordUpdateFn = void(__fastcall*)(char* record, bool broken, bool remove, bool flag);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor, bool dispose);

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // Serials: a press from before this session's first tick is ignored.
    bool g_haveSerials = false;
    uint32_t g_nukeSerial = 0;
    uint32_t g_despawnSerial = 0;
    uint32_t g_teamsSerial = 0;

    uint32_t g_rng = 0;
    float Random01() {
        if (!g_rng) g_rng = 0x2545F491u ^ (uint32_t)GetTickCount64() | 1u;
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (float)(g_rng & 0xFFFFFF) / 16777216.0f;
    }

    // --- Shared bits ------------------------------------------------------------

    char* Allegiance(void** state) { return state ? (char*)state[HALO3_TLS_SLOT_ALLEGIANCE] : nullptr; }

    bool Campaign(void** state) {
        char* globals = state ? (char*)state[HALO3_TLS_SLOT_GAME_GLOBALS] : nullptr;
        return globals && Field<uint8_t>(globals, HALO3_GAME_GLOBALS_MODE) == HALO3_GAME_MODE_CAMPAIGN;
    }

    int AllegianceCount(char* g) {
        int n = Field<uint16_t>(g, HALO3_ALLEG_COUNT);
        return n > HALO3_ALLEG_MAX_RECORDS ? 0 : n;
    }
    char* AllegianceRecord(char* g, int i) { return g + HALO3_ALLEG_RECORDS + i * HALO3_ALLEG_RECORD_SIZE; }

    // Player 1's team (the unit's), team 1 if there is no player right now.
    int PlayerTeam(const Players& p) {
        int t = p.count > 0 ? Field<int16_t>(p.units[0], HALO3_UNIT_TEAM) : HALO3_TEAM_PLAYER;
        return t < 0 || t >= HALO3_TEAM_COUNT ? HALO3_TEAM_PLAYER : t;
    }

    bool InRange(const Players& p, char* object, float metres) {
        if (metres <= 0.0f) return true;
        if (p.count == 0) return false;
        const float* a = &Field<float>(p.units[0], HALO3_OBJECT_POSITION);
        const float* b = &Field<float>(object, HALO3_OBJECT_POSITION);
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        float range = metres / kMetresPerUnit;
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

    // The actor with this datum, or nullptr.
    char* ActorOf(Blam::DataArray* actors, uint32_t datum) {
        if (!actors || datum == 0xFFFFFFFF) return nullptr;
        uint16_t index = datum & 0xFFFF;
        if (index >= actors->highWater) return nullptr;
        char* actor = actors->Entry(index);
        return *(uint16_t*)actor == (datum >> 16) && (datum >> 16) != 0 ? actor : nullptr;
    }

    void Say(const char* format, int a, int b = 0) {
        char text[96];
        sprintf_s(text, format, a, b);
        Halo3Internal::Toast(text);
    }

    // --- Nuke -------------------------------------------------------------------
    // Every AI unit that isn't a player (with `spare`, nor friends of player 1's
    // team) loses its cannot-take-damage / cannot-die flags and shield and is
    // killed with unit_kill (the damage multiplier's call).
    void Nuke(void** state, const Players& p, Blam::DataArray* objects, bool spare) {
        auto actors = (Blam::DataArray*)state[HALO3_TLS_SLOT_ACTORS];
        if (!actors) return;
        int mine = PlayerTeam(p);
        int killed = 0, spared = 0;
        for (int i = 0; i < actors->highWater; ++i) {
            char* actor = actors->Entry(i);
            if (*(uint16_t*)actor == 0) continue;
            uint32_t datum = Field<uint32_t>(actor, HALO3_ACTOR_UNIT);
            char* unit = Blam::ObjectFromDatum(objects, datum);
            if (!unit || p.Contains(unit)) continue;
            if (Field<float>(unit, HALO3_OBJECT_HEALTH) <= 0.0f) continue;
            if (spare && Halo3Internal::TeamsFriends(state, mine, Field<int16_t>(unit, HALO3_UNIT_TEAM))) {
                ++spared;
                continue;
            }
            Field<uint32_t>(unit, HALO3_OBJECT_DAMAGE_FLAGS) &=
                ~(HALO3_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE);
            Field<float>(unit, HALO3_OBJECT_SHIELD) = 0.0f;
            Field<float>(unit, HALO3_OBJECT_HEALTH) = 0.0001f;
            Engine<UnitKillFn>(OFFSET_HALO3_UNIT_KILL)(datum);
            ++killed;
        }
        LOG("Halo 3: nuke: %d AI killed, %d allies spared", killed, spared);
        Say(killed ? "Nuked %d AI" : "No AI to nuke", killed);
    }

    // --- Despawn all custom AI ----------------------------------------------------
    // Every AI the mod made (all go through Halo3Internal::Follow), alive or a
    // body: actor erase if the unit still has its actor, else object_delete.
    void DespawnCustomAi(void** state, Blam::DataArray* objects) {
        auto actors = (Blam::DataArray*)state[HALO3_TLS_SLOT_ACTORS];
        uint32_t units[512];
        int count = Halo3Internal::FollowedUnits(units, (int)_countof(units));
        int removed = 0;
        for (int i = 0; i < count; ++i) {
            char* unit = Blam::ObjectFromDatum(objects, units[i]);
            if (!unit) continue;
            uint32_t actorDatum = Field<uint32_t>(unit, HALO3_UNIT_ACTOR_INDEX);
            char* actor = ActorOf(actors, actorDatum);
            if (actor && Field<uint32_t>(actor, HALO3_ACTOR_UNIT) == units[i])
                Engine<ActorEraseFn>(OFFSET_HALO3_ACTOR_ERASE)(actorDatum, false);
            else
                Halo3Internal::DeleteObject(units[i]);
            ++removed;
        }
        Halo3Internal::ForgetFollowed();
        LOG("Halo 3: despawn all custom AI: %d removed", removed);
        Say(removed ? "Despawned %d custom AI" : "No custom AI to despawn", removed);
    }

    // --- Random AI teams ------------------------------------------------------------
    // Each AI actor in range goes on one of the first N of human, covenant, flood,
    // sentinel and two rogue teams (the rogue ones are made hostile to everyone,
    // each other included, by the spawn picker's Rogue path). `all` = the button
    // (every AI again); otherwise each actor datum once per map.
    std::unordered_set<uint32_t> g_teamsSeen;

    int RandomTeamFor(void** state, int slot) {
        switch (slot) {
            case 0: return kTeamHuman;
            case 1: return kTeamCovenant;
            case 2: return kTeamFlood;
            case 3: return kTeamSentinel;
            default: return Halo3Internal::ResolveSpawnTeam(state, kRogueChoice);  // 9 or 10, hostile to all
        }
    }

    void RandomTeams(void** state, const Players& p, Blam::DataArray* objects, const Games::Settings& s, bool all) {
        auto actors = (Blam::DataArray*)state[HALO3_TLS_SLOT_ACTORS];
        if (!actors) return;
        int factions = (int)(s.randomTeamsFactions + 0.5f);
        factions = factions < 2 ? 2 : (factions > 6 ? 6 : factions);
        if (g_teamsSeen.size() > 20000) g_teamsSeen.clear();
        int changed = 0;
        for (int i = 0; i < actors->highWater; ++i) {
            char* actor = actors->Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_teamsSeen.count(datum)) continue;
            char* unit = Blam::ObjectFromDatum(objects, Field<uint32_t>(actor, HALO3_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, HALO3_OBJECT_HEALTH) <= 0.0f) continue;
            if (!InRange(p, unit, s.randomTeamsRange)) continue;
            g_teamsSeen.insert(datum);
            int team = RandomTeamFor(state, (int)(Random01() * factions) % factions);
            if (team > 0) Halo3Internal::SetActorTeam(datum, (int16_t)team);
            ++changed;
        }
        if (all || changed)
            LOG("Halo 3: random AI teams (%s): %d AI over %d teams", all ? "button" : "auto", changed, factions);
        if (all) Say(changed ? "Shuffled %d AI over %d teams" : "No AI in range", changed, factions);
    }

    // --- Your team --------------------------------------------------------------------
    // The player stays on team 1 (AI cache team and hostility in their perception
    // props, so changing the player's team doesn't reach them). The allegiances
    // that involve team 1 are replaced instead, through the engine's ai_allegiance
    // and ai_allegiance_remove cores, which also refresh the props of the AI that
    // are already looking at each other: Covenant = the Covenant teams (3, prophet
    // 7, heretic 6: live the grunts/brutes/jackals are 7), Flood = just 4, Rogue =
    // nobody. The neutral "guilty" team 8 stays allied if the mission had it. The
    // first change snapshots the mission's own allegiances of team 1; Normal puts
    // them back in their original order. Checked every tick, as a script or a
    // reverted checkpoint can bring the old table back.
    int g_savedOthers[HALO3_ALLEG_MAX_RECORDS];  // the other team of each saved record of team 1
    int g_savedCount = 0;
    bool g_teamModified = false;

    // Player-team picker: 0 normal, 1 Covenant, 2 Rogue, 3 Flood.
    void SetPlayerTeam(void** state, const Players& p, int choice) {
        if (choice <= 0 && !g_teamModified) return;
        char* g = Allegiance(state);
        if (!g || !Campaign(state)) return;
        int me = PlayerTeam(p);

        int current[HALO3_ALLEG_MAX_RECORDS];
        int n = 0;
        for (int i = 0, count = AllegianceCount(g); i < count; ++i) {
            char* rec = AllegianceRecord(g, i);
            int a = Field<int16_t>(rec, HALO3_ALLEG_REC_A), b = Field<int16_t>(rec, HALO3_ALLEG_REC_B);
            if (a == me) current[n++] = b;
            else if (b == me) current[n++] = a;
        }

        if (!g_teamModified) {  // the first change: remember what the mission has
            memcpy(g_savedOthers, current, sizeof(current));
            g_savedCount = n;
            g_teamModified = true;
        }

        int want[HALO3_ALLEG_MAX_RECORDS];
        int w = 0;
        auto wants = [&](int team) {
            for (int k = 0; k < w; ++k)
                if (want[k] == team) return;
            if (w < HALO3_ALLEG_MAX_RECORDS && team != me) want[w++] = team;
        };
        if (choice <= 0) {
            for (int i = 0; i < g_savedCount; ++i) wants(g_savedOthers[i]);
        } else {
            if (choice == 1) {
                wants(kTeamCovenant);
                wants(kTeamProphet);
                wants(kTeamHeretic);
            } else if (choice == 3) {
                wants(kTeamFlood);
            }  // 2 = rogue: nobody
            for (int i = 0; i < g_savedCount; ++i)
                if (g_savedOthers[i] == kTeamGuilty) wants(kTeamGuilty);
        }

        for (int i = 0; i < n; ++i) {
            bool keep = false;
            for (int k = 0; k < w; ++k) keep |= want[k] == current[i];
            if (keep) continue;
            Engine<AllegianceRemoveFn>(OFFSET_HALO3_AI_ALLEGIANCE_REMOVE)((int16_t)me, (int16_t)current[i]);
            LOG("Halo 3: your team: allegiance %d-%d removed", me, current[i]);
        }
        for (int k = 0; k < w; ++k) {
            bool have = false;
            for (int i = 0; i < n; ++i) have |= current[i] == want[k];
            if (have) continue;
            Engine<AllegianceFn>(OFFSET_HALO3_AI_ALLEGIANCE)((int16_t)me, (int16_t)want[k]);
            LOG("Halo 3: your team: allegiance %d-%d added", me, want[k]);
        }
        if (choice <= 0) {
            g_teamModified = false;
            g_savedCount = 0;
        }
    }

    // --- Allies never turn against you ---------------------------------------------------
    // Hits add 1 and kills 3 to a record's score (0x199E48, decaying), and at the
    // threshold the allegiance is broken. While on, the threshold of the player
    // team's records is -1 (never), the score 0, and a broken one is put back with
    // the record update (not broken). Thresholds are restored when it's switched
    // off. Also forced on while "Your team" is changed, so an allegiance it made
    // can't break.
    struct SavedThreshold {
        int16_t a, b, threshold;
    };
    SavedThreshold g_savedThresholds[HALO3_ALLEG_MAX_RECORDS];
    int g_savedThresholdCount = 0;

    void NoBetrayal(void** state, const Players& p, bool on) {
        if (!on && g_savedThresholdCount == 0) return;
        char* g = Allegiance(state);
        if (!g || !Campaign(state)) return;
        int me = PlayerTeam(p);
        for (int i = 0, count = AllegianceCount(g); i < count; ++i) {
            char* rec = AllegianceRecord(g, i);
            int16_t a = Field<int16_t>(rec, HALO3_ALLEG_REC_A), b = Field<int16_t>(rec, HALO3_ALLEG_REC_B);
            if (a != me && b != me) continue;
            int16_t& threshold = Field<int16_t>(rec, HALO3_ALLEG_REC_THRESHOLD);
            int saved = -1;
            for (int k = 0; k < g_savedThresholdCount; ++k)
                if ((g_savedThresholds[k].a == a && g_savedThresholds[k].b == b) ||
                    (g_savedThresholds[k].a == b && g_savedThresholds[k].b == a))
                    saved = k;
            if (on) {
                if (threshold != -1 && saved < 0 && g_savedThresholdCount < HALO3_ALLEG_MAX_RECORDS)
                    g_savedThresholds[g_savedThresholdCount++] = {a, b, threshold};
                threshold = -1;
                Field<int16_t>(rec, HALO3_ALLEG_REC_SCORE) = 0;
                if (Field<uint8_t>(rec, HALO3_ALLEG_REC_BROKEN)) {
                    Engine<RecordUpdateFn>(OFFSET_HALO3_ALLEGIANCE_RECORD_UPDATE)(rec, false, false, false);
                    LOG("Halo 3: allegiance %d-%d put back (betrayal)", a, b);
                }
            } else if (saved >= 0 && threshold == -1) {
                threshold = g_savedThresholds[saved].threshold;
            }
        }
        if (!on) g_savedThresholdCount = 0;
    }
}

void Halo3Teams::Load(char* module) { g_module = module; }

void Halo3Teams::Unload() {
    g_module = nullptr;
    Reset();
}

void Halo3Teams::Reset() {
    g_teamsSeen.clear();
    g_savedCount = 0;
    g_teamModified = false;
    g_savedThresholdCount = 0;
}

void Halo3Teams::Tick(void** state, const Games::Settings& s, const Players& p, Blam::DataArray* objects) {
    if (!g_module || !state || !objects) return;
    if (!g_haveSerials) {  // don't run a button press from before this session
        g_nukeSerial = s.nukeSerial;
        g_despawnSerial = s.despawnSerial;
        g_teamsSerial = s.randomTeamsSerial;
        g_haveSerials = true;
    }

    if (s.despawnSerial != g_despawnSerial) {
        g_despawnSerial = s.despawnSerial;
        Guard("Halo 3 despawn custom AI", [&] { DespawnCustomAi(state, objects); });
    }
    if (s.nukeSerial != g_nukeSerial) {
        g_nukeSerial = s.nukeSerial;
        Guard("Halo 3 nuke", [&] { Nuke(state, p, objects, s.nukeSpareAllies); });
    }
    bool button = s.randomTeamsSerial != g_teamsSerial;
    g_teamsSerial = s.randomTeamsSerial;
    if (button) Guard("Halo 3 random AI teams", [&] { RandomTeams(state, p, objects, s, true); });
    else if (s.randomTeamsAuto) Guard("Halo 3 random AI teams auto", [&] { RandomTeams(state, p, objects, s, false); });
    Guard("Halo 3 your team", [&] { SetPlayerTeam(state, p, s.playerTeam); });
    Guard("Halo 3 no betrayal", [&] { NoBetrayal(state, p, s.noBetrayal || g_teamModified); });
}
