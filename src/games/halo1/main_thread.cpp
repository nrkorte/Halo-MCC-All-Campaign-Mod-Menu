// Halo: CE cheats that call engine functions. Runs once per game tick on the
// game thread, before the tick (see halo1.cpp).

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "MinHook.h"
#include "cheat_util.h"
#include "cheats.h"
#include "core/log.h"
#include "games/fun.h"
#include "games/squads.h"
#include "tags.h"

using namespace Halo1Cheats;

namespace {
    constexpr float kNoclipSpeed = 6.0f;  // world units per second
    constexpr float kNoclipFastScale = 4.0f;
    constexpr int kMaxTracked = 2048;     // object table slots we track for damage
    constexpr float kPi = 3.14159265f;
    constexpr float kAimMinCone = 3.0f;   // degrees from the crosshair, at 1% strength
    constexpr float kAimMaxCone = 55.0f;  // at 100%: about the whole screen
    constexpr float kAimRange = 200.0f;   // world units
    constexpr float kHeadAboveCenter = 0.2f;  // CE has no head-position call; aim a bit above centre
    constexpr float kEyeAboveCenter = 0.25f;

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using UnitAddWeaponFn = bool(__fastcall*)(uint32_t unit, uint32_t weapon, int16_t mode);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up);
    using CollisionTestFn = bool(__fastcall*)(uint32_t flags, const float* point, const float* vector,
                                              uint32_t ignore, void* result);
    using UltimateParentFn = uint32_t(__fastcall*)(uint32_t object);
    using HeadPositionFn = void(__fastcall*)(uint32_t unit, float* out);
    using ActorSpawnFn = uint32_t(__fastcall*)(uint32_t variant, uint32_t encounter, int32_t squad, void* spawnPoint,
                                               bool majorVariant, int16_t permutation);
    using ActorMigrateFn = void(__fastcall*)(uint32_t actor, uint32_t encounter, uint16_t squad);
    using ActorEraseFn = void(__fastcall*)(uint32_t actor, bool flag);
    using SetScaleFn = void(__fastcall*)(uint32_t object, float scale, uint16_t ticks);
    using ActorSetTeamFn = void(__fastcall*)(uint32_t actor, int16_t team);

    constexpr float kMetresPerUnit = 3.048f;  // a world unit is 10 feet
    constexpr float kSpawnRange = 300.0f;  // world units the spawn ray reaches
    constexpr float kSpawnBackOff = 0.3f;  // back along the ray from the hit, so it isn't inside the surface
    constexpr float kSpawnLift = 0.25f;    // and up, so it doesn't clip into the ground
    constexpr float kVehicleLift = 0.5f;   // vehicles and scenery: above the higher of the hit and the backed-off point

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    // Time stop (see TimeStop): the frozen units, read by the object-update hook.
    std::unordered_set<uint32_t> g_frozen;
    volatile bool g_timeStopOn = false;

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
    uint32_t g_despawnSerial = 0;
    uint32_t g_randomTeamsSerial = 0;
    uint32_t g_upgradeAiSerial = 0;
    uint32_t g_randomSwapSerial = 0;

    // AI we spawned, followed after death so "Enable respawn" decides
    // whether they come back (see FollowSpawnedAi).
    struct SpawnedAi {
        uint32_t unit = 0xFFFFFFFF;
        uint32_t variant = 0xFFFFFFFF;  // 'actv' tag, to respawn it
        int team = 0;                   // spawnTeam it was made with
        ULONGLONG deadAt = 0;           // when it was first seen dead, 0 = alive
    };
    constexpr int kMaxSpawnedAi = 256;
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
        LOG("Halo CE: spawn: %s", text);
    }

    float TickSeconds(LARGE_INTEGER& last) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart : 0.0f;
        last = now;
        return dt > 0.1f ? 0.1f : dt;
    }

    uint32_t g_rng = 0x2545F491u;  // xorshift
    float Random01() {
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return (g_rng >> 8) / 16777216.0f;
    }

    // Within `metres` of player 1 (0 = everywhere).
    bool InRange(const Players& p, char* object, float metres) {
        if (metres <= 0.0f || p.count == 0) return true;
        const float* a = &Field<float>(p.units[0], HALO1_OBJECT_POSITION);
        const float* b = &Field<float>(object, HALO1_OBJECT_POSITION);
        float range = metres / kMetresPerUnit;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz <= range * range;
    }

    uint32_t DatumOf(int index, char* entry) { return ((uint32_t)*(uint16_t*)entry << 16) | (uint32_t)index; }

    // Holds player 1 in place, and flies them where they aim while the noclip
    // button is held. Sets only the position (as Reach's noclip does): a
    // player's teleport also resets their view.
    void Noclip(const Players& p, bool on, const Games::FrameInput& in, float dt) {
        bool seated = p.count > 0 && Field<uint32_t>(p.units[0], HALO1_OBJECT_PARENT) != 0xFFFFFFFF;
        bool sameUnit = p.count > 0 && p.datums[0] == g_noclipUnit && !seated;
        if (g_noclipActive && (!on || !sameUnit)) {
            g_noclipActive = false;
            LOG("Halo CE: noclip off");
        }
        if (!on || p.count == 0 || seated) return;

        char* unit = p.units[0];
        float* pos = &Field<float>(unit, HALO1_OBJECT_POSITION);
        if (!g_noclipActive) {
            memcpy(g_anchor, pos, sizeof(g_anchor));
            g_noclipUnit = p.datums[0];
            g_noclipActive = true;
            LOG("Halo CE: noclip on at %.2f %.2f %.2f", pos[0], pos[1], pos[2]);
        }

        const float* aim = &Field<float>(unit, HALO1_UNIT_AIM_VECTOR);
        if (in.noclipForward) {
            float step = kNoclipSpeed * (in.fast ? kNoclipFastScale : 1.0f) * dt;
            for (int k = 0; k < 3; ++k) g_anchor[k] += aim[k] * step;
        }

        Engine<SetPositionFn>(OFFSET_HALO1_OBJECT_SET_POSITION)(p.datums[0], g_anchor, nullptr, nullptr);
        memset(&Field<float>(unit, HALO1_OBJECT_VELOCITY), 0, 3 * sizeof(float));

        static ULONGLONG s_lastLog = 0;  // to check the aim vector and position in the log
        if (GetTickCount64() - s_lastLog > 2000) {
            s_lastLog = GetTickCount64();
            LOG("Halo CE: noclip aim %.2f %.2f %.2f, anchor %.1f %.1f %.1f, unit at %.1f %.1f %.1f", aim[0], aim[1],
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
        int count = objects.table.Count();
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
            auto& health = Field<float>(object, HALO1_OBJECT_HEALTH);
            auto& shield = Field<float>(object, HALO1_OBJECT_SHIELD);
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
                    Field<uint16_t>(object, HALO1_OBJECT_DAMAGE_FLAGS) |= HALO1_DAMAGE_FLAG_KILL;
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
        for (int w = 0; w < HALO1_UNIT_WEAPON_COUNT; ++w)
            if (Field<uint32_t>(unit, HALO1_UNIT_WEAPONS + w * 4) == weapon) return w;
        return -1;
    }

    void AddAndSelect(char* unit, uint32_t unitDatum, uint32_t weapon) {
        bool added = Engine<UnitAddWeaponFn>(OFFSET_HALO1_UNIT_ADD_WEAPON)(unitDatum, weapon, HALO1_ADD_WEAPON_SELECT);
        int slot = added ? SlotOf(unit, weapon) : -1;
        if (slot < 0) Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(weapon);
        LOG("Halo CE: give weapon: %s (object %08X, slot %d)", slot >= 0 ? "added" : "add FAILED", weapon, slot);
    }

    // Replaces player 1's held weapon with a new one of the chosen type (or
    // fills a free slot).
    void GiveWeapon(const Players& p, const Objects& objects, uint32_t weaponTag) {
        if (p.count == 0) { LOG("Halo CE: give weapon: no player unit"); return; }
        if (!Halo1Tags::Get(weaponTag)) { LOG("Halo CE: give weapon %08X: tag not loaded", weaponTag); return; }
        char* unit = p.units[0];
        uint32_t unitDatum = p.datums[0];

        if (g_give.active) {
            Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
        }

        static uint8_t placement[HALO1_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO1_OBJECT_PLACEMENT_NEW)(placement, weaponTag, unitDatum);
        uint32_t weapon = Engine<ObjectNewFn>(OFFSET_HALO1_OBJECT_NEW)(placement);
        if (weapon == 0xFFFFFFFF) {
            LOG("Halo CE: give weapon %08X: the game couldn't create it here", weaponTag);
            return;
        }

        bool freeSlot = false;
        for (int w = 0; w < HALO1_PLAYER_WEAPON_SLOTS; ++w)
            freeSlot |= Field<uint32_t>(unit, HALO1_UNIT_WEAPONS + w * 4) == 0xFFFFFFFF;

        uint32_t held = 0xFFFFFFFF;
        if (freeSlot || !HeldWeapon(unit, objects, &held)) {
            AddAndSelect(unit, unitDatum, weapon);
            return;
        }
        Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(held);
        g_give = {true, weapon, held, 0};
        LOG("Halo CE: give weapon %08X: removing held %08X, adding once its slot frees", weaponTag, held);
    }

    void ContinueGive(const Players& p) {
        if (!g_give.active) return;
        if (p.count == 0) {
            Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
            return;
        }
        if (SlotOf(p.units[0], g_give.held) < 0) {
            AddAndSelect(p.units[0], p.datums[0], g_give.weapon);
            g_give = {};
        } else if (++g_give.frames > 120) {
            Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(g_give.weapon);
            LOG("Halo CE: give weapon: held weapon never left the inventory; gave up");
            g_give = {};
        }
    }

    std::string ShortName(uint32_t tag) {
        uint32_t group = Halo1Tags::GroupOf(tag);
        if (group)
            for (auto& choice : Halo1Tags::ListGroup(group))
                if (choice.datum == tag) return choice.name;
        return "selection";
    }

    // actor_set_team only writes the unit's team (+0x74); the AI also keeps its
    // own team on the actor (+0x32), which stayed at the variant's default (a
    // swapped marine turned Elite kept fighting as a Covenant one). Both are set.
    // Team 6 is the "rogue" team. Every team is its own friend in the allegiance
    // matrix (the diagonal), so rogues would fight everyone but each other; clearing
    // team 6's own bit makes them hostile to each other as well. New perception
    // props read the matrix when they're made, so this is done before the first
    // rogue gets its team.
    void MakeRogueFightItself() {
        char* g = *(char**)(g_module + OFFSET_HALO1_ALLEGIANCE);
        if (!g) return;
        constexpr int kIndex = 6 * 10 + 6;
        ((uint32_t*)(g + HALO1_ALLEGIANCE_FRIENDLY))[kIndex >> 5] &= ~(1u << (kIndex & 31));
    }

    void SetActorTeam(uint32_t actorDatum, int16_t team) {
        if (team == 6) MakeRogueFightItself();
        Engine<ActorSetTeamFn>(OFFSET_HALO1_ACTOR_SET_TEAM)(actorDatum, team);
        Array actors{*(char**)(g_module + OFFSET_HALO1_ACTORS)};
        if (char* actor = actors.header ? actors.Get(actorDatum) : nullptr)
            Field<int16_t>(actor, HALO1_ACTOR_TEAM) = team;
    }

    // The AI team picker (Settings::spawnTeam): 0 = their own, 1 = ally, 2 = Covenant,
    // 3 = Flood, 4 = rogue (a team nobody is allied with, so it fights everyone),
    // 5 = random. "Ally" is whichever team the player is allied with right now (the
    // humans, or the Covenant / Flood while "Your team" has changed it). Returns
    // the game's team number, or 0 to leave the AI on its own.
    int ResolveSpawnTeam(int choice) {
        constexpr int kRogue = 6;
        switch (choice) {
            case 1:
                for (int t : {HALO1_TEAM_HUMAN, HALO1_TEAM_COVENANT, HALO1_TEAM_FLOOD})
                    if (TeamsFriends(g_module, HALO1_TEAM_PLAYER, t)) return t;
                return kRogue;
            case 2: return HALO1_TEAM_COVENANT;
            case 3: return HALO1_TEAM_FLOOD;
            case 4: return kRogue;
            case 5: {
                const int teams[] = {HALO1_TEAM_HUMAN, HALO1_TEAM_COVENANT, HALO1_TEAM_FLOOD, HALO1_TEAM_SENTINEL, kRogue};
                return teams[(int)(Random01() * _countof(teams)) % _countof(teams)];
            }
            default: return 0;
        }
    }

    // An AI from an actor variant, through the engine's own spawner with no
    // encounter (as a vehicle's built-in crew is made): the variant's unit,
    // its weapon and a free actor. Returns the actor datum or -1.
    uint32_t SpawnActor(uint32_t variant, const float* position, float yaw, int team) {
        alignas(16) static uint8_t point[HALO1_SPAWN_POINT_SIZE];
        memset(point, 0, sizeof(point));
        memcpy(point, position, 3 * sizeof(float));
        Field<float>((char*)point, HALO1_SPAWN_POINT_YAW) = yaw;
        Field<int16_t>((char*)point, HALO1_SPAWN_POINT_1A) = -1;
        uint32_t actor = Engine<ActorSpawnFn>(OFFSET_HALO1_ACTOR_SPAWN)(variant, 0xFFFFFFFF, -1, point, false, 0);
        int actual = ResolveSpawnTeam(team);
        if (actor != 0xFFFFFFFF && actual > 0) SetActorTeam(actor, (int16_t)actual);
        return actor;
    }

    // The unit an actor was just made as: an AI biped of the variant's unit
    // type within 1 unit of where it was made (the actor's own unit field
    // isn't mapped for CE).
    uint32_t UnitNear(const Objects& objects, uint32_t variant, const float* position) {
        char* tag = Halo1Tags::Get(variant);
        uint32_t unitTag = tag ? Field<uint32_t>(tag, HALO1_ACTV_UNIT) : 0xFFFFFFFF;
        uint32_t found = 0xFFFFFFFF;
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = -1;
            char* object = salt ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || type != HALO1_OBJECT_TYPE_BIPED) continue;
            if (Field<uint32_t>(object, HALO1_OBJECT_TAG_DATUM) != unitTag) continue;
            if (Field<uint32_t>(object, HALO1_UNIT_ACTOR) == 0xFFFFFFFF) continue;
            const float* at = &Field<float>(object, HALO1_OBJECT_POSITION);
            float d[3] = {at[0] - position[0], at[1] - position[1], at[2] - position[2]};
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= 1.0f) found = ((uint32_t)salt << 16) | (uint32_t)i;
        }
        return found;
    }

    // Starts following an AI the mod made. The unit comes from the actor (+0x10);
    // finding it by tag and position picked the wrong neighbour in tight
    // clusters of the same variant (squads), leaving some untracked.
    void Follow(const Objects& objects, uint32_t variant, int team, const float* position,
                uint32_t actorDatum = 0xFFFFFFFF) {
        uint32_t unit = 0xFFFFFFFF;
        Array actors{*(char**)(g_module + OFFSET_HALO1_ACTORS)};
        if (actors.header && actorDatum != 0xFFFFFFFF)
            if (char* actor = actors.Get(actorDatum)) unit = Field<uint32_t>(actor, HALO1_ACTOR_UNIT);
        if (!objects.Get(unit)) unit = UnitNear(objects, variant, position);
        if (unit == 0xFFFFFFFF) {
            LOG("Halo CE: spawned AI: couldn't find its unit, not followed");
            return;
        }
        // A free slot, else the body that's been dead longest.
        SpawnedAi* slot = nullptr;
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF || !objects.Get(ai.unit)) { slot = &ai; break; }
            if (ai.deadAt && (!slot || ai.deadAt < slot->deadAt)) slot = &ai;
        }
        if (slot) *slot = {unit, variant, team, 0};
    }

    uint32_t NewObject(uint32_t tag, const float* position, float yaw) {
        static uint8_t placement[HALO1_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO1_OBJECT_PLACEMENT_NEW)(placement, tag, 0xFFFFFFFF);
        memcpy(placement + HALO1_PLACEMENT_POSITION, position, 3 * sizeof(float));
        float* forward = (float*)(placement + HALO1_PLACEMENT_FORWARD);
        forward[0] = cosf(yaw);
        forward[1] = sinf(yaw);
        forward[2] = 0.0f;
        return Engine<ObjectNewFn>(OFFSET_HALO1_OBJECT_NEW)(placement);
    }

    // Spawning: creates the selected tag where player 1 is aiming. A ray
    // from their head along the view (the engine's ray test with autoaim's
    // flags, ignoring the player and their vehicle) finds the spot; the
    // object goes a little back from it and above it. AI face the player;
    // everything else faces the way the player looks. CE keeps every tag in
    // the map loaded, so there are no designer zones to load first.
    void Spawn(const Players& p, const Objects& objects, uint32_t tag, int team) {
        if (p.count == 0) { SpawnResult("No player to spawn for"); return; }
        if (!Halo1Tags::Get(tag)) { SpawnResult("Selection isn't in this map"); return; }
        char* control = *(char**)(g_module + OFFSET_HALO1_PLAYER_CONTROL);  // entry 0 = local player 1
        if (!control) { SpawnResult("No player to spawn for"); return; }
        float yaw = Field<float>(control, HALO1_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO1_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_HALO1_UNIT_HEAD_POSITION)(p.datums[0], eye);
        float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};

        alignas(16) static uint8_t result[HALO1_COLLISION_RESULT_SIZE];
        memset(result, 0, sizeof(result));
        uint32_t ignore = Engine<UltimateParentFn>(OFFSET_HALO1_OBJECT_ULTIMATE_PARENT)(p.datums[0]);
        bool hit = Engine<CollisionTestFn>(OFFSET_HALO1_COLLISION_TEST_VECTOR)(HALO1_COLLISION_FLAGS_AUTOAIM, eye, ray,
                                                                              ignore, result);
        if (!hit) { SpawnResult("Nothing in range to spawn on"); return; }
        const float* at = (const float*)(result + HALO1_COLLISION_RESULT_POINT);
        float position[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                             at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        uint32_t group = Halo1Tags::GroupOf(tag);
        if (group == HALO1_TAG_VEHI || group == HALO1_TAG_SCEN || group == HALO1_TAG_MACH || group == HALO1_TAG_CTRL) {
            // Backed off by its size so it isn't in a wall, and kept off the floor.
            float radius = Field<float>(Halo1Tags::Get(tag), HALO1_OBJE_BOUNDING_RADIUS);
            if (!(radius > 0.0f && radius < 100.0f)) radius = 2.0f;
            for (int k = 0; k < 3; ++k) position[k] = at[k] - dir[k] * radius;
            position[2] = (position[2] > at[2] ? position[2] : at[2]) + kVehicleLift;
        }

        uint32_t made;
        if (group == HALO1_TAG_ACTV) {
            float facing = yaw + kPi;  // towards the player; CE keeps yaw in 0..2pi
            if (facing >= 2.0f * kPi) facing -= 2.0f * kPi;
            made = SpawnActor(tag, position, facing, team);
            if (made != 0xFFFFFFFF) Follow(objects, tag, team, position, made);
        } else {
            made = NewObject(tag, position, yaw);
        }
        LOG("Halo CE: spawn %08X at %.2f %.2f %.2f (hit %.2f %.2f %.2f, fraction %.3f) -> object/actor %08X", tag,
            position[0], position[1], position[2], at[0], at[1], at[2],
            *(const float*)(result + HALO1_COLLISION_RESULT_FRACTION), made);
        char text[96];
        std::string name = ShortName(tag);
        if (made == 0xFFFFFFFF)
            sprintf_s(text, "Couldn't spawn %s here", name.c_str());
        else
            sprintf_s(text, "Spawned %s", name.c_str());
        SpawnResult(text);
    }

    // Spawned AI after death. CE doesn't revive AI, so "Enable respawn" is
    // the mod's own, read once per death: on, an AI dead for kRespawnMs is
    // replaced by a fresh one of the same variant and team where it lay (the
    // body is removed); off, it stays a body and is no longer followed.
    void FollowSpawnedAi(const Players& p, const Objects& objects, bool respawn) {
        ULONGLONG now = GetTickCount64();
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            char* unit = objects.Get(ai.unit);
            if (!unit) { ai = {}; continue; }
            bool dead = (Field<uint16_t>(unit, HALO1_OBJECT_DAMAGE_FLAGS) & HALO1_DAMAGE_FLAG_DEAD) ||
                        Field<float>(unit, HALO1_OBJECT_HEALTH) <= 0.0f;
            if (!dead) { ai.deadAt = 0; continue; }
            if (!ai.deadAt) {
                ai.deadAt = now;
                LOG("Halo CE: spawned AI unit %08X died", ai.unit);
                continue;
            }
            if (now - ai.deadAt < kRespawnMs) continue;

            SpawnedAi old = ai;
            ai = {};
            if (!respawn || !Halo1Tags::Get(old.variant)) {
                LOG("Halo CE: spawned AI unit %08X left as a body (respawn off)", old.unit);
                continue;
            }
            float position[3];
            memcpy(position, &Field<float>(unit, HALO1_OBJECT_POSITION), sizeof(position));
            position[2] += kSpawnLift;
            float yaw = 0.0f;
            if (p.count > 0) {
                const float* to = &Field<float>(p.units[0], HALO1_OBJECT_POSITION);
                yaw = atan2f(to[1] - position[1], to[0] - position[0]);
                if (yaw < 0.0f) yaw += 2.0f * kPi;
            }
            Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(old.unit);
            uint32_t actor = SpawnActor(old.variant, position, yaw, old.team);
            if (actor != 0xFFFFFFFF) Follow(objects, old.variant, old.team, position, actor);
            LOG("Halo CE: spawned AI respawned: old unit %08X -> actor %08X", old.unit, actor);
        }
    }

    // Rapid fire: while the player fires, their triggers always count as long
    // past the last shot, so every tick can fire. Only while firing, so the
    // firing kick stops when they let go (patching out the reset kept it
    // shaking forever).
    // Vehicle rapid fire does the same for the weapons of the vehicle each
    // player is in. Both also skip the chambering delay after a shot (what
    // held the Scorpion cannon and the Banshee's fuel rod to one shot every
    // few seconds).
    // Vehicle guns skip chambering even when not "firing": that only reads
    // the right trigger, and the Banshee's bombs are on the left.
    void RapidFire(const Players& p, const Objects& objects, bool own, bool vehicles, bool firing) {
        char* units[kMaxLocalPlayers * 2];
        int count = 0;
        for (int i = 0; i < p.count; ++i) {
            if (own && firing) units[count++] = p.units[i];
            if (char* vehicle = vehicles ? VehicleOf(p.units[i], objects) : nullptr) units[count++] = vehicle;
        }
        for (int i = 0; i < count; ++i) {
            for (int w = 0; w < HALO1_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(units[i], objects, w);
                if (!weapon) continue;
                for (int t = 0; firing && t < HALO1_WEAPON_TRIGGER_COUNT; ++t)
                    Field<uint8_t>(weapon, HALO1_WEAPON_TRIGGERS + t * HALO1_WEAPON_TRIGGER_SIZE) = HALO1_TRIGGER_READY;
                for (int m = 0; m < HALO1_WEAPON_MAGAZINE_COUNT; ++m) {
                    int magazine = HALO1_WEAPON_MAGAZINES + m * HALO1_WEAPON_MAGAZINE_SIZE;
                    if (Field<int16_t>(weapon, magazine + HALO1_MAGAZINE_STATE) != HALO1_MAGAZINE_CHAMBERING) continue;
                    Field<int16_t>(weapon, magazine + HALO1_MAGAZINE_STATE) = 0;
                    Field<int16_t>(weapon, magazine + HALO1_MAGAZINE_TIMER) = 0;
                }
            }
        }
    }

    // Aimbot strength: while firing, turns player 1's view toward the enemy
    // nearest the crosshair (see the Halo 3 version).
    void AimPull(const Players& p, const Objects& objects, float strength) {
        if (strength <= 0.0f || p.count == 0) return;
        if (IsMeleeOnly(HeldWeapon(p.units[0], objects))) return;  // no pull while holding a melee weapon
        char* control = *(char**)(g_module + OFFSET_HALO1_PLAYER_CONTROL);  // entry 0 = local player 1
        if (!control) return;
        float s = strength / 100.0f;
        float& yaw = Field<float>(control, HALO1_PLAYER_CONTROL_YAW);
        float& pitch = Field<float>(control, HALO1_PLAYER_CONTROL_PITCH);
        float forward[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        const float* center = &Field<float>(p.units[0], HALO1_OBJECT_CENTER);
        float eye[3] = {center[0], center[1], center[2] + kEyeAboveCenter};

        float minCos = cosf((kAimMinCone + (kAimMaxCone - kAimMinCone) * s) * kPi / 180.0f);
        float best[3] = {};
        float bestCos = minCos;
        bool found = false;
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsKillableEnemy(object, type, p)) continue;
            if (Field<float>(object, HALO1_OBJECT_HEALTH) <= 0.0f) continue;

            const float* c = &Field<float>(object, HALO1_OBJECT_CENTER);
            float d[3] = {c[0] - eye[0], c[1] - eye[1], c[2] + kHeadAboveCenter - eye[2]};
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 0.1f || len > kAimRange) continue;
            float cosine = (d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2]) / len;
            if (cosine <= bestCos) continue;
            bestCos = cosine;
            memcpy(best, d, sizeof(best));
            found = true;
        }
        if (!found) return;

        float targetYaw = atan2f(best[1], best[0]);
        float targetPitch = atan2f(best[2], sqrtf(best[0] * best[0] + best[1] * best[1]));
        float dyaw = remainderf(targetYaw - yaw, 2.0f * kPi);
        float turn = s * s;
        yaw += dyaw * turn;
        if (yaw < 0.0f) yaw += 2.0f * kPi;  // CE keeps yaw in 0..2pi
        if (yaw >= 2.0f * kPi) yaw -= 2.0f * kPi;
        pitch += (targetPitch - pitch) * turn;
    }

    // --- Fun, Chaos and Skulls pages (shared logic in games/fun.cpp) ---------
    LARGE_INTEGER g_lastFunTick{};

    // Makes a spawn selection at a position: an AI (followed, as the spawn
    // key's) or an object. Returns the object made (an AI's unit) or -1.
    uint32_t CreateAt(const Objects& objects, uint32_t tag, int team, const float* position, float yaw) {
        if (!Halo1Tags::Get(tag)) return 0xFFFFFFFF;
        if (Halo1Tags::GroupOf(tag) != HALO1_TAG_ACTV) return NewObject(tag, position, yaw);
        uint32_t actor = SpawnActor(tag, position, yaw, team);
        if (actor == 0xFFFFFFFF) return 0xFFFFFFFF;
        Follow(objects, tag, team, position, actor);
        Array actors{*(char**)(g_module + OFFSET_HALO1_ACTORS)};
        char* a = actors.header ? actors.Get(actor) : nullptr;
        uint32_t unit = a ? Field<uint32_t>(a, HALO1_ACTOR_UNIT) : 0xFFFFFFFF;
        return objects.Get(unit) ? unit : UnitNear(objects, tag, position);
    }


    // Pushes an object by `add` world units per second. Bipeds go through the
    // engine's own push (a scratch copy: it halves the vector, so doubled
    // first); vehicles through theirs; anything else loose gets its velocity
    // written and its physics woken. Held or seated objects are left alone.
    void AddVelocityTo(const Objects& objects, uint32_t datum, const float* add) {
        int type = -1;
        char* o = objects.Get(datum, &type);
        if (!o || Field<uint32_t>(o, HALO1_OBJECT_PARENT) != 0xFFFFFFFF) return;
        float dv[3] = {add[0] / HALO1_TICKS_PER_SECOND, add[1] / HALO1_TICKS_PER_SECOND,
                       add[2] / HALO1_TICKS_PER_SECOND};
        if (type == HALO1_OBJECT_TYPE_BIPED) {
            float scratch[3] = {dv[0] * 2.0f, dv[1] * 2.0f, dv[2] * 2.0f};
            Engine<void(__fastcall*)(uint32_t, float*, bool)>(OFFSET_HALO1_BIPED_APPLY_ACCEL)(datum, scratch, false);
        } else if (type == HALO1_OBJECT_TYPE_VEHICLE) {
            Engine<void(__fastcall*)(uint32_t, float*, int)>(OFFSET_HALO1_VEHICLE_APPLY_ACCEL)(datum, dv, -1);
        } else {
            float* v = &Field<float>(o, HALO1_OBJECT_VELOCITY);
            for (int k = 0; k < 3; ++k) v[k] += dv[k];
            Field<uint32_t>(o, HALO1_OBJECT_FLAGS) &= ~0x20u;
        }
    }

    // Skulls are one byte each (index 0-31), then the engine's cheat switches
    // that sit next to them: bits 32-35 = bump possession, super jump,
    // reflexive damage, medusa (bytes 0x1C421AB-AE).
    constexpr int kSkullBits = 36;
    constexpr uint64_t kSkullUsable =
        (1ull << 1) | (1ull << 2) | (1ull << 3) | (1ull << 4) | (1ull << 5) | (1ull << 6) | (1ull << 7) |
        (1ull << 9) | (1ull << 10) | (1ull << 11) | (1ull << 12) | (1ull << 15) | (1ull << 17) | (1ull << 18) |
        (1ull << 19) | (1ull << 20) | (1ull << 21) | (1ull << 22) | (1ull << 23) | (1ull << 24) | (1ull << 26) |
        (1ull << 27) | (1ull << 28) | (1ull << 29) | (1ull << 30) | (0xFull << 32);

    uint8_t* SkullByte(int bit) {
        if (bit < HALO1_SKULL_BYTES) return (uint8_t*)(g_module + OFFSET_HALO1_SKULLS) + bit;
        return (uint8_t*)(g_module + OFFSET_HALO1_CHEAT_BUMP_POSSESSION) + (bit - 32);
    }

    bool SkullsRead(uint64_t& mask) {
        mask = 0;
        for (int i = 0; i < kSkullBits; ++i)
            if (*SkullByte(i)) mask |= 1ull << i;
        return true;
    }
    void SkullsWrite(uint64_t mask) {
        for (int i = 0; i < kSkullBits; ++i)
            if (kSkullUsable & (1ull << i)) *SkullByte(i) = (mask >> i) & 1;
    }
    uint64_t SkullsMask() { return kSkullUsable; }

    // Launch on hit: an enemy whose health or shield dropped since last tick
    // (or who just died) is pushed upward.
    Tracked g_launchTracked[kMaxTracked];

    void LaunchOnHit(const Players& p, const Objects& objects, float speed, bool on) {
        if (!on) {
            memset(g_launchTracked, 0, sizeof(g_launchTracked));
            return;
        }
        int count = objects.table.Count();
        for (int i = 0; i < count && i < kMaxTracked; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            Tracked& t = g_launchTracked[i];
            uint32_t datum = DatumOf(i, entry);
            bool known = object && t.datum == datum;
            if (!object || type != HALO1_OBJECT_TYPE_BIPED || p.Contains(object) ||
                (!known && (IsAlly(object, p) || !IsAiUnit(object, type)))) {
                t.datum = 0;
                continue;
            }
            float health = Field<float>(object, HALO1_OBJECT_HEALTH);
            float shield = Field<float>(object, HALO1_OBJECT_SHIELD);
            if (!known) {
                t = {datum, health, shield};
                continue;
            }
            bool hit = health < t.health - 1e-4f || shield < t.shield - 1e-4f;
            bool died = health <= 0.0f;
            t.health = health;
            t.shield = shield;
            if (died) t.datum = 0;  // launched once, on the blow that killed it
            if (!hit || (g_timeStopOn && g_frozen.count(datum))) continue;
            const float up[3] = {0.0f, 0.0f, speed};
            AddVelocityTo(objects, datum, up);
        }
    }

    // Double / infinite jump: a jump press while airborne sets the biped's
    // vertical speed to the tag's jump velocity (the Jump slider has already
    // scaled it), keeping horizontal speed. Only once the game's own jump is
    // over (its tick counter above 5), so the press that took off isn't spent.
    int g_airJumpsUsed = 0;
    int g_airTicks = 0;  // game ticks since the feet left the ground
    uint32_t g_jumpPressesSeen = 0;
    bool g_airJumpLogged = false;

    void AirJump(const Players& p, const Games::Settings& s, const Games::FrameInput& in) {
        bool pressed = in.jumpPresses != g_jumpPressesSeen;
        g_jumpPressesSeen = in.jumpPresses;
        if (!s.doubleJump || p.count == 0) { g_airJumpsUsed = 0; g_airTicks = 0; return; }
        char* unit = p.units[0];
        bool airborne = Field<uint32_t>(unit, HALO1_OBJECT_PARENT) == 0xFFFFFFFF && !g_noclipActive &&
                        (Field<uint32_t>(unit, HALO1_BIPED_STATE) & HALO1_BIPED_AIRBORNE);
        if (!airborne) { g_airJumpsUsed = 0; g_airTicks = 0; return; }
        ++g_airTicks;
        if (!pressed) return;
        int limit = s.infiniteJump ? 0x7FFFFFFF : (int)(s.airJumps + 0.5f);
        static int s_logged = 0;
        // The press that took off is still counted at the start of the jump.
        if (g_airTicks < 4 || g_airJumpsUsed >= limit) {
            if (s_logged++ < 12) LOG("Halo CE: air jump press ignored (airborne %d ticks, used %d of %d)", g_airTicks, g_airJumpsUsed, limit);
            return;
        }
        char* tag = Halo1Tags::Get(Field<uint32_t>(unit, HALO1_OBJECT_TAG_DATUM));
        float jump = tag ? Field<float>(tag, HALO1_BIPD_JUMP_VELOCITY) : 0.0f;
        if (!(jump > 0.0f)) { LOG("Halo CE: air jump: no jump velocity in the biped tag"); return; }
        ++g_airJumpsUsed;
        Field<float>(unit, HALO1_OBJECT_VELOCITY + 8) = jump;
        if (!g_airJumpLogged) {
            g_airJumpLogged = true;
            const float* pos = &Field<float>(unit, HALO1_OBJECT_POSITION);
            LOG("Halo CE: air jump at %.2f %.2f %.2f, upward speed %.4f per tick (jump %d)", pos[0], pos[1], pos[2],
                jump, g_airJumpsUsed);
        }
    }

    // The chaos gun and recoil push around one shot: the weapon's triggers'
    // projectile datums are swapped for a random one and put back after the
    // call. The shooter is a player, or the vehicle a player is in.
    using WeaponFireFn = uint64_t(__fastcall*)(uint32_t weapon, int16_t trigger, uint32_t c, uint16_t d);
    WeaponFireFn g_origWeaponFire = nullptr;
    void* g_weaponFireHook = nullptr;

    void BeforeShot(uint32_t weapon, int16_t trigger, Fun::Shot& shot) {
        __try {
            Objects objects;
            Array players;
            if (!ReadTables(g_module, players, objects)) return;
            Players p = FindPlayers(players, objects);
            char* object = objects.Get(weapon);
            uint32_t holder = object ? Field<uint32_t>(object, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
            if (holder == 0xFFFFFFFF) return;

            int shooter = -1;
            bool vehicleGun = false;
            for (int i = 0; i < p.count && shooter < 0; ++i) {
                if (p.datums[i] == holder) { shooter = i; break; }
                uint32_t at = Field<uint32_t>(p.units[i], HALO1_OBJECT_PARENT);
                for (int depth = 0; depth < 3 && at != 0xFFFFFFFF; ++depth) {
                    if (at == holder) { shooter = i; vehicleGun = true; break; }
                    char* up = objects.Get(at);
                    at = up ? Field<uint32_t>(up, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
                }
            }
            if (shooter < 0) return;

            uint32_t* fields[Fun::Shot::kMax];
            int fieldCount = 0;
            char* tag = Halo1Tags::Get(Field<uint32_t>(object, HALO1_OBJECT_TAG_DATUM));
            for (int index : {(int)trigger, trigger == 0 ? 1 : -1}) {
                char* def = index >= 0 ? Halo1Tags::BlockElement(tag, HALO1_WEAP_TRIGGERS, HALO1_WEAP_TRIGGER_SIZE, index)
                                       : nullptr;
                if (def) fields[fieldCount++] = &Field<uint32_t>(def, HALO1_TRIGGER_PROJECTILE + HALO1_TAGREF_DATUM);
            }

            char* control = *(char**)(g_module + OFFSET_HALO1_PLAYER_CONTROL);
            float dir[3] = {};
            if (control) {
                char* look = control + shooter * HALO1_PLAYER_CONTROL_SIZE;
                float yaw = Field<float>(look, HALO1_PLAYER_CONTROL_YAW);
                float pitch = Field<float>(look, HALO1_PLAYER_CONTROL_PITCH);
                dir[0] = cosf(yaw) * cosf(pitch);
                dir[1] = sinf(yaw) * cosf(pitch);
                dir[2] = sinf(pitch);
            }
            // Seated, the whole vehicle moves: the top vehicle above the shooter.
            uint32_t target = p.datums[shooter];
            for (int depth = 0; depth < 5; ++depth) {
                char* at = objects.Get(target);
                uint32_t parent = at ? Field<uint32_t>(at, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
                if (!objects.OfType(parent, HALO1_OBJECT_TYPE_VEHICLE)) break;
                target = parent;
            }
            if ((target == p.datums[shooter] && vehicleGun) || !control) target = 0xFFFFFFFF;
            Fun::BeforeShot(fields, fieldCount, target, dir, shot);
        } __except (Log::Exception(GetExceptionInformation(), "Halo CE shot effects")) {
        }
    }

    // AI can't shoot: weapons not held by a player (or the vehicle a player is
    // in) don't fire. The code patch only stops the AI's normal fire decision;
    // a charged plasma pistol released while the jackal dodges fires through
    // another path, so every shot is stopped here, where all of them end.
    volatile bool g_aiCantShoot = false;

    bool BlockedAiShot(uint32_t weapon) {
        __try {
            Objects objects;
            Array players;
            if (!ReadTables(g_module, players, objects)) return false;
            Players p = FindPlayers(players, objects);
            char* object = objects.Get(weapon);
            uint32_t holder = object ? Field<uint32_t>(object, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
            if (holder == 0xFFFFFFFF) return false;
            for (int i = 0; i < p.count; ++i) {
                if (p.datums[i] == holder) return false;
                uint32_t at = Field<uint32_t>(p.units[i], HALO1_OBJECT_PARENT);
                for (int depth = 0; depth < 4 && at != 0xFFFFFFFF; ++depth) {
                    if (at == holder) return false;
                    char* up = objects.Get(at);
                    at = up ? Field<uint32_t>(up, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
                }
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    uint64_t __fastcall WeaponFireDetour(uint32_t weapon, int16_t trigger, uint32_t c, uint16_t d) {
        if (g_aiCantShoot && BlockedAiShot(weapon)) return 0;
        Fun::Shot shot;
        if (Fun::Active()) BeforeShot(weapon, trigger, shot);
        uint64_t result = g_origWeaponFire(weapon, trigger, c, d);
        Fun::AfterShot(shot);
        return result;
    }

    class FunTitle : public Fun::Title {
    public:
        FunTitle(const Players& p, const Objects& objects) : m_p(p), m_objects(objects) {}
        const char* Name() override { return "Halo CE"; }
        uint32_t Player() override { return m_p.count > 0 ? m_p.datums[0] : 0xFFFFFFFF; }
        uint32_t PlayerVehicle() override {
            if (m_p.count == 0) return 0xFFFFFFFF;
            uint32_t top = 0xFFFFFFFF, at = m_p.datums[0];
            for (int depth = 0; depth < 5; ++depth) {
                char* object = m_objects.Get(at);
                uint32_t parent = object ? Field<uint32_t>(object, HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
                if (!m_objects.OfType(parent, HALO1_OBJECT_TYPE_VEHICLE)) break;
                top = at = parent;
            }
            return top;
        }
        bool View(float* eye, float* dir) override {
            char* control = *(char**)(g_module + OFFSET_HALO1_PLAYER_CONTROL);  // entry 0 = local player 1
            if (!control || m_p.count == 0) return false;
            float yaw = Field<float>(control, HALO1_PLAYER_CONTROL_YAW);
            float pitch = Field<float>(control, HALO1_PLAYER_CONTROL_PITCH);
            dir[0] = cosf(yaw) * cosf(pitch);
            dir[1] = sinf(yaw) * cosf(pitch);
            dir[2] = sinf(pitch);
            Engine<HeadPositionFn>(OFFSET_HALO1_UNIT_HEAD_POSITION)(m_p.datums[0], eye);
            return true;
        }
        bool Ray(const float* from, const float* vec, bool ignorePlayer, float* hit) override {
            alignas(16) static uint8_t result[HALO1_COLLISION_RESULT_SIZE];
            memset(result, 0, sizeof(result));
            uint32_t ignore = ignorePlayer && m_p.count > 0
                                  ? Engine<UltimateParentFn>(OFFSET_HALO1_OBJECT_ULTIMATE_PARENT)(m_p.datums[0])
                                  : 0xFFFFFFFF;
            if (!Engine<CollisionTestFn>(OFFSET_HALO1_COLLISION_TEST_VECTOR)(HALO1_COLLISION_FLAGS_AUTOAIM, from, vec,
                                                                            ignore, result))
                return false;
            memcpy(hit, result + HALO1_COLLISION_RESULT_POINT, 3 * sizeof(float));
            return true;
        }
        bool Position(uint32_t object, float* out) override {
            char* o = m_objects.Get(object);
            if (o) memcpy(out, &Field<float>(o, HALO1_OBJECT_POSITION), 3 * sizeof(float));
            return o != nullptr;
        }
        float Radius(uint32_t object) override {
            char* o = m_objects.Get(object);
            char* tag = o ? Halo1Tags::Get(Field<uint32_t>(o, HALO1_OBJECT_TAG_DATUM)) : nullptr;
            return tag ? Field<float>(tag, HALO1_OBJE_BOUNDING_RADIUS) : 0.0f;
        }
        void Teleport(uint32_t object, const float* position) override {
            Engine<SetPositionFn>(OFFSET_HALO1_OBJECT_SET_POSITION)(object, position, nullptr, nullptr);
            if (char* o = m_objects.Get(object)) memset(&Field<float>(o, HALO1_OBJECT_VELOCITY), 0, 3 * sizeof(float));
            if (g_noclipActive && m_p.count > 0 && object == m_p.datums[0]) memcpy(g_anchor, position, sizeof(g_anchor));
        }
        void AddVelocity(uint32_t object, const float* add) override { AddVelocityTo(m_objects, object, add); }
        uint32_t Create(uint32_t selection, int team, const float* position, float yaw) override {
            return CreateAt(m_objects, selection, team, position, yaw);
        }
        bool InUseByPlayer(uint32_t object) override {
            for (int i = 0; i < m_p.count; ++i) {
                if (Field<uint32_t>(m_p.units[i], HALO1_OBJECT_PARENT) == object) return true;
                for (int w = 0; w < HALO1_UNIT_WEAPON_COUNT; ++w)
                    if (Field<uint32_t>(m_p.units[i], HALO1_UNIT_WEAPONS + w * 4) == object) return true;
            }
            return false;
        }
        void Delete(uint32_t object) override { Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(object); }
        void Message(const char* text) override { SpawnResult(text); }
        bool ReadSkulls(uint64_t& mask) override { return SkullsRead(mask); }
        void WriteSkulls(uint64_t mask) override { SkullsWrite(mask); }
        uint64_t SkullMask() override { return SkullsMask(); }

    private:
        const Players& m_p;
        const Objects& m_objects;
    };

    void RunFun(const Players& p, const Objects& objects, const Games::Settings& s) {
        FunTitle fun(p, objects);
        Fun::Tick(fun, s, TickSeconds(g_lastFunTick));
    }

    // Your team. The AI does not read the player's team each tick: it copies it
    // into each perception prop once, when the prop is made, with the hostile
    // flags it worked out then, so writing the player's team only moved the
    // reticle colour. Instead the player stays on team 1 and the game's own
    // allegiance calls (which refresh props that already exist) are used:
    //   Covenant: the player/human allegiance is removed (marines turn hostile)
    //             and player/Covenant is added (Covenant turn friendly).
    //   Rogue:    only the player/human allegiance is removed; every other
    //             team was already hostile.
    // Level scripts can rewrite allegiances, so the matrix is checked each tick.
    using AllegianceFn = void(__fastcall*)(int16_t a, int16_t b);
    int g_teamApplied = 0;
    bool g_teamAdded[8] = {};  // alliances with the player this feature added (team -> true)

    void AllegianceAdd(int a, int b) { Engine<AllegianceFn>(OFFSET_HALO1_ALLEGIANCE_ADD)((int16_t)a, (int16_t)b); }
    void AllegianceRemove(int a, int b) { Engine<AllegianceFn>(OFFSET_HALO1_ALLEGIANCE_REMOVE)((int16_t)a, (int16_t)b); }

    void SetPlayerTeam(int choice) {
        choice = choice < 0 || choice > 3 ? 0 : choice;
        auto friends = [&](int a, int b) { return TeamsFriends(g_module, a, b); };
        // The team (besides the humans) the player is allied to: Covenant, or Flood.
        int ally = choice == 1 ? HALO1_TEAM_COVENANT : choice == 3 ? HALO1_TEAM_FLOOD : 0;
        if (choice != g_teamApplied) {
            LOG("Halo CE: your team %d -> %d", g_teamApplied, choice);
            if (choice == 0 && !friends(HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN))
                AllegianceAdd(HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN);
            g_teamApplied = choice;
        }
        if (choice == 0) {
            // Back to normal: drop whichever alliance we added.
            for (int t : {HALO1_TEAM_COVENANT, HALO1_TEAM_FLOOD})
                if (friends(HALO1_TEAM_PLAYER, t) && g_teamAdded[t]) { AllegianceRemove(HALO1_TEAM_PLAYER, t); g_teamAdded[t] = false; }
            return;
        }
        if (friends(HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN)) AllegianceRemove(HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN);
        for (int t : {HALO1_TEAM_COVENANT, HALO1_TEAM_FLOOD}) {
            bool want = t == ally;
            if (want && !friends(HALO1_TEAM_PLAYER, t)) { AllegianceAdd(HALO1_TEAM_PLAYER, t); g_teamAdded[t] = true; }
            if (!want && friends(HALO1_TEAM_PLAYER, t)) { AllegianceRemove(HALO1_TEAM_PLAYER, t); g_teamAdded[t] = false; }
        }
    }

    // Time stop: every AI unit, and the vehicle it rides (unless a player is
    // in that), is skipped by the per-object update, so nothing about it
    // moves, animates or fires. Clearing the AI-update bit stops the actors
    // thinking too. The set is rebuilt each tick on the game thread, which is
    // also where the hook runs.
    using ObjectUpdateFn = bool(__fastcall*)(uint32_t datum);
    ObjectUpdateFn g_origObjectUpdate = nullptr;
    void* g_objectUpdateHook = nullptr;
    bool g_aiUpdateCleared = false;

    bool __fastcall ObjectUpdateDetour(uint32_t datum) {
        if (g_timeStopOn && g_frozen.count(datum)) return true;
        return g_origObjectUpdate(datum);
    }

    void TimeStop(const Players& p, const Objects& objects, bool on) {
        uint8_t* ai = *(uint8_t**)(g_module + OFFSET_HALO1_AI_GLOBALS);
        if (!on) {
            if (g_timeStopOn) LOG("Halo CE: time resumed");
            g_timeStopOn = false;
            g_frozen.clear();
            if (g_aiUpdateCleared && ai) *ai |= HALO1_AI_GLOBALS_UPDATE;
            g_aiUpdateCleared = false;
            return;
        }
        uint32_t myParent = p.count ? Field<uint32_t>(p.units[0], HALO1_OBJECT_PARENT) : 0xFFFFFFFF;
        g_frozen.clear();
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsAiUnit(object, type) || p.Contains(object)) continue;
            g_frozen.insert(DatumOf(i, entry));
            uint32_t parent = Field<uint32_t>(object, HALO1_OBJECT_PARENT);
            if (parent != 0xFFFFFFFF && parent != myParent && objects.OfType(parent, HALO1_OBJECT_TYPE_VEHICLE))
                g_frozen.insert(parent);
        }
        g_timeStopOn = true;
        if (ai && (*ai & HALO1_AI_GLOBALS_UPDATE)) {
            *ai &= ~HALO1_AI_GLOBALS_UPDATE;
            g_aiUpdateCleared = true;
        }
    }

    // Giant / tiny AI: every AI unit (and its weapons, which don't follow the
    // unit's scale by themselves) is kept at the size set for its side. The
    // engine's object_set_scale is called when a unit's scale differs; only
    // units it changed are put back when the sliders return to 1.
    std::unordered_map<uint32_t, float> g_scaledAi;  // unit datum -> the scale this set

    void ScaleAi(const Players& p, const Objects& objects, float enemy, float friendly) {
        auto setScale = [&](uint32_t datum, float scale) {
            Engine<SetScaleFn>(OFFSET_HALO1_OBJECT_SET_SCALE)(datum, scale, 0);
            char* unit = objects.Get(datum);
            for (int w = 0; unit && w < HALO1_UNIT_WEAPON_COUNT; ++w) {
                uint32_t weapon = Field<uint32_t>(unit, HALO1_UNIT_WEAPONS + w * 4);
                if (objects.Get(weapon)) Engine<SetScaleFn>(OFFSET_HALO1_OBJECT_SET_SCALE)(weapon, scale, 0);
            }
        };
        bool wanted = enemy != 1.0f || friendly != 1.0f;
        if (!wanted && g_scaledAi.empty()) return;
        for (auto it = g_scaledAi.begin(); it != g_scaledAi.end();) {
            if (!objects.Get(it->first)) { it = g_scaledAi.erase(it); continue; }
            if (!wanted) { setScale(it->first, 1.0f); it = g_scaledAi.erase(it); continue; }
            ++it;
        }
        if (!wanted) return;
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* unit = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!unit || !IsAiUnit(unit, type) || p.Contains(unit)) continue;
            uint32_t datum = DatumOf(i, entry);
            float scale = IsAlly(unit, p) ? friendly : enemy;
            float actual = Field<float>(unit, HALO1_OBJECT_SCALE);
            if (actual <= 0.0f) actual = 1.0f;  // 0 = unscaled
            auto known = g_scaledAi.find(datum);
            if (known == g_scaledAi.end()) {
                if (fabsf(scale - 1.0f) < 0.001f) continue;
            } else if (fabsf(actual - scale) < 0.001f) {
                continue;
            }
            setScale(datum, scale);
            g_scaledAi[datum] = scale;
        }
    }

    // Squads: every member is made around the aim point, a loose cluster (a
    // sunflower spiral, so any size fits), through the same Create as a
    // single spawn. CE keeps every tag loaded, so nothing waits.
    void SpawnSquad(const Players& p, const Objects& objects, int index, int team) {
        if (index < 0 || index >= Games::kSquadCount) return;
        const Games::Squad& squad = Games::kSquads[index];
        if (p.count == 0) { SpawnResult("No player to spawn for"); return; }
        std::vector<Games::TagChoice> characters;
        for (auto& variant : Halo1Tags::ListGroup(HALO1_TAG_ACTV)) {
            char* tag = Halo1Tags::Get(variant.datum);
            if (tag && Field<uint32_t>(tag, HALO1_ACTV_UNIT) != 0xFFFFFFFF) characters.push_back(variant);
        }
        std::vector<uint32_t> tags = Games::SquadTags(squad, characters);
        if (tags.empty()) { SpawnResult("This squad isn't in this map"); return; }

        char* control = *(char**)(g_module + OFFSET_HALO1_PLAYER_CONTROL);
        if (!control) { SpawnResult("No player to spawn for"); return; }
        float yaw = Field<float>(control, HALO1_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO1_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_HALO1_UNIT_HEAD_POSITION)(p.datums[0], eye);
        float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};
        alignas(16) static uint8_t result[HALO1_COLLISION_RESULT_SIZE];
        memset(result, 0, sizeof(result));
        uint32_t ignore = Engine<UltimateParentFn>(OFFSET_HALO1_OBJECT_ULTIMATE_PARENT)(p.datums[0]);
        if (!Engine<CollisionTestFn>(OFFSET_HALO1_COLLISION_TEST_VECTOR)(HALO1_COLLISION_FLAGS_AUTOAIM, eye, ray,
                                                                         ignore, result)) {
            SpawnResult("Nothing in range to spawn on");
            return;
        }
        const float* at = (const float*)(result + HALO1_COLLISION_RESULT_POINT);
        float center[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                           at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        float facing = remainderf(yaw + kPi, 2.0f * kPi);
        if (facing < 0.0f) facing += 2.0f * kPi;  // CE keeps yaw in 0..2pi
        float spacing = squad.large ? 1.5f : 0.9f;  // world units between neighbours

        int made = 0;
        for (size_t i = 0; i < tags.size(); ++i) {
            float radius = spacing * sqrtf((float)i);
            float angle = (float)i * 2.39996323f;  // the golden angle
            float position[3] = {center[0] + cosf(angle) * radius, center[1] + sinf(angle) * radius, center[2]};
            if (CreateAt(objects, tags[i], team, position, facing) != 0xFFFFFFFF) ++made;
        }
        LOG("Halo CE: squad %s at %.2f %.2f %.2f: %d of %zu made", squad.name, center[0], center[1], center[2], made,
            tags.size());
        char text[96];
        sprintf_s(text, made == (int)tags.size() ? "Spawned %s" : "Spawned %s (some couldn't be made here)", squad.name);
        SpawnResult(text);
    }

    // --- Upgrade AI and Random AI swap -----------------------------------------
    // Both replace an AI by a fresh one: the new actor is made where the old
    // one stands, moved into its encounter and squad (so mission scripts that
    // count the encounter still see it), given its team, and the old one is
    // erased. Seated AI wait until they're out.

    std::vector<Games::TagChoice> g_chars;  // actor variants with a unit; rebuilt after a map change
    bool g_charsBuilt = false;
    std::unordered_map<uint32_t, uint32_t> g_upgradeTo;  // variant -> next rank (allies flagged below)
    std::unordered_set<uint32_t> g_allyRanks;            // variants whose upgrade needs "Allies too"
    std::unordered_set<uint32_t> g_upgradeSeen, g_swapSeen;

    const std::vector<Games::TagChoice>& Characters() {
        if (g_charsBuilt) return g_chars;
        g_chars.clear();
        for (auto& variant : Halo1Tags::ListGroup(HALO1_TAG_ACTV)) {
            char* tag = Halo1Tags::Get(variant.datum);
            if (tag && Field<uint32_t>(tag, HALO1_ACTV_UNIT) != 0xFFFFFFFF) g_chars.push_back(variant);
        }
        g_upgradeTo.clear();
        g_allyRanks.clear();
        struct Ladder { std::vector<const char*> ranks; bool ally; };
        const Ladder ladders[] = {
            {{"grunt minor plasma pistol", "grunt major plasma pistol", "grunt specops needler"}, false},
            {{"grunt minor needler", "grunt major needler", "grunt specops needler"}, false},
            {{"jackal minor plasma pistol", "jackal major plasma pistol"}, false},
            {{"elite minor plasma rifle", "elite major plasma rifle", "elite specops plasma rifle",
              "elite commander plasma rifle", "elite commander energy sword"}, false},
            {{"elite minor needler", "elite major needler", "elite specops needler"}, false},
            {{"stealth elite plasma rifle", "stealth elite major plasma rifle", "stealth elite major energy sword"}, false},
            {{"hunter", "hunter major"}, false},
            {{"sentinel", "sentinel major"}, false},
            {{"sentinel_shielded", "sentinel_shielded major"}, false},
            {{"marine_armored assault rifle", "marine_armored assault rifle major"}, true},
            {{"marine assault rifle", "marine assault rifle major"}, true},
            {{"marine_armored sniper rifle", "marine_armored sniper rifle major"}, true},
        };
        for (const Ladder& ladder : ladders) {
            uint32_t previous = 0xFFFFFFFF;
            for (const char* name : ladder.ranks) {
                uint32_t datum = 0xFFFFFFFF;
                for (auto& c : g_chars)
                    if (c.name == name) { datum = c.datum; break; }
                if (datum == 0xFFFFFFFF) continue;  // a rank this map lacks is skipped
                if (previous != 0xFFFFFFFF && !g_upgradeTo.count(previous)) {
                    g_upgradeTo[previous] = datum;
                    if (ladder.ally) g_allyRanks.insert(previous);
                }
                previous = datum;
            }
        }
        g_charsBuilt = true;
        return g_chars;
    }

    // Replaces the AI behind `actorDatum` by a fresh `variant`. Returns the new
    // actor datum, or -1.
    uint32_t ReplaceActor(const Array& actors, const Objects& objects, uint32_t actorDatum, uint32_t variant) {
        char* actor = actors.Get(actorDatum);
        uint32_t unitDatum = actor ? Field<uint32_t>(actor, HALO1_ACTOR_UNIT) : 0xFFFFFFFF;
        char* unit = objects.Get(unitDatum);
        if (!unit) return 0xFFFFFFFF;
        float position[3];
        memcpy(position, &Field<float>(unit, HALO1_OBJECT_POSITION), sizeof(position));
        position[2] += kSpawnLift;
        const float* aim = &Field<float>(unit, HALO1_UNIT_AIM_VECTOR);
        float yaw = atan2f(aim[1], aim[0]);
        if (yaw < 0.0f) yaw += 2.0f * kPi;  // CE keeps yaw in 0..2pi
        int16_t team = Field<int16_t>(unit, HALO1_OBJECT_TEAM);
        uint32_t encounter = Field<uint32_t>(actor, HALO1_ACTOR_ENCOUNTER);
        int16_t squad = Field<int16_t>(actor, HALO1_ACTOR_SQUAD);

        uint32_t fresh = SpawnActor(variant, position, yaw, 0);
        if (fresh == 0xFFFFFFFF) return fresh;
        if (encounter != 0xFFFFFFFF) Engine<ActorMigrateFn>(OFFSET_HALO1_ACTOR_MIGRATE)(fresh, encounter, (uint16_t)squad);
        SetActorTeam(fresh, team);  // attaching may have changed it
        Engine<ActorEraseFn>(OFFSET_HALO1_ACTOR_ERASE)(actorDatum, false);
        // An AI the mod spawned stays followed (respawn), as its new self.
        for (auto& ai : g_spawnedAi)
            if (ai.unit == unitDatum) {
                int aiTeam = ai.team;
                ai = {};
                Follow(objects, variant, aiTeam, position, fresh);
                break;
            }
        return fresh;
    }

    // Walks the live AI (not swarm actors, not players, not the dead and not
    // seated) and replaces those `choose` returns a variant for. `all` is the
    // button (everyone, again); otherwise each AI once. Returns how many
    // were replaced.
    template <typename Choose>
    int ReplaceAi(const Players& p, const Objects& objects, float range, bool all, std::unordered_set<uint32_t>& seen,
                  int* seated, Choose choose) {
        Array actors{*(char**)(g_module + OFFSET_HALO1_ACTORS)};
        if (!actors.header) return 0;
        struct Pick { uint32_t actor, variant; };
        std::vector<Pick> picks;  // collected first: replacing adds actors
        int count = actors.Count();
        for (int i = 0; i < count; ++i) {
            char* actor = actors.Entry(i);
            if (!*(uint16_t*)actor) continue;
            uint32_t datum = DatumOf(i, actor);
            if (!all && seen.count(datum)) continue;
            if (Field<uint16_t>(actor, HALO1_ACTOR_FLAGS) & HALO1_ACTOR_FLAG_SWARM) continue;
            char* unit = objects.Get(Field<uint32_t>(actor, HALO1_ACTOR_UNIT));
            if (!unit || p.Contains(unit) || (Field<uint16_t>(unit, HALO1_OBJECT_DAMAGE_FLAGS) & HALO1_DAMAGE_FLAG_DEAD))
                continue;
            if (!InRange(p, unit, range)) continue;
            uint32_t variant = choose(Field<uint32_t>(actor, HALO1_ACTOR_VARIANT), unit);
            if (variant == 0xFFFFFFFF) { seen.insert(datum); continue; }
            if (Field<uint32_t>(unit, HALO1_OBJECT_PARENT) != 0xFFFFFFFF) { if (seated) ++*seated; continue; }
            picks.push_back({datum, variant});
        }
        int done = 0;
        for (const Pick& pick : picks) {
            uint32_t fresh = ReplaceActor(actors, objects, pick.actor, pick.variant);
            seen.insert(pick.actor);
            if (fresh != 0xFFFFFFFF) { seen.insert(fresh); ++done; }
        }
        return done;
    }

    void UpgradeAi(const Players& p, const Objects& objects, const Games::Settings& s, bool all) {
        Characters();
        if (g_upgradeTo.empty()) {
            if (all) SpawnResult("No AI to upgrade in this map");
            return;
        }
        int seated = 0;
        int done = ReplaceAi(p, objects, s.upgradeAiRange, all, g_upgradeSeen, &seated,
                             [&](uint32_t variant, char* unit) -> uint32_t {
                                 auto to = g_upgradeTo.find(variant);
                                 if (to == g_upgradeTo.end() || (g_allyRanks.count(variant) && !s.upgradeAiAllies))
                                     return 0xFFFFFFFF;
                                 return to->second;
                             });
        if (all || done) LOG("Halo CE: upgrade AI (%s): %d upgraded, %d seated skipped", all ? "button" : "auto", done, seated);
        if (!all) return;
        char text[96];
        if (done) sprintf_s(text, "Upgraded %d AI", done);
        else sprintf_s(text, seated ? "No AI to upgrade (%d in vehicles)" : "No AI to upgrade", seated);
        SpawnResult(text);
    }

    // Names that never make a random pick: crews, turret gunners, the monitor,
    // Flood infection forms (swarm actors).
    bool SwapPoolName(const std::string& name) {
        for (const char* bad : {"pilot", "gunner", "monitor", "flood_infection", "captain", "crewman"})
            if (name.find(bad) != std::string::npos) return false;
        return true;
    }

    void RandomSwap(const Players& p, const Objects& objects, const Games::Settings& s, bool all) {
        std::vector<uint32_t> pool;
        for (auto& c : Characters())
            if (SwapPoolName(c.name)) pool.push_back(c.datum);
        if (pool.size() < 2) {
            if (all) SpawnResult("No AI to swap in this map");
            return;
        }
        int seated = 0;
        int done = ReplaceAi(p, objects, 0.0f, all, g_swapSeen, &seated, [&](uint32_t variant, char* unit) -> uint32_t {
            std::string name;
            for (auto& c : Characters())
                if (c.datum == variant) name = c.name;
            if (!SwapPoolName(name) || (!s.randomSwapAllies && IsAlly(unit, p))) return 0xFFFFFFFF;
            uint32_t pick = variant;
            for (int tries = 0; tries < 8 && pick == variant; ++tries)
                pick = pool[(size_t)(Random01() * pool.size()) % pool.size()];
            return pick == variant ? 0xFFFFFFFF : pick;
        });
        if (all || done) LOG("Halo CE: random swap (%s): %d swapped, %d seated skipped, pool %zu", all ? "button" : "auto", done, seated, pool.size());
        if (!all) return;
        char text[96];
        if (done) sprintf_s(text, "Swapped %d AI", done);
        else sprintf_s(text, seated ? "No AI to swap (%d in vehicles)" : "No AI to swap", seated);
        SpawnResult(text);
    }

    // Kills an AI unit the way unit_kill does (a damage flag the unit update
    // handles), after making it mortal. False if it was already dead.
    bool KillUnit(char* unit) {
        auto& flags = Field<uint16_t>(unit, HALO1_OBJECT_DAMAGE_FLAGS);
        if (flags & HALO1_DAMAGE_FLAG_DEAD) return false;
        flags = (uint16_t)((flags & ~HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE) | HALO1_DAMAGE_FLAG_KILL);
        Field<float>(unit, HALO1_OBJECT_SHIELD) = 0.0f;
        return true;
    }

    // Nuke: kills every AI. Players, and with `spareAllies` the player's
    // side, are left alone.
    void Nuke(const Players& p, const Objects& objects, bool spareAllies) {
        int killed = 0;
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsAiUnit(object, type) || p.Contains(object)) continue;
            if (spareAllies && IsAlly(object, p)) continue;
            if (KillUnit(object)) ++killed;
        }
        char text[96];
        sprintf_s(text, killed ? "Nuked %d AI" : "No AI to nuke", killed);
        SpawnResult(text);
    }

    // Poison touch: any AI on foot within reach of player 1 dies, friend or
    // foe. Touching = within kPoisonRange horizontally and about a body height
    // vertically.
    void PoisonTouch(const Players& p, const Objects& objects) {
        constexpr float kPoisonRange = 1.0f;   // world units (a unit is ~0.6 wide)
        constexpr float kPoisonHeight = 1.5f;
        if (p.count == 0) return;
        const float* me = &Field<float>(p.units[0], HALO1_OBJECT_POSITION);
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsAiUnit(object, type) || p.Contains(object)) continue;
            const float* pos = &Field<float>(object, HALO1_OBJECT_POSITION);
            float dx = pos[0] - me[0], dy = pos[1] - me[1], dz = pos[2] - me[2];
            if (dx * dx + dy * dy > kPoisonRange * kPoisonRange || fabsf(dz) > kPoisonHeight) continue;
            KillUnit(object);
        }
    }

    // Random teams: each AI is put on one of a few teams. The humans (your
    // side) are one faction; the Covenant, Flood and Sentinels fight each
    // other and the humans as the game's allegiances say. `all` is the
    // button (every AI in range, again); otherwise each AI once, as it shows
    // up.
    std::unordered_set<uint32_t> g_teamsSeen;
    const int kFactionTeams[] = {HALO1_TEAM_HUMAN, HALO1_TEAM_COVENANT, HALO1_TEAM_FLOOD, HALO1_TEAM_SENTINEL,
                                6 /* rogue: allied with nobody */};

    void RandomTeams(const Players& p, const Objects& objects, const Games::Settings& s, bool all) {
        int factions = (int)(s.randomTeamsFactions + 0.5f);
        int most = (int)_countof(kFactionTeams);
        factions = factions < 2 ? 2 : (factions > most ? most : factions);
        int changed = 0;
        int count = objects.table.Count();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsAiUnit(object, type) || p.Contains(object)) continue;
            uint32_t datum = DatumOf(i, entry);
            if (!all && g_teamsSeen.count(datum)) continue;
            if (Field<uint16_t>(object, HALO1_OBJECT_DAMAGE_FLAGS) & HALO1_DAMAGE_FLAG_DEAD) continue;
            if (!InRange(p, object, s.randomTeamsRange)) continue;
            g_teamsSeen.insert(datum);
            uint32_t actor = Field<uint32_t>(object, HALO1_UNIT_ACTOR);
            if (actor == 0xFFFFFFFF) actor = Field<uint32_t>(object, HALO1_UNIT_SWARM_ACTOR);
            int team = kFactionTeams[(int)(Random01() * factions) % factions];
            SetActorTeam(actor, (int16_t)team);
            ++changed;
        }
        if (all || changed) LOG("Halo CE: random AI teams (%s): %d AI over %d teams", all ? "button" : "auto", changed, factions);
        if (all) {
            char text[96];
            sprintf_s(text, changed ? "Shuffled %d AI over %d teams" : "No AI in range", changed, factions);
            SpawnResult(text);
        }
    }

    // Despawn all custom AI: every AI the mod spawned, alive or dead, is deleted.
    void DespawnCustomAi(const Objects& objects) {
        int removed = 0;
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            if (char* unit = objects.Get(ai.unit)) {
                uint32_t actor = Field<uint32_t>(unit, HALO1_UNIT_ACTOR);
                if (actor != 0xFFFFFFFF) Engine<ActorEraseFn>(OFFSET_HALO1_ACTOR_ERASE)(actor, false);
                else Engine<ObjectDeleteFn>(OFFSET_HALO1_OBJECT_DELETE)(ai.unit);
                ++removed;
            }
            ai = {};
        }
        char text[96];
        sprintf_s(text, removed ? "Despawned %d custom AI" : "No custom AI to despawn", removed);
        SpawnResult(text);
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
        } __except (Log::Exception(GetExceptionInformation(), "Halo CE main-thread players")) {
            return false;
        }
    }
}

