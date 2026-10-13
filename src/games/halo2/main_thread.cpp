// Halo 2 cheats that call engine functions. Runs once per game tick on the
// game thread, before the tick (see halo2.cpp).

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include "cheat_util.h"
#include "cheats.h"
#include "core/log.h"
#include "games/fun.h"
#include "internal.h"
#include "tags.h"

using namespace Halo2Cheats;

namespace {
    constexpr float kNoclipSpeed = 6.0f;  // world units per second
    constexpr float kNoclipFastScale = 4.0f;
    constexpr int kMaxTracked = 2048;     // object table slots we track for damage
    constexpr float kPi = 3.14159265f;
    constexpr float kMaxPitch = 1.49f;  // just inside straight up/down
    constexpr float kAimMinCone = 3.0f;   // degrees from the crosshair, at 1% strength
    constexpr float kAimMaxCone = 55.0f;  // at 100%: about the whole screen
    constexpr float kAimRange = 200.0f;   // world units
    constexpr float kEyeHeight = 0.6f;   // above the unit's position (its feet)
    constexpr float kHeadHeight = 0.55f; // Halo 2 has no head-position call used here

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using UnitAddWeaponFn = bool(__fastcall*)(uint32_t unit, uint32_t weapon, int mode);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up, void*);
    using CollisionTestFn = bool(__fastcall*)(uint32_t flags, const float* point, const float* vector,
                                              uint32_t ignore, uint32_t ignore2, void* result);
    using ActorCreateFn = uint32_t(__fastcall*)(uint32_t character, uint32_t squad, void* start, bool upgrade);
    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);
    using ActorSetTeamFn = void(__fastcall*)(uint32_t actor, int16_t team);
    using ActorDeleteFn = void(__fastcall*)(uint32_t actor);
    using UnitKillFn = void(__fastcall*)(uint32_t unit);
    using AllegianceFn = void(__fastcall*)(int16_t a, int16_t b);
    using AllegianceRemoveFn = bool(__fastcall*)(int16_t a, int16_t b);
    using AllegianceBreakFn = void(__fastcall*)(char* record, bool broken, bool clearAllied);

    constexpr float kSpawnRange = 300.0f;  // world units the spawn ray reaches
    constexpr float kSpawnBackOff = 0.3f;  // back along the ray from the hit, so it isn't inside the surface
    constexpr float kSpawnLift = 0.25f;    // and up, so it doesn't clip into the ground
    constexpr float kVehicleLift = 0.5f;   // vehicles and scenery: above the higher of the hit and the backed-off point

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    // Noclip
    bool g_noclipActive = false;
    uint32_t g_noclipUnit = 0xFFFFFFFF;
    float g_anchor[3] = {};
    LARGE_INTEGER g_lastTick{};

    // Damage multiplier: last seen vitality per object slot.
    struct Tracked {
        uint32_t datum;
        float health;
        float shield;
    };
    Tracked g_tracked[kMaxTracked];

    uint32_t g_giveSerial = 0;
    uint32_t g_saveSerial = 0;
    uint32_t g_loadSerial = 0;
    uint32_t g_spawnSerial = 0;
    uint32_t g_nukeSerial = 0;
    uint32_t g_grenadeSerial = 0;
    uint32_t g_despawnSerial = 0;
    uint32_t g_teamsSerial = 0;

    // AI we spawned, followed after death so "Enable respawn" decides
    // whether they come back (see FollowSpawnedAi).
    struct SpawnedAi {
        uint32_t unit = 0xFFFFFFFF;
        uint32_t character = 0xFFFFFFFF;  // 'char' tag, to respawn it
        int team = 0;                     // spawnTeam it was made with
        ULONGLONG deadAt = 0;             // when it was first seen dead, 0 = alive
    };
    constexpr int kMaxSpawnedAi = 512;  // big squads and rain; Despawn walks all of them
    constexpr ULONGLONG kRespawnMs = 5000;  // dead this long: respawned, or left as a body
    SpawnedAi g_spawnedAi[kMaxSpawnedAi];

    // Spawning results for the menu (game thread writes, render thread reads).
    std::mutex g_spawnLock;
    uint32_t g_spawnDone = 0;
    char g_spawnMessage[96] = "";

    void SpawnResult(const char* text) {
        std::lock_guard<std::mutex> lock(g_spawnLock);
        sprintf_s(g_spawnMessage, "%s", text);
        ++g_spawnDone;
        LOG("Halo 2: spawn: %s", text);
    }

    float TickSeconds(LARGE_INTEGER& last) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart : 0.0f;
        last = now;
        return dt > 0.1f ? 0.1f : dt;
    }

    // Holds player 1 in place, and flies them where they aim while the noclip
    // button is held. Sets only the position (as Reach's noclip does): a
    // player's teleport also resets their view.
    void Noclip(const Players& p, bool on, const Games::FrameInput& in, float dt) {
        bool seated = p.count > 0 && Field<uint32_t>(p.units[0], HALO2_OBJECT_PARENT) != 0xFFFFFFFF;
        bool sameUnit = p.count > 0 && p.datums[0] == g_noclipUnit && !seated;
        if (g_noclipActive && (!on || !sameUnit)) {
            g_noclipActive = false;
            LOG("Halo 2: noclip off");
        }
        if (!on || p.count == 0 || seated) return;

        char* unit = p.units[0];
        float* pos = &Field<float>(unit, HALO2_OBJECT_POSITION);
        if (!g_noclipActive) {
            memcpy(g_anchor, pos, sizeof(g_anchor));
            g_noclipUnit = p.datums[0];
            g_noclipActive = true;
            LOG("Halo 2: noclip on at %.2f %.2f %.2f", pos[0], pos[1], pos[2]);
        }

        const float* aim = &Field<float>(unit, HALO2_UNIT_AIM_VECTOR);
        if (in.noclipForward) {
            float step = kNoclipSpeed * (in.fast ? kNoclipFastScale : 1.0f) * dt;
            for (int k = 0; k < 3; ++k) g_anchor[k] += aim[k] * step;
        }

        Engine<SetPositionFn>(OFFSET_HALO2_OBJECT_SET_POSITION)(p.datums[0], g_anchor, nullptr, nullptr, nullptr);
        memset(&Field<float>(unit, HALO2_OBJECT_VELOCITY), 0, 3 * sizeof(float));

        static ULONGLONG s_lastLog = 0;  // to check the aim vector and position in the log
        if (GetTickCount64() - s_lastLog > 2000) {
            s_lastLog = GetTickCount64();
            LOG("Halo 2: noclip aim %.2f %.2f %.2f, anchor %.1f %.1f %.1f, unit at %.1f %.1f %.1f", aim[0], aim[1],
                aim[2], g_anchor[0], g_anchor[1], g_anchor[2], pos[0], pos[1], pos[2]);
        }
    }

    // Scales the health and shield enemies lost since last tick. Fatal
    // scaled damage kills them the way unit_kill does (a damage flag).
    void Damage(const Players& p, const Objects& objects, float m) {
        if (m == 1.0f) {
            memset(g_tracked, 0, sizeof(g_tracked));
            return;
        }
        int count = objects.table.HighWater();
        for (int i = 0; i < count && i < kMaxTracked; ++i) {
            char* entry = objects.table.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = -1;
            char* object = salt ? objects.FromEntry(entry, &type) : nullptr;
            Tracked& t = g_tracked[i];
            if (!object || !IsKillableEnemy(object, type, p)) {
                t.datum = 0;
                continue;
            }

            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            auto& health = Field<float>(object, HALO2_OBJECT_HEALTH);
            auto& shield = Field<float>(object, HALO2_OBJECT_SHIELD);
            if (t.datum != datum) {
                t = {datum, health, shield};
                continue;
            }
            if (health <= 0.0f) {
                t.datum = 0;
                continue;
            }
            if (shield < t.shield) {
                float s = t.shield - (t.shield - shield) * m;
                shield = s > 0.0f ? s : 0.0f;
            }
            if (health < t.health) {
                float h = t.health - (t.health - health) * m;
                if (h <= 0.0f) {
                    Field<uint16_t>(object, HALO2_OBJECT_DAMAGE_FLAGS) |= HALO2_DAMAGE_FLAG_KILL;
                    t.datum = 0;
                    continue;
                }
                health = h;
            }
            t.health = health;
            t.shield = shield;
        }
    }

    // A give waiting for the game to free the held weapon's slot (deletes
    // take effect a tick or so later).
    struct PendingGive {
        bool active = false;
        uint32_t weapon = 0xFFFFFFFF;
        uint32_t held = 0xFFFFFFFF;
        int frames = 0;
    };
    PendingGive g_give;

    int SlotOf(char* unit, uint32_t weapon) {
        for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w)
            if (Field<uint32_t>(unit, HALO2_UNIT_WEAPONS + w * 4) == weapon) return w;
        return -1;
    }

    void AddAndSelect(char* unit, uint32_t unitDatum, uint32_t weapon) {
        bool added = Engine<UnitAddWeaponFn>(OFFSET_HALO2_UNIT_ADD_WEAPON)(unitDatum, weapon, HALO2_ADD_WEAPON_MODE);
        int slot = added ? SlotOf(unit, weapon) : -1;
        if (slot >= 0) Field<int8_t>(unit, HALO2_UNIT_DESIRED_WEAPON) = (int8_t)slot;
        else Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(weapon);
        LOG("Halo 2: give weapon: %s (object %08X, slot %d)", slot >= 0 ? "added" : "add FAILED", weapon, slot);
    }

    // Replaces player 1's held weapon with a new one of the chosen type (or
    // fills a free slot).
    void GiveWeapon(const Players& p, const Objects& objects, uint32_t weaponTag) {
        if (p.count == 0) { LOG("Halo 2: give weapon: no player unit"); return; }
        if (!Halo2Tags::Get(weaponTag)) { LOG("Halo 2: give weapon %08X: tag not loaded", weaponTag); return; }
        char* unit = p.units[0];
        uint32_t unitDatum = p.datums[0];

        if (g_give.active) {
            Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
        }

        static uint8_t placement[HALO2_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO2_OBJECT_PLACEMENT_NEW)(placement, weaponTag, unitDatum, 0);
        uint32_t weapon = Engine<ObjectNewFn>(OFFSET_HALO2_OBJECT_NEW)(placement);
        if (weapon == 0xFFFFFFFF) {
            LOG("Halo 2: give weapon %08X: the game couldn't create it here", weaponTag);
            return;
        }

        bool freeSlot = false;
        for (int w = 0; w < HALO2_PLAYER_WEAPON_SLOTS; ++w)
            freeSlot |= Field<uint32_t>(unit, HALO2_UNIT_WEAPONS + w * 4) == 0xFFFFFFFF;

        uint32_t held = 0xFFFFFFFF;
        if (freeSlot || !HeldWeapon(unit, objects, &held)) {
            AddAndSelect(unit, unitDatum, weapon);
            return;
        }
        Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(held);
        g_give = {true, weapon, held, 0};
        LOG("Halo 2: give weapon %08X: removing held %08X, adding once its slot frees", weaponTag, held);
    }

    void ContinueGive(const Players& p) {
        if (!g_give.active) return;
        if (p.count == 0) {
            Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
            return;
        }
        if (SlotOf(p.units[0], g_give.held) < 0) {
            AddAndSelect(p.units[0], p.datums[0], g_give.weapon);
            g_give = {};
        } else if (++g_give.frames > 120) {
            Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(g_give.weapon);
            LOG("Halo 2: give weapon: held weapon never left the inventory; gave up");
            g_give = {};
        }
    }

    // Rapid fire: while the player fires, their triggers always count as long
    // past the last shot, so every tick can fire. Only while firing, so the
    // firing kick stops when they let go (patching out the reset kept it
    // shaking forever).
    void RapidFire(const Players& p, const Objects& objects, bool own, bool vehicles, bool firing) {
        if (!firing) return;
        char* units[kMaxLocalPlayers * 2];
        int count = Shooters(p, objects, own, vehicles, units, _countof(units));
        for (int i = 0; i < count; ++i) {
            for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(units[i], objects, w);
                if (!weapon) continue;
                Field<uint8_t>(weapon, HALO2_WEAPON_TRIGGER) = HALO2_TRIGGER_READY;
                Field<uint16_t>(weapon, HALO2_WEAPON_FIRE_DELAY) = 0;
            }
        }
    }

    // Aimbot strength: picks the enemy nearest player 1's crosshair and
    // returns how far to turn (see the Halo 3 version).
    bool AimTurn(const Players& p, const Objects& objects, float strength, float yaw, float pitch, float& dyaw,
                 float& dpitch) {
        float s = strength / 100.0f;
        float forward[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        const float* feet = &Field<float>(p.units[0], HALO2_OBJECT_POSITION);
        float eye[3] = {feet[0], feet[1], feet[2] + kEyeHeight};

        float minCos = cosf((kAimMinCone + (kAimMaxCone - kAimMinCone) * s) * kPi / 180.0f);
        float best[3] = {};
        float bestCos = minCos;
        bool found = false;
        int count = objects.table.HighWater();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsKillableEnemy(object, type, p)) continue;
            if (Field<float>(object, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            const float* pos = &Field<float>(object, HALO2_OBJECT_POSITION);
            float d[3] = {pos[0] - eye[0], pos[1] - eye[1], pos[2] + kHeadHeight - eye[2]};
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 0.1f || len > kAimRange) continue;
            float cosine = (d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2]) / len;
            if (cosine <= bestCos) continue;
            bestCos = cosine;
            memcpy(best, d, sizeof(best));
            found = true;
        }
        if (!found) return false;
        float targetYaw = atan2f(best[1], best[0]);
        float targetPitch = atan2f(best[2], sqrtf(best[0] * best[0] + best[1] * best[1]));
        float turn = s * s;
        dyaw = remainderf(targetYaw - yaw, 2.0f * kPi) * turn;
        dpitch = (targetPitch - pitch) * turn;
        return true;
    }

    // Aimbot strength: while firing, turns player 1's view toward the enemy
    // nearest the crosshair, in the look angles the camera follows.
    void AimPull(const Players& p, const Objects& objects, float strength) {
        if (strength <= 0.0f || p.count == 0) return;
        if (HoldingMeleeOnly(p.units[0], objects)) return;  // the sword keeps its own lunge
        char* control = *(char**)(g_module + OFFSET_HALO2_PLAYER_CONTROL);
        if (!control) return;
        char* entry = control + HALO2_PLAYER_CONTROL_ENTRIES;  // player 1
        float& yaw = Field<float>(entry, HALO2_PLAYER_CONTROL_YAW);
        float& pitch = Field<float>(entry, HALO2_PLAYER_CONTROL_PITCH);
        float dyaw, dpitch;
        if (!AimTurn(p, objects, strength, yaw, pitch, dyaw, dpitch)) return;
        yaw = fmodf(yaw + dyaw, 2.0f * kPi);  // the game keeps yaw in 0..2pi
        if (yaw < 0.0f) yaw += 2.0f * kPi;
        pitch += dpitch;
        if (pitch > kMaxPitch) pitch = kMaxPitch;
        if (pitch < -kMaxPitch) pitch = -kMaxPitch;
    }

    std::string ShortName(uint32_t tag) {
        std::string name = Halo2Tags::FullName(tag);
        name = name.substr(name.find_last_of('\\') + 1);
        return name.empty() ? "selection" : name;
    }

    // What a spawn selection means: a tag, or a vehicle variant from the
    // Vehicles list.
    VehicleChoice ChoiceFor(uint32_t selection) {
        VehicleChoice c;
        if (!FindVehicleChoice(selection, c)) c.vehicle = selection;
        return c;
    }

    std::string SelectionName(uint32_t selection) {
        VehicleChoice c = ChoiceFor(selection);
        return c.name.empty() ? ShortName(c.vehicle) : c.name;
    }

    // Halo 2 characters carry no loadout: the mission's squads give their
    // AI weapons from the scenario's weapon palette (a starting location's
    // own pick, else the squad's). A spawned character gets one of the
    // loadouts the mission gives that character, chosen at random; if no
    // squad uses it, its parent's (elite_major -> elite). Writes palette
    // indices (-1 = none) into the starting location record.
    void PickLoadout(uint32_t character, char* start) {
        char* scnr = Halo2Tags::First(HALO2_TAG_SCNR);
        if (!scnr) return;
        int16_t found[256][2];
        int count = 0;
        int squads = Halo2Tags::BlockCount(scnr, HALO2_SCNR_SQUADS);
        int characters = Halo2Tags::BlockCount(scnr, HALO2_SCNR_CHARACTER_PALETTE);
        for (int depth = 0; depth < 8 && count == 0 && character != 0xFFFFFFFF; ++depth) {
            for (int i = 0; i < squads && i < 1024 && count < 256; ++i) {
                char* squad = Halo2Tags::BlockElement(scnr, HALO2_SCNR_SQUADS, HALO2_SQUAD_SIZE, i);
                if (!squad) continue;
                int starts = Halo2Tags::BlockCount(squad, HALO2_SQUAD_STARTS);
                for (int k = 0; k < starts && k < 64 && count < 256; ++k) {
                    char* st = Halo2Tags::BlockElement(squad, HALO2_SQUAD_STARTS, HALO2_START_SIZE, k);
                    if (!st) continue;
                    int16_t index = Field<int16_t>(st, HALO2_START_CHARACTER_INDEX);
                    if (index < 0) index = Field<int16_t>(squad, HALO2_SQUAD_CHARACTER);
                    if (index < 0 || index >= characters) continue;
                    char* entry = Halo2Tags::BlockElement(scnr, HALO2_SCNR_CHARACTER_PALETTE,
                                                          HALO2_CHARACTER_PALETTE_SIZE, index);
                    if (!entry || Field<uint32_t>(entry, HALO2_PALETTE_ENTRY_DATUM) != character) continue;
                    int16_t weapon = Field<int16_t>(st, HALO2_START_WEAPON);
                    int16_t secondary = Field<int16_t>(st, HALO2_START_SECONDARY_WEAPON);
                    if (weapon < 0) weapon = Field<int16_t>(squad, HALO2_SQUAD_WEAPON);
                    if (secondary < 0) secondary = Field<int16_t>(squad, HALO2_SQUAD_SECONDARY);
                    if (weapon < 0) continue;  // e.g. vehicle crews
                    found[count][0] = weapon;
                    found[count][1] = secondary;
                    ++count;
                }
            }
            if (count == 0) {
                char* tag = Halo2Tags::Get(character);
                character = tag ? Field<uint32_t>(tag, HALO2_CHAR_PARENT) : 0xFFFFFFFF;
            }
        }
        if (count == 0) {
            LOG("Halo 2: no squad in this mission gives a weapon to this character; spawning it unarmed");
            return;
        }
        int pick = (int)(GetTickCount64() % (ULONGLONG)count);
        Field<int16_t>(start, HALO2_START_WEAPON) = found[pick][0];
        Field<int16_t>(start, HALO2_START_SECONDARY_WEAPON) = found[pick][1];
    }

    // --- Teams (the AI team picker, Random AI teams, Your team) ----------------

    constexpr float kMetresPerUnit = 3.048f;  // a world unit is 10 feet

    uint32_t g_rng = 0x2545F491u;
    float Random01() {
        if (g_rng == 0x2545F491u) g_rng ^= (uint32_t)GetTickCount64();
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (float)(g_rng & 0xFFFFFF) / 16777216.0f;
    }

    char* Allegiance() { return *(char**)(g_module + OFFSET_HALO2_ALLEGIANCE); }

    int AllegianceCount(char* g) {
        int n = Field<uint16_t>(g, HALO2_ALLEG_COUNT);
        return n > HALO2_ALLEG_MAX_RECORDS ? 0 : n;
    }
    char* AllegianceRecord(char* g, int i) { return g + HALO2_ALLEG_RECORDS + i * HALO2_ALLEG_RECORD_SIZE; }

    // The game's "not hostile" mask (allegiances plus every team's own bit).
    bool TeamsFriends(int a, int b) {
        if (a < 0 || b < 0 || a > 15 || b > 15) return a == b;
        char* g = Allegiance();
        if (!g) return a == b;
        int bit = a * 16 + b;
        return (Field<uint32_t>(g, HALO2_ALLEG_FRIENDLY_MASK + (bit >> 5) * 4) >> (bit & 31)) & 1;
    }

    int PlayerTeam(const Players& p) {
        int t = p.count > 0 ? Field<int16_t>(p.units[0], HALO2_UNIT_TEAM) : HALO2_TEAM_PLAYER;
        return t < 0 || t > 15 ? HALO2_TEAM_PLAYER : t;
    }

    bool OnArbiterMission(const Players& p) {
        return p.count > 0 &&
               Halo2Tags::FullName(Field<uint32_t>(p.units[0], HALO2_OBJECT_TAG_DATUM)).find("dervish") !=
                   std::string::npos;
    }

    // Every team is its own friend in the mask, so rogues would fight everyone
    // but each other. Clearing the rogue team's own bit makes them hostile to
    // each other too (as CE does for its rogue team; untested here whether the
    // engine also compares team numbers first). Props made later read it.
    void MakeRogueFightItself() {
        char* g = Allegiance();
        if (!g) return;
        int bit = HALO2_TEAM_ROGUE * 16 + HALO2_TEAM_ROGUE;
        Field<uint32_t>(g, HALO2_ALLEG_FRIENDLY_MASK + (bit >> 5) * 4) &= ~(1u << (bit & 31));
    }

    constexpr int16_t kFactionTeams[] = {HALO2_TEAM_HUMAN,    HALO2_TEAM_COVENANT, HALO2_TEAM_FLOOD,
                                         HALO2_TEAM_SENTINEL, HALO2_TEAM_HERETIC,  HALO2_TEAM_ROGUE};

    // The AI team picker (Settings::spawnTeam): 0 = their own (returns 0), 1 = ally
    // (a team that's friends with player 1's team right now: human, or on Arbiter
    // missions the Covenant; follows "Your team"), 2 = the enemy Covenant (Heretics
    // on Arbiter missions; another hostile team if "Your team" made the Covenant
    // your friends), 3 = Flood, 4 = rogue, 5 = a random one each call.
    int16_t ResolveTeam(const Players& p, int choice) {
        int mine = PlayerTeam(p);
        bool arbiter = OnArbiterMission(p);
        switch (choice) {
            case 1: {
                const int order[] = {arbiter ? HALO2_TEAM_COVENANT : HALO2_TEAM_HUMAN,
                                     arbiter ? HALO2_TEAM_HUMAN : HALO2_TEAM_COVENANT,
                                     HALO2_TEAM_FLOOD, HALO2_TEAM_SENTINEL, HALO2_TEAM_HERETIC, HALO2_TEAM_PROPHET};
                for (int t : order)
                    if (t != mine && TeamsFriends(mine, t)) return (int16_t)t;
                return (int16_t)(mine > 0 ? mine : HALO2_TEAM_PLAYER);  // nobody's allied: your own team
            }
            case 2: {
                const int order[] = {arbiter ? HALO2_TEAM_HERETIC : HALO2_TEAM_COVENANT, HALO2_TEAM_COVENANT,
                                     HALO2_TEAM_HUMAN, HALO2_TEAM_HERETIC, HALO2_TEAM_FLOOD};
                for (int t : order)
                    if (!TeamsFriends(mine, t)) return (int16_t)t;
                return HALO2_TEAM_ROGUE;
            }
            case 3: return HALO2_TEAM_FLOOD;
            case 4: return HALO2_TEAM_ROGUE;
            case 5: return kFactionTeams[(int)(Random01() * _countof(kFactionTeams)) % _countof(kFactionTeams)];
            default: return 0;
        }
    }

    void SetTeam(uint32_t actor, int16_t team) {
        if (team == HALO2_TEAM_ROGUE) MakeRogueFightItself();
        Engine<ActorSetTeamFn>(OFFSET_HALO2_ACTOR_SET_TEAM)(actor, team);
    }

    // An AI from a character tag as a squadless actor, built the way the
    // game's own squadless swarm spawn builds its starting location (see
    // offsets.h): the character's unit, its weapons and the actor. Returns
    // the actor datum or -1.
    uint32_t SpawnActor(const Players& p, uint32_t character, const float* position, float yaw, int team) {
        alignas(16) static uint8_t start[HALO2_START_SIZE];
        char* s = (char*)start;
        memset(start, 0, sizeof(start));
        memcpy(start + HALO2_START_POSITION, position, 3 * sizeof(float));
        Field<int16_t>(s, HALO2_START_BSP) = -1;
        Field<float>(s, HALO2_START_YAW) = yaw;
        Field<uint32_t>(s, HALO2_START_FLAGS) = HALO2_START_FLAGS_SQUADLESS;
        Field<int32_t>(s, HALO2_START_CHARACTER) = -1;
        Field<int16_t>(s, HALO2_START_SECONDARY_WEAPON) = -1;
        Field<int32_t>(s, HALO2_START_VEHICLE) = -1;
        Field<int16_t>(s, HALO2_START_GRENADES) = -1;
        Field<int16_t>(s, HALO2_START_SWARM_COUNT) = 1;
        Field<int16_t>(s, HALO2_START_3C) = -1;
        Field<int16_t>(s, HALO2_START_60) = -1;
        PickLoadout(character, s);
        uint32_t actor = Engine<ActorCreateFn>(OFFSET_HALO2_ACTOR_CREATE)(character, 0xFFFFFFFF, start, false);
        if (actor == 0xFFFFFFFF) return actor;
        Engine<ActorSetActiveFn>(OFFSET_HALO2_ACTOR_SET_ACTIVE)(actor, true);
        if (team > 0) {
            int16_t resolved = ResolveTeam(p, team);
            if (resolved > 0) SetTeam(actor, resolved);
        }
        return actor;
    }

    // Starts following an actor just spawned (for "Enable respawn").
    void Follow(const Objects& objects, uint32_t actorDatum, uint32_t character, int team) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        char* actor = actors.header ? actors.Get(actorDatum) : nullptr;
        uint32_t unit = actor ? Field<uint32_t>(actor, HALO2_ACTOR_UNIT) : 0xFFFFFFFF;
        if (!objects.Get(unit)) {
            LOG("Halo 2: spawned actor %08X has no unit; not followed", actorDatum);
            return;
        }
        // A free slot, else the body that's been dead longest.
        SpawnedAi* slot = nullptr;
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF || !objects.Get(ai.unit)) { slot = &ai; break; }
            if (ai.deadAt && (!slot || ai.deadAt < slot->deadAt)) slot = &ai;
        }
        if (slot) *slot = {unit, character, team, 0};
    }

    uint32_t NewObject(uint32_t tag, uint32_t variant, const float* position, float yaw) {
        static uint8_t placement[HALO2_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO2_OBJECT_PLACEMENT_NEW)(placement, tag, 0xFFFFFFFF, 0);
        if (variant) Field<uint32_t>((char*)placement, HALO2_PLACEMENT_VARIANT) = variant;
        memcpy(placement + HALO2_PLACEMENT_POSITION, position, 3 * sizeof(float));
        float* forward = (float*)(placement + HALO2_PLACEMENT_FORWARD);
        forward[0] = cosf(yaw);
        forward[1] = sinf(yaw);
        forward[2] = 0.0f;
        float* up = (float*)(placement + HALO2_PLACEMENT_UP);
        up[0] = 0.0f;
        up[1] = 0.0f;
        up[2] = 1.0f;
        return Engine<ObjectNewFn>(OFFSET_HALO2_OBJECT_NEW)(placement);
    }

    // Spawning: creates the selected tag where player 1 is aiming. A ray
    // from about eye height along the view (the engine's ray test with the
    // unit line-of-sight flags, ignoring the player and their vehicle) finds
    // the spot; the object goes a little back from it and above it. AI face
    // the player; everything else faces the way the player looks.
    void Spawn(const Players& p, const Objects& objects, uint32_t selection, int team) {
        VehicleChoice choice = ChoiceFor(selection);
        uint32_t tag = choice.vehicle;
        if (p.count == 0) { SpawnResult("No player to spawn for"); return; }
        if (!Halo2Tags::Get(tag)) { SpawnResult("Selection isn't in this map"); return; }
        char* control = *(char**)(g_module + OFFSET_HALO2_PLAYER_CONTROL);
        if (!control) { SpawnResult("No player to spawn for"); return; }
        char* look = control + HALO2_PLAYER_CONTROL_ENTRIES;  // entry 0 = local player 1
        float yaw = Field<float>(look, HALO2_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(look, HALO2_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        memcpy(eye, &Field<float>(p.units[0], HALO2_OBJECT_POSITION), sizeof(eye));
        eye[2] += kEyeHeight;
        float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};

        alignas(16) static uint8_t result[HALO2_COLLISION_RESULT_SIZE];
        memset(result, 0, sizeof(result));
        uint32_t vehicle = Field<uint32_t>(p.units[0], HALO2_OBJECT_PARENT);
        bool hit = Engine<CollisionTestFn>(OFFSET_HALO2_COLLISION_TEST_VECTOR)(
            HALO2_COLLISION_FLAGS_LINE_OF_SIGHT, eye, ray, p.datums[0], vehicle, result);
        float fraction = *(const float*)(result + HALO2_COLLISION_RESULT_FRACTION);
        if (!hit || !(fraction >= 0.0f && fraction <= 1.0f)) { SpawnResult("Nothing in range to spawn on"); return; }
        float at[3] = {eye[0] + ray[0] * fraction, eye[1] + ray[1] * fraction, eye[2] + ray[2] * fraction};
        float position[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                             at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        uint32_t group = Halo2Tags::GroupOf(tag);
        if (group == HALO2_TAG_VEHI || group == HALO2_TAG_SCEN || group == HALO2_TAG_MACH ||
            group == HALO2_TAG_CTRL || group == HALO2_TAG_BLOC) {
            // Backed off by its size so it isn't in a wall, and kept off the floor.
            float radius = Field<float>(Halo2Tags::Get(tag), HALO2_OBJE_BOUNDING_RADIUS);
            if (!(radius > 0.0f && radius < 100.0f)) radius = 2.0f;
            for (int k = 0; k < 3; ++k) position[k] = at[k] - dir[k] * radius;
            position[2] = (position[2] > at[2] ? position[2] : at[2]) + kVehicleLift;
        }

        uint32_t made;
        if (group == HALO2_TAG_CHAR) {
            float facing = yaw + kPi;  // towards the player; Halo 2 keeps yaw in 0..2pi
            if (facing >= 2.0f * kPi) facing -= 2.0f * kPi;
            made = SpawnActor(p, tag, position, facing, team);
            if (made != 0xFFFFFFFF) Follow(objects, made, tag, team);
        } else {
            made = NewObject(tag, choice.variant, position, yaw);
        }
        LOG("Halo 2: spawn %08X at %.2f %.2f %.2f (hit %.2f %.2f %.2f, fraction %.3f) -> object/actor %08X", tag,
            position[0], position[1], position[2], at[0], at[1], at[2], fraction, made);
        char text[96];
        std::string name = SelectionName(selection);
        if (made == 0xFFFFFFFF)
            sprintf_s(text, "Couldn't spawn %s here", name.c_str());
        else
            sprintf_s(text, "Spawned %s", name.c_str());
        SpawnResult(text);
    }

    // Spawned AI after death. The game doesn't revive them, so "Enable
    // respawn" is the mod's own, read once per death: on, an AI dead for
    // kRespawnMs is replaced by a fresh one of the same character and team
    // where it lay (the body is removed); off, it stays a body and is no
    // longer followed.
    void FollowSpawnedAi(const Players& p, const Objects& objects, bool respawn) {
        ULONGLONG now = GetTickCount64();
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            char* unit = objects.Get(ai.unit);
            if (!unit) { ai = {}; continue; }
            if (Field<float>(unit, HALO2_OBJECT_HEALTH) > 0.0f) { ai.deadAt = 0; continue; }
            if (!ai.deadAt) {
                ai.deadAt = now;
                LOG("Halo 2: spawned AI unit %08X died", ai.unit);
                continue;
            }
            if (now - ai.deadAt < kRespawnMs) continue;

            SpawnedAi old = ai;
            ai = {};
            if (!respawn || !Halo2Tags::Get(old.character)) {
                LOG("Halo 2: spawned AI unit %08X left as a body (respawn off)", old.unit);
                continue;
            }
            float position[3];
            memcpy(position, &Field<float>(unit, HALO2_OBJECT_POSITION), sizeof(position));
            position[2] += kSpawnLift;
            float yaw = 0.0f;
            if (p.count > 0) {
                const float* to = &Field<float>(p.units[0], HALO2_OBJECT_POSITION);
                yaw = atan2f(to[1] - position[1], to[0] - position[0]);
                if (yaw < 0.0f) yaw += 2.0f * kPi;
            }
            Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(old.unit);
            uint32_t actor = SpawnActor(p, old.character, position, yaw, old.team);
            if (actor != 0xFFFFFFFF) Follow(objects, actor, old.character, old.team);
            LOG("Halo 2: spawned AI respawned: old unit %08X -> actor %08X", old.unit, actor);
        }
    }

    // --- Despawn, Nuke, Random AI teams, Your team, no betrayal -------------

    bool InRange(const Players& p, char* object, float metres) {
        if (metres <= 0.0f) return true;
        if (p.count == 0) return false;
        const float* a = &Field<float>(p.units[0], HALO2_OBJECT_POSITION);
        const float* b = &Field<float>(object, HALO2_OBJECT_POSITION);
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        float range = metres / kMetresPerUnit;
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

    // ai_erase's per-actor step: detach the actor, then delete its unit.
    void EraseActor(const Array& actors, uint32_t datum) {
        char* actor = actors.Get(datum);
        if (!actor) return;
        uint32_t unit = Field<uint32_t>(actor, HALO2_ACTOR_UNIT);
        uint32_t swarm = Field<uint32_t>(actor, HALO2_ACTOR_SWARM);
        Engine<ActorDeleteFn>(OFFSET_HALO2_ACTOR_DELETE)(datum);
        if (unit != 0xFFFFFFFF) Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(unit);
        else if (swarm != 0xFFFFFFFF) Engine<ActorDeleteFn>(OFFSET_HALO2_ACTOR_DELETE_SWARM)(swarm);
    }

    // Despawn all custom AI: every AI the mod made (single spawns, rain, squads,
    // respawns, upgraded and swapped AI: all go through Follow), alive or a
    // body, is erased (actor_erase if it still has an actor, else object_delete).
    void DespawnCustomAi(const Objects& objects) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        int removed = 0;
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            if (char* unit = objects.Get(ai.unit)) {
                uint32_t actor = Field<uint32_t>(unit, HALO2_UNIT_ACTOR);
                if (actors.header && actors.Get(actor)) EraseActor(actors, actor);
                else Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(ai.unit);
                ++removed;
            }
            ai = {};
        }
        LOG("Halo 2: despawn all custom AI: %d removed", removed);
        char text[96];
        sprintf_s(text, removed ? "Despawned %d custom AI" : "No custom AI to despawn", removed);
        SpawnResult(text);
    }

    // Nuke: every AI unit that isn't a player (with `spare`, nor friends of
    // player 1's team) loses its cannot-take-damage flag and shield and is
    // killed with unit_kill's core (the flag the damage multiplier sets,
    // plus the game's own "changed" notification).
    void Nuke(const Players& p, const Objects& objects, bool spare) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        if (!actors.header) return;
        int mine = PlayerTeam(p);
        int killed = 0, spared = 0;
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            if (*(uint16_t*)actor == 0) continue;
            uint32_t datum = Field<uint32_t>(actor, HALO2_ACTOR_UNIT);
            char* unit = objects.Get(datum);
            if (!unit || p.Contains(unit)) continue;
            if (Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            if (spare && TeamsFriends(mine, Field<int16_t>(unit, HALO2_UNIT_TEAM))) { ++spared; continue; }
            Field<uint16_t>(unit, HALO2_OBJECT_DAMAGE_FLAGS) &= ~(uint16_t)HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE;
            Field<float>(unit, HALO2_OBJECT_SHIELD) = 0.0f;
            Engine<UnitKillFn>(OFFSET_HALO2_UNIT_KILL)(datum);
            ++killed;
        }
        LOG("Halo 2: nuke: %d AI killed, %d allies spared", killed, spared);
        char text[96];
        sprintf_s(text, killed ? "Nuked %d AI" : "No AI to nuke", killed);
        SpawnResult(text);
    }

    // "We all feel down sometimes": a grenade where each AI stands. The
    // projectile comes from the globals tag's grenade list (frag first). `all`
    // = the button; otherwise only AI not seen yet are handled, and the first
    // call after the checkbox is ticked just remembers the ones already there.
    std::unordered_set<uint32_t> g_grenadeSeen;
    bool g_grenadeAutoWas = false;

    uint32_t GrenadeProjectile() {
        char* matg = Halo2Tags::First(HALO2_TAG_MATG);
        for (int i = 0; matg && i < 2 && i < Halo2Tags::BlockCount(matg, HALO2_MATG_GRENADES); ++i) {
            char* el = Halo2Tags::BlockElement(matg, HALO2_MATG_GRENADES, HALO2_MATG_GRENADE_SIZE, i);
            uint32_t tag = el ? Field<uint32_t>(el, HALO2_MATG_GRENADE_PROJECTILE + HALO2_TAGREF_DATUM) : 0xFFFFFFFF;
            if (tag != 0xFFFFFFFF && Halo2Tags::Get(tag)) return tag;
        }
        return 0xFFFFFFFF;
    }

    void DropGrenades(const Players& p, const Objects& objects, const Games::Settings& s, bool all) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        if (!actors.header) return;
        bool firstAuto = !all && !g_grenadeAutoWas;
        g_grenadeAutoWas = !all || g_grenadeAutoWas;
        uint32_t tag = firstAuto ? 0 : GrenadeProjectile();
        if (!firstAuto && tag == 0xFFFFFFFF) {
            if (all) SpawnResult("No grenade in this map");
            return;
        }
        int mine = PlayerTeam(p);
        int dropped = 0;
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_grenadeSeen.count(datum)) continue;
            char* unit = objects.Get(Field<uint32_t>(actor, HALO2_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            g_grenadeSeen.insert(datum);
            if (firstAuto) continue;
            if (s.grenadeEnemiesOnly && TeamsFriends(mine, Field<int16_t>(unit, HALO2_UNIT_TEAM))) continue;
            float at[3];
            memcpy(at, &Field<float>(unit, HALO2_OBJECT_POSITION), sizeof(at));
            at[2] += 0.3f;
            if (NewObject(tag, 0, at, 0.0f) != 0xFFFFFFFF) ++dropped;
        }
        if (all || dropped) LOG("Halo 2: grenades (%s): %d dropped", all ? "button" : "auto", dropped);
        if (all) {
            char text[96];
            sprintf_s(text, dropped ? "Dropped %d grenades" : "No AI to drop grenades under", dropped);
            SpawnResult(text);
        }
    }

    // Random AI teams: each AI actor in range goes on one of the first N of
    // kFactionTeams (human is your side; the rest fight everyone else as the
    // allegiance mask says, the rogue team even itself). `all` = the button
    // (every AI again); otherwise each AI once, as it shows up.
    std::unordered_set<uint32_t> g_teamsSeen;

    void RandomTeams(const Players& p, const Objects& objects, const Games::Settings& s, bool all) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        if (!actors.header) return;
        int factions = (int)(s.randomTeamsFactions + 0.5f);
        int most = (int)_countof(kFactionTeams);
        factions = factions < 2 ? 2 : (factions > most ? most : factions);
        int changed = 0;
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (!all && g_teamsSeen.count(datum)) continue;
            char* unit = objects.Get(Field<uint32_t>(actor, HALO2_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            if (!InRange(p, unit, s.randomTeamsRange)) continue;
            g_teamsSeen.insert(datum);
            SetTeam(datum, kFactionTeams[(int)(Random01() * factions) % factions]);
            ++changed;
        }
        if (all || changed)
            LOG("Halo 2: random AI teams (%s): %d AI over %d teams", all ? "button" : "auto", changed, factions);
        if (all) {
            char text[96];
            sprintf_s(text, changed ? "Shuffled %d AI over %d teams" : "No AI in range", changed, factions);
            SpawnResult(text);
        }
    }

    // Your team: flips the allegiance table around the player's team (teams
    // are enemies unless an allegiance record says otherwise; 0x619E60 and
    // 0x619F40 are what ai_allegiance and ai_allegiance_remove call, and both
    // refresh the AI's cached hostility). The player stays on team 1; the
    // allegiances that involve it are replaced: Covenant = just the Covenant,
    // Rogue = nobody, Flood = just the Flood. The first change snapshots the
    // original ones, Normal puts them back. Checked every tick, as scripts or
    // a reverted checkpoint can bring the old table back.
    struct AllegiancePair { int16_t a, b; };
    AllegiancePair g_savedAllies[HALO2_ALLEG_MAX_RECORDS];
    int g_savedAlliesCount = 0;
    bool g_teamModified = false;

    void SetPlayerTeam(const Players& p, int choice) {
        if (p.count == 0 || (choice <= 0 && !g_teamModified)) return;
        char* g = Allegiance();
        if (!g) return;
        int me = PlayerTeam(p);

        AllegiancePair current[HALO2_ALLEG_MAX_RECORDS];
        int n = 0;
        for (int i = 0, count = AllegianceCount(g); i < count; ++i) {
            char* rec = AllegianceRecord(g, i);
            int16_t a = Field<int16_t>(rec, HALO2_ALLEG_REC_A), b = Field<int16_t>(rec, HALO2_ALLEG_REC_B);
            if (a == me || b == me) current[n++] = {a, b};
        }
        auto other = [&](const AllegiancePair& r) { return r.a == me ? r.b : r.a; };

        if (!g_teamModified) {  // the first change: remember what the mission has
            memcpy(g_savedAllies, current, sizeof(current));
            g_savedAlliesCount = n;
            g_teamModified = true;
        }

        // The teams that should be allied with the player's.
        AllegiancePair want[HALO2_ALLEG_MAX_RECORDS];
        int w = 0;
        if (choice <= 0) {
            for (int i = 0; i < g_savedAlliesCount; ++i) want[w++] = g_savedAllies[i];
        } else if (choice == 1) {
            // Not just team 3: live in Regret the elites and grunts are team 3 but the
            // jackals (and drones) are team 7 (prophet), allied to 3 only by the
            // mission's (3,7) record, so a (1,3) allegiance left them hostile. Every
            // Covenant-aligned team gets its own record: 3, 7 and heretic 6.
            want[w++] = {(int16_t)me, HALO2_TEAM_COVENANT};
            want[w++] = {(int16_t)me, HALO2_TEAM_PROPHET};
            want[w++] = {(int16_t)me, HALO2_TEAM_HERETIC};
        } else if (choice == 3) {
            want[w++] = {(int16_t)me, HALO2_TEAM_FLOOD};
        }  // 2 = rogue: none

        for (int i = 0; i < n; ++i) {
            bool keep = false;
            for (int k = 0; k < w; ++k) keep |= other(want[k]) == other(current[i]);
            if (keep) continue;
            Engine<AllegianceRemoveFn>(OFFSET_HALO2_AI_ALLEGIANCE_REMOVE)(current[i].a, current[i].b);
            LOG("Halo 2: your team: allegiance %d-%d removed", current[i].a, current[i].b);
        }
        for (int k = 0; k < w; ++k) {
            bool have = false;
            for (int i = 0; i < n; ++i) have |= other(current[i]) == other(want[k]);
            if (have) continue;
            Engine<AllegianceFn>(OFFSET_HALO2_AI_ALLEGIANCE)(want[k].a, want[k].b);
            LOG("Halo 2: your team: allegiance %d-%d added", want[k].a, want[k].b);
        }
        if (choice <= 0) { g_teamModified = false; g_savedAlliesCount = 0; }
    }

    // Allies never turn against you: the betrayal score (hits +1, kills +3,
    // decaying) breaks an allegiance once it reaches the record's threshold.
    // While on, the threshold of the player team's records is -1 (never), the
    // score 0, and a broken one is put back with the break function. The
    // thresholds are restored when it's switched off. `on` is also forced
    // while "Your team" is changed, so an allegiance it made can't break.
    struct SavedThreshold { int16_t a, b, threshold; };
    SavedThreshold g_savedThresholds[HALO2_ALLEG_MAX_RECORDS];
    int g_savedThresholdCount = 0;

    void NoBetrayal(const Players& p, bool on) {
        if (p.count == 0 || (!on && g_savedThresholdCount == 0)) return;
        char* g = Allegiance();
        if (!g) return;
        int me = PlayerTeam(p);
        for (int i = 0, count = AllegianceCount(g); i < count; ++i) {
            char* rec = AllegianceRecord(g, i);
            int16_t a = Field<int16_t>(rec, HALO2_ALLEG_REC_A), b = Field<int16_t>(rec, HALO2_ALLEG_REC_B);
            if (a != me && b != me) continue;
            int16_t& threshold = Field<int16_t>(rec, HALO2_ALLEG_REC_THRESHOLD);
            int saved = -1;
            for (int k = 0; k < g_savedThresholdCount; ++k)
                if ((g_savedThresholds[k].a == a && g_savedThresholds[k].b == b) ||
                    (g_savedThresholds[k].a == b && g_savedThresholds[k].b == a)) saved = k;
            if (on) {
                if (threshold != -1 && saved < 0 && g_savedThresholdCount < HALO2_ALLEG_MAX_RECORDS)
                    g_savedThresholds[g_savedThresholdCount++] = {a, b, threshold};
                threshold = -1;
                Field<int16_t>(rec, HALO2_ALLEG_REC_SCORE) = 0;
                if (Field<uint8_t>(rec, HALO2_ALLEG_REC_BROKEN)) {
                    Engine<AllegianceBreakFn>(OFFSET_HALO2_ALLEGIANCE_BREAK)(rec, false, false);
                    LOG("Halo 2: allegiance %d-%d put back (betrayal)", a, b);
                }
            } else if (saved >= 0 && threshold == -1) {
                threshold = g_savedThresholds[saved].threshold;
            }
        }
        if (!on) g_savedThresholdCount = 0;
    }

    // --- The Fun, Chaos and Skulls pages (games/fun.h) ---------------------

    using SetVelocityFn = void(__fastcall*)(uint32_t object, const float* linear, const float* angular);
    using MoveFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward, const float* up,
                                     const void*);
    LARGE_INTEGER g_lastFunTick{};

    // Makes a spawn selection at a position: an AI (followed, as the spawn
    // key's) or an object. Returns the object made (an AI's unit) or -1.
    uint32_t CreateAt(const Players& p, const Objects& objects, uint32_t selection, int team, const float* position,
                      float yaw) {
        VehicleChoice choice = ChoiceFor(selection);
        if (Halo2Tags::GroupOf(choice.vehicle) != HALO2_TAG_CHAR)
            return NewObject(choice.vehicle, choice.variant, position, yaw);
        uint32_t actor = SpawnActor(p, choice.vehicle, position, yaw, team);
        if (actor == 0xFFFFFFFF) return actor;
        Follow(objects, actor, choice.vehicle, team);
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        char* a = actors.header ? actors.Get(actor) : nullptr;
        return a ? Field<uint32_t>(a, HALO2_ACTOR_UNIT) : 0xFFFFFFFF;
    }

    class FunTitle : public Fun::Title {
    public:
        FunTitle(const Players& p, const Objects& objects) : m_p(p), m_objects(objects) {}
        const char* Name() override { return "Halo 2"; }
        uint32_t Player() override { return m_p.count > 0 ? m_p.datums[0] : 0xFFFFFFFF; }
        uint32_t PlayerVehicle() override {
            if (m_p.count == 0) return 0xFFFFFFFF;
            uint32_t top = 0xFFFFFFFF, at = m_p.datums[0];
            for (int depth = 0; depth < 5; ++depth) {
                char* object = m_objects.Get(at);
                uint32_t parent = object ? Field<uint32_t>(object, HALO2_OBJECT_PARENT) : 0xFFFFFFFF;
                if (!m_objects.OfType(parent, HALO2_OBJECT_TYPE_VEHICLE)) break;
                top = at = parent;
            }
            return top;
        }
        bool View(float* eye, float* dir) override {
            char* control = *(char**)(g_module + OFFSET_HALO2_PLAYER_CONTROL);
            if (!control || m_p.count == 0) return false;
            char* look = control + HALO2_PLAYER_CONTROL_ENTRIES;  // entry 0 = local player 1
            float yaw = Field<float>(look, HALO2_PLAYER_CONTROL_YAW);
            float pitch = Field<float>(look, HALO2_PLAYER_CONTROL_PITCH);
            dir[0] = cosf(yaw) * cosf(pitch);
            dir[1] = sinf(yaw) * cosf(pitch);
            dir[2] = sinf(pitch);
            memcpy(eye, &Field<float>(m_p.units[0], HALO2_OBJECT_POSITION), 3 * sizeof(float));
            eye[2] += kEyeHeight;
            return true;
        }
        bool Ray(const float* from, const float* vec, bool ignorePlayer, float* hit) override {
            alignas(16) static uint8_t result[HALO2_COLLISION_RESULT_SIZE];
            memset(result, 0, sizeof(result));
            uint32_t ignore = 0xFFFFFFFF, ignore2 = 0xFFFFFFFF;
            if (ignorePlayer && m_p.count > 0) {
                ignore = m_p.datums[0];
                ignore2 = Field<uint32_t>(m_p.units[0], HALO2_OBJECT_PARENT);
            }
            if (!Engine<CollisionTestFn>(OFFSET_HALO2_COLLISION_TEST_VECTOR)(HALO2_COLLISION_FLAGS_LINE_OF_SIGHT, from,
                                                                            vec, ignore, ignore2, result))
                return false;
            float fraction = *(const float*)(result + HALO2_COLLISION_RESULT_FRACTION);
            if (!(fraction >= 0.0f && fraction <= 1.0f)) return false;
            for (int k = 0; k < 3; ++k) hit[k] = from[k] + vec[k] * fraction;
            return true;
        }
        bool Position(uint32_t object, float* out) override {
            char* o = m_objects.Get(object);
            if (o) memcpy(out, &Field<float>(o, HALO2_OBJECT_POSITION), 3 * sizeof(float));
            return o != nullptr;
        }
        float Radius(uint32_t object) override {
            char* o = m_objects.Get(object);
            char* tag = o ? Halo2Tags::Get(Field<uint32_t>(o, HALO2_OBJECT_TAG_DATUM)) : nullptr;
            return tag ? Field<float>(tag, HALO2_OBJE_BOUNDING_RADIUS) : 0.0f;
        }
        void Teleport(uint32_t object, const float* position) override {
            Engine<MoveFn>(OFFSET_HALO2_OBJECT_SET_POSITION)(object, position, nullptr, nullptr, nullptr);
            const float still[3] = {};
            Engine<SetVelocityFn>(OFFSET_HALO2_OBJECT_SET_VELOCITY)(object, still, still);
            if (g_noclipActive && m_p.count > 0 && object == m_p.datums[0]) memcpy(g_anchor, position, sizeof(g_anchor));
        }
        void AddVelocity(uint32_t object, const float* add) override {
            char* o = m_objects.Get(object);
            if (!o) return;
            const float* now = &Field<float>(o, HALO2_OBJECT_VELOCITY);
            float velocity[3] = {now[0] + add[0], now[1] + add[1], now[2] + add[2]};
            Engine<SetVelocityFn>(OFFSET_HALO2_OBJECT_SET_VELOCITY)(object, velocity, nullptr);
        }
        uint32_t Create(uint32_t selection, int team, const float* position, float yaw) override {
            return CreateAt(m_p, m_objects, selection, team, position, yaw);
        }
        bool InUseByPlayer(uint32_t object) override {
            for (int i = 0; i < m_p.count; ++i) {
                if (Field<uint32_t>(m_p.units[i], HALO2_OBJECT_PARENT) == object) return true;
                for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w)
                    if (Field<uint32_t>(m_p.units[i], HALO2_UNIT_WEAPONS + w * 4) == object) return true;
            }
            return false;
        }
        void Delete(uint32_t object) override { Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(object); }
        void Message(const char* text) override { SpawnResult(text); }
        // Skulls are one byte each, read live by the game.
        bool ReadSkulls(uint64_t& mask) override {
            mask = 0;
            const uint8_t* bytes = (const uint8_t*)(g_module + OFFSET_HALO2_SKULLS);
            for (int i = 0; i < HALO2_SKULL_COUNT; ++i)
                if (bytes[i]) mask |= 1ull << i;
            return true;
        }
        void WriteSkulls(uint64_t mask) override {
            uint8_t* bytes = (uint8_t*)(g_module + OFFSET_HALO2_SKULLS);
            uint64_t ours = SkullMask();
            for (int i = 0; i < HALO2_SKULL_COUNT; ++i)
                if (ours & (1ull << i)) bytes[i] = (mask >> i) & 1;
        }
        // Not Envy (its own toggle) or Acrophobia (Fly).
        uint64_t SkullMask() override {
            return ((1ull << HALO2_SKULL_COUNT) - 1) & ~(1ull << HALO2_SKULL_ENVY) & ~(1ull << HALO2_SKULL_ACROPHOBIA);
        }

    private:
        const Players& m_p;
        const Objects& m_objects;
    };

    void RunFun(const Players& p, const Objects& objects, const Games::Settings& s) {
        FunTitle fun(p, objects);
        Fun::Tick(fun, s, TickSeconds(g_lastFunTick));
    }

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    bool ReadPlayers(Objects& objects, Players& p) {
        __try {
            Array players;
            if (!ReadTables(g_module, players, objects)) return false;
            p = FindPlayers(players, objects);
            return true;
        } __except (Log::Exception(GetExceptionInformation(), "Halo 2 main-thread players")) {
            return false;
        }
    }
}

