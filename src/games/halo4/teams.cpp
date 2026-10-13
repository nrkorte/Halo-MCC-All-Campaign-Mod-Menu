// Halo 4 teams and killing: Nuke, Despawn all custom AI, Random AI teams, Your
// team and "Allies never turn against you". Ported from Reach's
// (reach/main_thread.cpp: Nuke, DespawnCustomAi, RandomTeams, SetPlayerTeam;
// reach/cheats.cpp: NoBetrayal) in the shape of halo3/teams.cpp; the offsets
// are in teams_offsets.h. Runs on the game thread, once per tick, inside SEH.

#include <windows.h>

#include <cstdio>
#include <string>
#include <unordered_set>

#include "core/log.h"
#include "internal.h"
#include "tags.h"
#include "teams_offsets.h"

using namespace Halo4Cheats;

namespace {
    constexpr float kMetresPerUnit = 3.048f;  // a world unit is 10 feet

    // Random AI teams: humans are one faction, then the Covenant, the Prometheans
    // and the teams the campaign doesn't use (brute, mule, spare). Every team
    // lists only itself in the allegiance table (live), so each one is an enemy
    // of every other, and with more factions it's a brawl.
    const int kFactionTeams[] = {HALO4_TEAM_HUMAN, HALO4_TEAM_COVENANT, HALO4_TEAM_FORERUNNER,
                                 HALO4_TEAM_BRUTE, HALO4_TEAM_MULE,     HALO4_TEAM_SPARE};