namespace Halo1Cheats {
    void SetMainThreadModule(HMODULE halo1) {
        g_module = (char*)halo1;
        g_allegianceModule = (char*)halo1;
    }

    // Unload, or a new map: object and actor datums are per map, so what was
    // followed or handled is forgotten.
    void ResetMainThread() {
        g_noclipActive = false;
        g_noclipUnit = 0xFFFFFFFF;
        g_lastTick = {};
        g_give = {};
        memset(g_tracked, 0, sizeof(g_tracked));
        for (auto& ai : g_spawnedAi) ai = {};
        g_teamsSeen.clear();
        g_upgradeSeen.clear();
        g_swapSeen.clear();
        g_charsBuilt = false;
        memset(g_launchTracked, 0, sizeof(g_launchTracked));
        g_frozen.clear();
        g_timeStopOn = false;
        g_aiUpdateCleared = false;  // the AI globals went with the map
        g_scaledAi.clear();
        g_teamApplied = 0;  // allegiances reset with a new map
        memset(g_teamAdded, 0, sizeof(g_teamAdded));
        g_lastFunTick = {};
        Fun::ResetState();
    }

    // Lock checkpoints: the game's own saves go through all their steps (script
    // waits, the "checkpoint" message, state flags) but the snapshot copy that
    // makes them real is skipped, so a revert still lands on the last
    // checkpoint from before the lock. (Vetoing the save request itself left
    // the game waiting on a save that never finished and it reverted by itself.)
    // The mod's Save button opens a short window that lets one through.
    volatile bool g_lockCheckpoints = false;
    volatile ULONGLONG g_ownSaveUntil = 0;
    using SaveSnapshotFn = void(__fastcall*)(void* object);
    SaveSnapshotFn g_origSaveSnapshot = nullptr;
    void* g_saveSnapshotHook = nullptr;