// What the other Halo 2 feature files use (internal.h).
char* Halo2Internal::Module() { return g_module; }
uint32_t Halo2Internal::SpawnActor(const Players& p, uint32_t character, const float* position, float yaw, int team) {
    return ::SpawnActor(p, character, position, yaw, team);
}
int16_t Halo2Internal::ResolveSpawnTeam(const Players& p, int choice) { return choice > 0 ? ::ResolveTeam(p, choice) : 0; }
void Halo2Internal::SetActorTeam(uint32_t actor, int16_t team) { ::SetTeam(actor, team); }
bool Halo2Internal::TeamsFriends(int teamA, int teamB) { return ::TeamsFriends(teamA, teamB); }
void Halo2Internal::Follow(const Objects& objects, uint32_t actor, uint32_t character, int team) {
    ::Follow(objects, actor, character, team);
}
void Halo2Internal::Toast(const char* text) { ::SpawnResult(text); }
void Halo2Internal::Refollow(const Objects& objects, uint32_t oldUnit, uint32_t newActor, uint32_t newCharacter) {
    for (auto& ai : g_spawnedAi) {
        if (ai.unit != oldUnit) continue;
        int team = ai.team;
        ai = {};
        ::Follow(objects, newActor, newCharacter, team);
        return;
    }
}
void Halo2Internal::DeleteObject(uint32_t object) { Engine<ObjectDeleteFn>(OFFSET_HALO2_OBJECT_DELETE)(object); }