    // Your team picker: 0 normal, 1 Covenant, 2 rogue (spare: nobody is its friend).
    const int kPlayerTeams[] = {HALO4_TEAM_PLAYER, HALO4_TEAM_COVENANT, HALO4_TEAM_SPARE};

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    using UnitKillFn = void(__fastcall*)(uint32_t unit, uint32_t, bool, int, int);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor, bool deferred);
    using RecordUpdateFn = void(__fastcall*)(char* record, bool broken, bool remove, bool flag);

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

    // Player 1's team (the unit's), the player team if there is no player right now.
    int PlayerTeam(const Players& p) {
        int t = p.count > 0 ? Field<int8_t>(p.units[0], HALO4_UNIT_TEAM) : HALO4_TEAM_PLAYER;
        return t < 0 || t >= HALO4_TEAM_COUNT ? HALO4_TEAM_PLAYER : t;
    }

    bool InRange(const Players& p, char* object, float metres) {
        if (metres <= 0.0f) return true;
        if (p.count == 0) return false;
        const float* a = &Field<float>(p.units[0], HALO4_OBJECT_POSITION);
        const float* b = &Field<float>(object, HALO4_OBJECT_POSITION);
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        float range = metres / kMetresPerUnit;
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

    bool Dead(char* unit) {
        float* health = Health(unit);
        return !health || *health <= 0.0f;
    }

    std::string ShortName(uint32_t tag) {
        std::string name = Halo4Tags::FullName(tag);
        size_t slash = name.find_last_of('\\');
        return slash != std::string::npos ? name.substr(slash + 1) : name;
    }

    void Say(const char* format, int a, int b = 0) {
        char text[96];
        sprintf_s(text, format, a, b);
        Halo4Internal::Toast(text);
    }

    // --- Nuke -------------------------------------------------------------------
    // Every AI unit that isn't a player (with `spare`, nor friends of player 1's
    // team) loses its cannot-take-damage / cannot-die flags and shield and is
    // killed with unit_kill (the damage multiplier's call).
    void Nuke(void** state, const Players& p, const Array& objects, bool spare) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        int mine = PlayerTeam(p);
        int killed = 0, spared = 0;
        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            if (*(uint16_t*)actor == 0) continue;
            uint32_t datum = Field<uint32_t>(actor, HALO4_ACTOR_UNIT);
            char* unit = ObjectFromDatum(objects, datum);
            if (!unit || p.Contains(unit) || Dead(unit)) continue;
            if (spare && Halo4Internal::TeamsFriends(state, mine, Field<int8_t>(unit, HALO4_UNIT_TEAM))) {
                ++spared;
                continue;
            }
            Field<uint32_t>(unit, HALO4_OBJECT_DAMAGE_FLAGS) &=
                ~(HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO4_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE);
            if (float* shield = Shield(unit)) *shield = 0.0f;
            if (float* health = Health(unit)) *health = 0.0001f;
            Engine<UnitKillFn>(OFFSET_HALO4_UNIT_KILL)(datum, 0, true, 2, 0);
            ++killed;
        }
        LOG("Halo 4: nuke: %d AI killed, %d allies spared", killed, spared);
        Say(killed ? "Nuked %d AI" : "No AI to nuke", killed);
    }

    // --- Despawn all custom AI ----------------------------------------------------
    // Every AI the mod made (all go through Halo4Internal::Follow), alive or a
    // body: actor erase if the unit still has its actor, else object_delete.
    void DespawnCustomAi(void** state, const Array& objects) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        uint32_t units[512];
        int count = Halo4Internal::FollowedUnits(units, (int)_countof(units));
        int removed = 0;
        for (int i = 0; i < count; ++i) {
            char* unit = ObjectFromDatum(objects, units[i]);
            if (!unit) continue;
            uint32_t actorDatum = Field<uint32_t>(unit, HALO4_UNIT_ACTOR);
            char* actor = actors.Valid() ? actors.Get(actorDatum) : nullptr;
            if (actor && Field<uint32_t>(actor, HALO4_ACTOR_UNIT) == units[i])
                Engine<ActorEraseFn>(OFFSET_HALO4_ACTOR_ERASE)(actorDatum, false);
            else
                Halo4Internal::DeleteObject(units[i]);
            ++removed;
        }
        Halo4Internal::ForgetFollowed();
        LOG("Halo 4: despawn all custom AI: %d removed", removed);
        Say(removed ? "Despawned %d custom AI" : "No custom AI to despawn", removed);
    }

    // --- Random AI teams ------------------------------------------------------------
    // Each AI actor in range goes on one of the first N of kFactionTeams. `all` =
    // the button (every AI again); otherwise each actor datum once per map.
    // Invisible scripted pilots (the "null" characters) keep their team, and
    // Spartans unless asked.
    std::unordered_set<uint32_t> g_teamsSeen;

    void RandomTeams(void** state, const Players& p, const Array& objects, const Games::Settings& s, bool all) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        int factions = (int)(s.randomTeamsFactions + 0.5f);
        int most = (int)_countof(kFactionTeams);
        factions = factions < 2 ? 2 : (factions > most ? most : factions);
        if (g_teamsSeen.size() > 20000) g_teamsSeen.clear();
        int changed = 0;
        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_teamsSeen.count(datum)) continue;
            char* unit = ObjectFromDatum(objects, Field<uint32_t>(actor, HALO4_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Dead(unit)) continue;
            if (!InRange(p, unit, s.randomTeamsRange)) continue;
            g_teamsSeen.insert(datum);
            std::string name = ShortName(Field<uint32_t>(actor, HALO4_TEAMS_ACTOR_CHARACTER));
            if (name.rfind("null", 0) == 0) continue;
            if (!s.randomTeamsSpartans && name.find("spartan") != std::string::npos) continue;
            int team = kFactionTeams[(int)(Random01() * factions) % factions];
            Halo4Internal::SetActorTeam(datum, (int16_t)team);
            ++changed;
        }
        if (all || changed)
            LOG("Halo 4: random AI teams (%s): %d AI over %d teams", all ? "button" : "auto", changed, factions);
        if (all) Say(changed ? "Shuffled %d AI over %d teams" : "No AI in range", changed, factions);
    }

    // --- Your team --------------------------------------------------------------------
    // The player record's team, which the unit update copies to the player's unit
    // every tick (teams_offsets.h). Switching back to normal puts the player team
    // back once; a new map starts on it anyway.
    bool g_teamForced = false;

    void SetPlayerTeam(void** state, int choice) {
        if (choice <= 0 && !g_teamForced) return;
        int count = (int)_countof(kPlayerTeams);
        int team = kPlayerTeams[choice > 0 && choice < count ? choice : 0];
        Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
        if (!players.Valid()) return;
        int limit = players.HighWater() < 16 ? players.HighWater() : 16;
        for (int i = 0; i < limit; ++i) {
            char* player = players.Entry(i);
            if (*(uint16_t*)player == 0) continue;
            int8_t& current = Field<int8_t>(player, HALO4_TEAMS_PLAYER_TEAM);
            if (current == team) continue;
            LOG("Halo 4: player %d team %d -> %d", i, current, team);
            current = (int8_t)team;
        }
        g_teamForced = choice > 0;
    }

    // --- Allies never turn against you ---------------------------------------------------
    // Hits add to a record's score and at the threshold the allegiance breaks
    // (0x207DF0). While on, the threshold of every record that involves the
    // player's team is -1 (never), the score 0, and a broken one (a script's
    // ai_allegiance_break) is put back with the record update (not broken).
    // Thresholds are restored when it's switched off.
    struct SavedThreshold {
        int8_t a, b;
        int16_t threshold;
    };
    SavedThreshold g_savedThresholds[HALO4_ALLEG_MAX_RECORDS];
    int g_savedThresholdCount = 0;

    void NoBetrayal(void** state, const Players& p, bool on) {
        if (!on && g_savedThresholdCount == 0) return;
        char* table = (char*)state[HALO4_TLS_SLOT_ALLEGIANCE];
        if (!table) return;
        int me = PlayerTeam(p);
        int count = Field<uint16_t>(table, HALO4_ALLEG_COUNT);
        if (count > HALO4_ALLEG_MAX_RECORDS) return;
        for (int i = 0; i < count; ++i) {
            char* rec = table + HALO4_ALLEG_RECORDS + i * HALO4_ALLEG_RECORD_SIZE;
            int8_t a = Field<int8_t>(rec, HALO4_ALLEG_REC_A), b = Field<int8_t>(rec, HALO4_ALLEG_REC_B);
            if (a != me && b != me) continue;
            int16_t& threshold = Field<int16_t>(rec, HALO4_ALLEG_REC_THRESHOLD);
            int saved = -1;
            for (int k = 0; k < g_savedThresholdCount; ++k)
                if ((g_savedThresholds[k].a == a && g_savedThresholds[k].b == b) ||
                    (g_savedThresholds[k].a == b && g_savedThresholds[k].b == a))
                    saved = k;
            if (on) {
                if (threshold != -1 && saved < 0 && g_savedThresholdCount < HALO4_ALLEG_MAX_RECORDS)
                    g_savedThresholds[g_savedThresholdCount++] = {a, b, threshold};
                threshold = -1;
                Field<uint16_t>(rec, HALO4_ALLEG_REC_SCORE) = 0;
                if (Field<uint8_t>(rec, HALO4_ALLEG_REC_BROKEN)) {
                    Engine<RecordUpdateFn>(OFFSET_HALO4_ALLEGIANCE_RECORD_UPDATE)(rec, false, false, false);
                    LOG("Halo 4: allegiance %d-%d put back (betrayal)", a, b);
                }
            } else if (saved >= 0 && threshold == -1) {
                threshold = g_savedThresholds[saved].threshold;
            }
        }
        if (!on) g_savedThresholdCount = 0;
    }
}