    // The "checkpoint" / "saving" HUD message (bool saving): hidden while locked, so
    // it doesn't claim a checkpoint that wasn't made.
    using SaveMessageFn = void(__fastcall*)(uint8_t saving);
    SaveMessageFn g_origSaveMessage = nullptr;
    void* g_saveMessageHook = nullptr;

    void __fastcall SaveMessageDetour(uint8_t saving) {
        if (g_lockCheckpoints && GetTickCount64() >= g_ownSaveUntil) return;
        g_origSaveMessage(saving);
    }

    void __fastcall SaveSnapshotDetour(void* object) {
        if (g_lockCheckpoints && GetTickCount64() >= g_ownSaveUntil) {
            static int s_logged = 0;
            if (s_logged++ < 5) LOG("Halo CE: checkpoint save skipped (locked)");
            return;
        }
        g_origSaveSnapshot(object);
    }

    // Hook on the per-object update (time stop); installed with the module.
    void InstallObjectHook() {
        void* target = g_module + OFFSET_HALO1_OBJECT_UPDATE;
        if (MH_CreateHook(target, (void*)&ObjectUpdateDetour, (void**)&g_origObjectUpdate) == MH_OK &&
            MH_EnableHook(target) == MH_OK) {
            g_objectUpdateHook = target;
        } else {
            MH_RemoveHook(target);
            LOG("Halo CE: failed to hook the object update at %p; time stop unavailable", target);
        }
        {
            void* target = g_module + OFFSET_HALO1_WEAPON_TRIGGER_FIRE;
            if (MH_CreateHook(target, (void*)&WeaponFireDetour, (void**)&g_origWeaponFire) == MH_OK &&
                MH_EnableHook(target) == MH_OK) {
                g_weaponFireHook = target;
            } else {
                MH_RemoveHook(target);
                LOG("Halo CE: failed to hook weapon fire at %p; chaos gun and recoil push unavailable", target);
            }
        }
        {
            void* target = g_module + OFFSET_HALO1_SAVE_MESSAGE;
            if (MH_CreateHook(target, (void*)&SaveMessageDetour, (void**)&g_origSaveMessage) == MH_OK &&
                MH_EnableHook(target) == MH_OK) {
                g_saveMessageHook = target;
            } else {
                MH_RemoveHook(target);
                LOG("Halo CE: failed to hook the checkpoint message at %p; it will still show while locked", target);
            }
        }
        {
            void* target = g_module + OFFSET_HALO1_SAVE_SNAPSHOT;
            if (MH_CreateHook(target, (void*)&SaveSnapshotDetour, (void**)&g_origSaveSnapshot) == MH_OK &&
                MH_EnableHook(target) == MH_OK) {
                g_saveSnapshotHook = target;
            } else {
                MH_RemoveHook(target);
                LOG("Halo CE: failed to hook the checkpoint snapshot at %p; lock checkpoints unavailable", target);
            }
        }
    }