bool Halo2Internal::AimPoint(const Players& p, float* at, float* dir, float* facing) {
    if (p.count == 0) return false;
    char* control = *(char**)(g_module + OFFSET_HALO2_PLAYER_CONTROL);
    if (!control) return false;
    char* look = control + HALO2_PLAYER_CONTROL_ENTRIES;  // entry 0 = local player 1
    float yaw = Field<float>(look, HALO2_PLAYER_CONTROL_YAW);
    float pitch = Field<float>(look, HALO2_PLAYER_CONTROL_PITCH);
    dir[0] = cosf(yaw) * cosf(pitch);
    dir[1] = sinf(yaw) * cosf(pitch);
    dir[2] = sinf(pitch);
    float eye[3];
    memcpy(eye, &Field<float>(p.units[0], HALO2_OBJECT_POSITION), sizeof(eye));
    eye[2] += kEyeHeight;
    float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};
    alignas(16) static uint8_t result[HALO2_COLLISION_RESULT_SIZE];
    memset(result, 0, sizeof(result));
    uint32_t vehicle = Field<uint32_t>(p.units[0], HALO2_OBJECT_PARENT);
    bool hit = Engine<CollisionTestFn>(OFFSET_HALO2_COLLISION_TEST_VECTOR)(
        HALO2_COLLISION_FLAGS_LINE_OF_SIGHT, eye, ray, p.datums[0], vehicle, result);
    float fraction = *(const float*)(result + HALO2_COLLISION_RESULT_FRACTION);
    if (!hit || !(fraction >= 0.0f && fraction <= 1.0f)) return false;
    for (int k = 0; k < 3; ++k) at[k] = eye[k] + ray[k] * fraction;
    float f = yaw + kPi;
    if (f >= 2.0f * kPi) f -= 2.0f * kPi;
    *facing = f;
    return true;
}