void Halo4Teams::Load(char* module) { g_module = module; }

void Halo4Teams::Unload() {
    g_module = nullptr;
    Reset();
}

void Halo4Teams::Reset() {
    g_teamsSeen.clear();
    g_teamForced = false;
    g_savedThresholdCount = 0;
}

void Halo4Teams::Tick(void** state, const Games::Settings& s, const Players& p, const Array& objects) {
    if (!g_module || !state || !objects.Valid()) return;
    if (!g_haveSerials) {  // don't run a button press from before this session
        g_nukeSerial = s.nukeSerial;
        g_despawnSerial = s.despawnSerial;
        g_teamsSerial = s.randomTeamsSerial;
        g_haveSerials = true;
    }

    if (s.despawnSerial != g_despawnSerial) {
        g_despawnSerial = s.despawnSerial;
        Guard("Halo 4 despawn custom AI", [&] { DespawnCustomAi(state, objects); });
    }
    if (s.nukeSerial != g_nukeSerial) {
        g_nukeSerial = s.nukeSerial;
        Guard("Halo 4 nuke", [&] { Nuke(state, p, objects, s.nukeSpareAllies); });
    }
    bool button = s.randomTeamsSerial != g_teamsSerial;
    g_teamsSerial = s.randomTeamsSerial;
    if (button) Guard("Halo 4 random AI teams", [&] { RandomTeams(state, p, objects, s, true); });
    else if (s.randomTeamsAuto) Guard("Halo 4 random AI teams auto", [&] { RandomTeams(state, p, objects, s, false); });
    Guard("Halo 4 your team", [&] { SetPlayerTeam(state, s.playerTeam); });
    Guard("Halo 4 no betrayal", [&] { NoBetrayal(state, p, s.noBetrayal); });
}