    void RemoveObjectHook() {
        g_timeStopOn = false;
        if (!g_objectUpdateHook) return;
        MH_DisableHook(g_objectUpdateHook);
        MH_RemoveHook(g_objectUpdateHook);
        g_objectUpdateHook = nullptr;
        for (void** hook : {&g_saveSnapshotHook, &g_saveMessageHook, &g_weaponFireHook}) {
            if (!*hook) continue;
            MH_DisableHook(*hook);
            MH_RemoveHook(*hook);
            *hook = nullptr;
        }
        g_lockCheckpoints = false;
    }

    void SpawnStatus(Games::Status& status) {
        std::lock_guard<std::mutex> lock(g_spawnLock);
        status.spawnDone = g_spawnDone;
        sprintf_s(status.spawnMessage, "%s", g_spawnMessage);
    }
}

void Halo1Cheats::MainThread(const Games::Settings& s, const Games::FrameInput& in) {
    if (!g_module) return;
    static bool s_first = true;  // don't run a "Give" pressed before this session
    if (s_first) {
        g_giveSerial = s.giveWeaponSerial;
        g_saveSerial = s.saveCheckpointSerial;
        g_loadSerial = s.loadCheckpointSerial;
        g_spawnSerial = s.spawnSerial;
        g_nukeSerial = s.nukeSerial;
        g_despawnSerial = s.despawnSerial;
        g_randomTeamsSerial = s.randomTeamsSerial;
        g_upgradeAiSerial = s.upgradeAiSerial;
        g_randomSwapSerial = s.randomSwapSerial;
        s_first = false;
    }
    if (s.saveCheckpointSerial != g_saveSerial) {
        g_saveSerial = s.saveCheckpointSerial;
        LOG("Halo CE: saving a checkpoint");
        g_ownSaveUntil = GetTickCount64() + 3000;
        Guard("Halo CE save checkpoint", [&] { *(uint16_t*)(g_module + OFFSET_HALO1_GAME_SAVE_NOW_FLAG) = 1; });
    }
    if (s.loadCheckpointSerial != g_loadSerial) {
        g_loadSerial = s.loadCheckpointSerial;
        LOG("Halo CE: loading the last checkpoint");
        Guard("Halo CE load checkpoint", [&] { Engine<void(__fastcall*)()>(OFFSET_HALO1_GAME_REVERT)(); });
    }

    g_lockCheckpoints = s.lockCheckpoints;
    g_aiCantShoot = s.aiCantShoot;
    Objects objects;
    Players p;
    if (!ReadPlayers(objects, p)) return;

    static char* s_scenario = nullptr;
    char* scenario = nullptr;
    Guard("Halo CE scenario", [&] { scenario = *(char**)(g_module + OFFSET_HALO1_SCENARIO); });
    if (scenario != s_scenario) {
        s_scenario = scenario;
        ResetMainThread();
    }

    float dt = TickSeconds(g_lastTick);
    Guard("Halo CE noclip", [&] { Noclip(p, s.noclip, in, dt); });
    Guard("Halo CE damage multiplier", [&] { Damage(p, objects, s.damage); });
    Guard("Halo CE aimbot strength", [&] {
        if (s.aimbot && in.firing) AimPull(p, objects, s.aimbotStrength);
    });
    Guard("Halo CE rapid fire", [&] { RapidFire(p, objects, s.rapidFire, s.vehicleRapidFire, in.firing); });
    if (s.spawnSerial != g_spawnSerial) {
        g_spawnSerial = s.spawnSerial;
        if (s.spawnEnabled && s.spawnSquad >= 0)
            Guard("Halo CE spawn squad", [&] { SpawnSquad(p, objects, s.spawnSquad, s.spawnTeam); });
        else if (s.spawnEnabled && s.spawnTag != 0xFFFFFFFF)
            Guard("Halo CE spawn", [&] { Spawn(p, objects, s.spawnTag, s.spawnTeam); });
    }
    Guard("Halo CE spawned AI", [&] { FollowSpawnedAi(p, objects, s.spawnRespawn); });
    if (s.nukeSerial != g_nukeSerial) {
        g_nukeSerial = s.nukeSerial;
        Guard("Halo CE nuke", [&] { Nuke(p, objects, s.nukeSpareAllies); });
    }
    if (s.despawnSerial != g_despawnSerial) {
        g_despawnSerial = s.despawnSerial;
        Guard("Halo CE despawn custom AI", [&] { DespawnCustomAi(objects); });
    }
    Guard("Halo CE time stop", [&] { TimeStop(p, objects, s.timeStop); });
    Guard("Halo CE AI size", [&] { ScaleAi(p, objects, s.aiScaleEnemy, s.aiScaleFriendly); });
    Guard("Halo CE your team", [&] { SetPlayerTeam(s.playerTeam); });
    // Disable friendly fire / allies never turn against you also make peace: if the
    // marines already turned on you (the betrayal broke the player/human
    // allegiance), re-adding it through the game's own call makes them friends
    // again, props they already hold included. At most once a second.
    Guard("Halo CE restore allies", [&] {
        static ULONGLONG s_last = 0;
        if (!(s.noFriendlyFire || s.noBetrayal) || s.playerTeam != 0) return;
        if (TeamsFriends(g_module, HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN)) return;
        ULONGLONG now = GetTickCount64();
        if (now - s_last < 1000) return;
        s_last = now;
        LOG("Halo CE: the humans were hostile to the player; restoring the allegiance");
        AllegianceAdd(HALO1_TEAM_PLAYER, HALO1_TEAM_HUMAN);
    });
    Guard("Halo CE fun", [&] { RunFun(p, objects, s); });
    Guard("Halo CE launch on hit", [&] { LaunchOnHit(p, objects, s.launchStrength, s.launchOnHit); });
    Guard("Halo CE air jump", [&] { AirJump(p, s, in); });
    Guard("Halo CE poison touch", [&] { if (s.poisonTouch) PoisonTouch(p, objects); });
    if (s.upgradeAiSerial != g_upgradeAiSerial) {
        g_upgradeAiSerial = s.upgradeAiSerial;
        Guard("Halo CE upgrade AI", [&] { UpgradeAi(p, objects, s, true); });
    } else if (s.upgradeAiAuto) {
        Guard("Halo CE upgrade AI", [&] { UpgradeAi(p, objects, s, false); });
    }
    if (s.randomSwapSerial != g_randomSwapSerial) {
        g_randomSwapSerial = s.randomSwapSerial;
        Guard("Halo CE random swap", [&] { RandomSwap(p, objects, s, true); });
    } else if (s.randomSwapAuto) {
        Guard("Halo CE random swap", [&] { RandomSwap(p, objects, s, false); });
    }
    if (s.randomTeamsSerial != g_randomTeamsSerial) {
        g_randomTeamsSerial = s.randomTeamsSerial;
        Guard("Halo CE random AI teams", [&] { RandomTeams(p, objects, s, true); });
    } else if (s.randomTeamsAuto) {
        Guard("Halo CE random AI teams", [&] { RandomTeams(p, objects, s, false); });
    }
    bool request = s.giveWeaponSerial != g_giveSerial;
    if (request) {
        g_giveSerial = s.giveWeaponSerial;
        LOG("Halo CE: give weapon request %08X (%d player units)", s.giveWeapon, p.count);
    }
    __try {
        if (request) GiveWeapon(p, objects, s.giveWeapon);
        ContinueGive(p);
    } __except (Log::Exception(GetExceptionInformation(), "Halo CE give weapon")) {
        g_give = {};
    }
}