namespace Halo2Cheats {
    void SetMainThreadModule(HMODULE halo2) { g_module = (char*)halo2; }

    void ResetMainThread() {
        g_noclipActive = false;
        g_noclipUnit = 0xFFFFFFFF;
        g_lastTick = {};
        g_give = {};
        memset(g_tracked, 0, sizeof(g_tracked));
        for (auto& ai : g_spawnedAi) ai = {};
        g_teamsSeen.clear();
        g_teamModified = false;  // a new map starts with its own allegiances
        g_savedAlliesCount = 0;
        g_savedThresholdCount = 0;
        g_lastFunTick = {};
        Fun::Reset();
        Halo2Extras::Reset();
        Halo2Fun::Reset();
        Halo2Upgrade::Reset();
    }

    void SpawnStatus(Games::Status& status) {
        std::lock_guard<std::mutex> lock(g_spawnLock);
        status.spawnDone = g_spawnDone;
        sprintf_s(status.spawnMessage, "%s", g_spawnMessage);
    }
}

void Halo2Cheats::MainThread(const Games::Settings& s, const Games::FrameInput& in) {
    if (!g_module) return;
    static bool s_first = true;  // don't run a "Give" pressed before this session
    if (s_first) {
        g_giveSerial = s.giveWeaponSerial;
        g_saveSerial = s.saveCheckpointSerial;
        g_loadSerial = s.loadCheckpointSerial;
        g_spawnSerial = s.spawnSerial;
        g_nukeSerial = s.nukeSerial;
        g_grenadeSerial = s.grenadeSerial;
        g_despawnSerial = s.despawnSerial;
        g_teamsSerial = s.randomTeamsSerial;
        s_first = false;
    }
    if (s.saveCheckpointSerial != g_saveSerial) {
        g_saveSerial = s.saveCheckpointSerial;
        LOG("Halo 2: saving a checkpoint");
        Halo2Extras::BeginOwnSave();
        Guard("Halo 2 save checkpoint", [&] { Engine<void(__fastcall*)()>(OFFSET_HALO2_GAME_SAVE_IMMEDIATE)(); });
        Halo2Extras::EndOwnSave();
    }
    if (s.loadCheckpointSerial != g_loadSerial) {
        g_loadSerial = s.loadCheckpointSerial;
        LOG("Halo 2: loading the last checkpoint");
        Guard("Halo 2 load checkpoint", [&] { Engine<void(__fastcall*)()>(OFFSET_HALO2_GAME_REVERT)(); });
    }

    Objects objects;
    Players p;
    if (!ReadPlayers(objects, p)) return;

    float dt = TickSeconds(g_lastTick);
    Guard("Halo 2 noclip", [&] { Noclip(p, s.noclip, in, dt); });
    Guard("Halo 2 damage multiplier", [&] { Damage(p, objects, s.damage); });
    Guard("Halo 2 rapid fire", [&] { RapidFire(p, objects, s.rapidFire, s.vehicleRapidFire, in.firing); });
    Guard("Halo 2 aimbot strength", [&] { if (s.aimbot && in.firing) AimPull(p, objects, s.aimbotStrength); });
    if (s.spawnSerial != g_spawnSerial) {
        g_spawnSerial = s.spawnSerial;
        if (s.spawnEnabled && s.spawnSquad >= 0)
            Guard("Halo 2 spawn squad", [&] { Halo2Upgrade::SpawnSquad(p, objects, s.spawnSquad, s.spawnTeam); });
        else if (s.spawnEnabled && s.spawnTag != 0xFFFFFFFF)
            Guard("Halo 2 spawn", [&] { Spawn(p, objects, s.spawnTag, s.spawnTeam); });
    }
    if (s.despawnSerial != g_despawnSerial) {
        g_despawnSerial = s.despawnSerial;
        Guard("Halo 2 despawn custom AI", [&] { DespawnCustomAi(objects); });
    }
    Guard("Halo 2 spawned AI", [&] { FollowSpawnedAi(p, objects, s.spawnRespawn); });
    if (s.nukeSerial != g_nukeSerial) {
        g_nukeSerial = s.nukeSerial;
        Guard("Halo 2 nuke", [&] { Nuke(p, objects, s.nukeSpareAllies); });
    }
    {
        bool button = s.grenadeSerial != g_grenadeSerial;
        g_grenadeSerial = s.grenadeSerial;
        if (button) Guard("Halo 2 drop grenades", [&] { DropGrenades(p, objects, s, true); });
        else if (s.grenadeAuto) Guard("Halo 2 drop grenades auto", [&] { DropGrenades(p, objects, s, false); });
        else if (g_grenadeAutoWas) { g_grenadeAutoWas = false; g_grenadeSeen.clear(); }
    }
    {
        bool button = s.randomTeamsSerial != g_teamsSerial;
        g_teamsSerial = s.randomTeamsSerial;
        if (button) Guard("Halo 2 random AI teams", [&] { RandomTeams(p, objects, s, true); });
        else if (s.randomTeamsAuto) Guard("Halo 2 random AI teams auto", [&] { RandomTeams(p, objects, s, false); });
    }
    Guard("Halo 2 your team", [&] { SetPlayerTeam(p, s.playerTeam); });
    Guard("Halo 2 no betrayal", [&] { NoBetrayal(p, s.noBetrayal || g_teamModified); });
    RunFun(p, objects, s);
    Guard("Halo 2 extras", [&] { Halo2Extras::Tick(s, in, p, objects); });
    Guard("Halo 2 fun extras", [&] { Halo2Fun::Tick(s, in, p, objects); });
    Guard("Halo 2 upgrade AI", [&] { Halo2Upgrade::Tick(s, p, objects); });
    bool request = s.giveWeaponSerial != g_giveSerial;
    if (request) {
        g_giveSerial = s.giveWeaponSerial;
        LOG("Halo 2: give weapon request %08X (%d player units)", s.giveWeapon, p.count);
    }
    __try {
        if (request) GiveWeapon(p, objects, s.giveWeapon);
        ContinueGive(p);
    } __except (Log::Exception(GetExceptionInformation(), "Halo 2 give weapon")) {
        g_give = {};
    }
}

// The chaos gun and recoil push around one barrel's projectiles (the
// projectile-creation hook, see offsets.h).
void Halo2Cheats::BeforeShot(uint32_t weapon, int16_t barrel, Fun::Shot& shot) {
    if (!Fun::Active() || !g_module) return;
    __try {
        Objects objects;
        Players p;
        Array players;
        if (!ReadTables(g_module, players, objects)) return;
        p = FindPlayers(players, objects);
        char* object = objects.Get(weapon);
        uint32_t holder = object ? Field<uint32_t>(object, HALO2_OBJECT_PARENT) : 0xFFFFFFFF;
        if (holder == 0xFFFFFFFF) return;

        // Whose shot: a player's own weapon, or a gun of the vehicle (or
        // turret, or what that's mounted on) a player is in.
        int shooter = -1;
        bool vehicleGun = false;
        for (int i = 0; i < p.count && shooter < 0; ++i) {
            if (p.datums[i] == holder) { shooter = i; break; }
            uint32_t at = Field<uint32_t>(p.units[i], HALO2_OBJECT_PARENT);
            for (int depth = 0; depth < 3 && at != 0xFFFFFFFF; ++depth) {
                if (at == holder) { shooter = i; vehicleGun = true; break; }
                char* up = objects.Get(at);
                at = up ? Field<uint32_t>(up, HALO2_OBJECT_PARENT) : 0xFFFFFFFF;
            }
        }
        if (shooter < 0) return;

        uint32_t* fields[Fun::Shot::kMax];
        int fieldCount = 0;
        char* tag = Halo2Tags::Get(Field<uint32_t>(object, HALO2_OBJECT_TAG_DATUM));
        for (int index : {(int)barrel, barrel == 0 ? 1 : -1}) {
            char* def = index >= 0 ? Halo2Tags::BlockElement(tag, HALO2_WEAP_BARRELS, HALO2_WEAP_BARREL_SIZE, index)
                                   : nullptr;
            if (def) fields[fieldCount++] = &Field<uint32_t>(def, HALO2_BARREL_PROJECTILE + HALO2_TAGREF_DATUM);
        }

        char* control = *(char**)(g_module + OFFSET_HALO2_PLAYER_CONTROL);
        float dir[3] = {};
        if (control) {
            char* look = control + HALO2_PLAYER_CONTROL_ENTRIES + shooter * HALO2_PLAYER_CONTROL_SIZE;
            float yaw = Field<float>(look, HALO2_PLAYER_CONTROL_YAW);
            float pitch = Field<float>(look, HALO2_PLAYER_CONTROL_PITCH);
            dir[0] = cosf(yaw) * cosf(pitch);
            dir[1] = sinf(yaw) * cosf(pitch);
            dir[2] = sinf(pitch);
        }
        // Seated, the whole vehicle moves: the top vehicle above the shooter.
        uint32_t target = p.datums[shooter];
        for (int depth = 0; depth < 5; ++depth) {
            char* at = objects.Get(target);
            uint32_t parent = at ? Field<uint32_t>(at, HALO2_OBJECT_PARENT) : 0xFFFFFFFF;
            if (!objects.OfType(parent, HALO2_OBJECT_TYPE_VEHICLE)) break;
            target = parent;
        }
        if ((target == p.datums[shooter] && vehicleGun) || !control) target = 0xFFFFFFFF;
        Fun::BeforeShot(fields, fieldCount, target, dir, shot);
    } __except (Log::Exception(GetExceptionInformation(), "Halo 2 shot effects")) {
    }
}
