// Halo 4: launch on hit, giant/tiny AI, double/infinite jump, poison touch (the "fun_extras" work item; see NOTES.md).
// Halo4Fun::Tick runs on the game thread once per game tick inside SEH. Ported from reach/main_thread.cpp
// (LaunchOnHit, ScaleAi, AirJump, PoisonTouch); Halo 3's fun_extras.cpp is the file template.
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "core/log.h"
#include "fun_extras_offsets.h"
#include "internal.h"

using namespace Halo4Cheats;

namespace {
    // --- Tuning -------------------------------------------------------------
    constexpr int kMaxTracked = 4096;        // object table slots followed for launch on hit
    constexpr float kHealthEpsilon = 4e-3f;  // a drop smaller than this isn't a hit (idle AI vitality flickers, see ODST)
    constexpr float kPoisonRange = 1.0f;     // world units horizontally (a biped is ~0.6 wide)
    constexpr float kPoisonHeight = 1.5f;    // and vertically (about a body height)
    constexpr float kScaleEpsilon = 0.001f;  // scale difference that counts as "different"
    constexpr float kAirGap = 0.25f;         // no ground this far below the feet = in the air (0.9 never counted a normal jump as airborne)

    using SetScaleFn = void(__fastcall*)(uint32_t object, float scale, uint16_t ticks);
    using SetVelocityFn = void(__fastcall*)(uint32_t object, const float* linear, const float* angular);
    using UnitKillFn = void(__fastcall*)(uint32_t unit, uint32_t, bool, int, int);
    using CollisionTestFn = bool(__fastcall*)(void* query, void* result);

    char* g_module = nullptr;
    bool g_logged[4] = {};

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    bool Seated(char* object) { return Field<uint32_t>(object, HALO4_OBJECT_PARENT) != 0xFFFFFFFF; }

    // Halo 4's ray (spawn.cpp's CastRay, same query shape): true if something is hit.
    bool CastRay(const Players& p, const float* from, const float* vec) {
        alignas(16) static uint8_t query[HALO4_QUERY_SIZE];
        alignas(16) static uint8_t result[HALO4_COLLISION_RESULT_SIZE];
        memset(query, 0, sizeof(query));
        memset(result, 0, sizeof(result));
        char* q = (char*)query;
        float end[3] = {from[0] + vec[0], from[1] + vec[1], from[2] + vec[2]};
        Field<int32_t>(q, HALO4_QUERY_KIND) = HALO4_COLLISION_KIND;
        Field<uint64_t>(q, HALO4_QUERY_FLAGS) = *(uint64_t*)(g_module + OFFSET_HALO4_COLLISION_FLAGS);
        memcpy(q + HALO4_QUERY_START, from, 3 * sizeof(float));
        memcpy(q + HALO4_QUERY_END, end, sizeof(end));
        int ignored = 0;
        if (p.count > 0) Field<uint32_t>(q, HALO4_QUERY_IGNORE + 4 * ignored++) = p.datums[0];
        Field<uint8_t>(q, HALO4_QUERY_IGNORE_COUNT) = (uint8_t)ignored;
        if (!Engine<CollisionTestFn>(OFFSET_HALO4_COLLISION_TEST)(query, result)) return false;
        float fraction = Field<float>((char*)result, HALO4_COLLISION_RESULT_FRACTION);
        return fraction >= 0.0f && fraction < 1.0f;
    }

    // --- Launch on hit --------------------------------------------------------
    // Every enemy AI biped is followed with its own health/shield; when either
    // dropped since last tick (or it just died) it gets launchStrength upward
    // through object_set_velocity (the engine's own jump does the same).
    struct Tracked {
        uint32_t datum;
        float health;
        float shield;
    };
    Tracked g_launch[kMaxTracked];

    void LaunchOnHit(const Players& p, const Array& objects, float speed, bool on) {
        if (!on) {
            memset(g_launch, 0, sizeof(g_launch));
            return;
        }
        for (int i = 0; i < objects.HighWater() && i < kMaxTracked; ++i) {
            char* entry = objects.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE);
            char* object = Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
            Tracked& t = g_launch[i];
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            bool known = salt && t.datum == datum;
            float* health = salt && object && type == HALO4_OBJECT_TYPE_BIPED ? Health(object) : nullptr;
            // A dead unit has lost its actor, so only the first look checks the
            // enemy test; after that the unit is followed to its death.
            if (!health || p.Contains(object) || (!known && !IsKillableEnemy(object, type, p))) {
                t.datum = 0;
                continue;
            }
            float* shield = Shield(object);
            float shieldNow = shield ? *shield : 0.0f;
            if (!known) {
                t = {datum, *health, shieldNow};
                continue;
            }
            bool hit = *health < t.health - kHealthEpsilon || shieldNow < t.shield - kHealthEpsilon;
            bool died = *health <= 0.0f;
            t.health = *health;
            t.shield = shieldNow;
            if (died) t.datum = 0;  // launched once, on the blow that killed it
            if (!hit || Seated(object)) continue;
            const float* now = &Field<float>(object, HALO4_OBJECT_VELOCITY);
            float velocity[3] = {now[0], now[1], (now[2] > 0.0f ? now[2] : 0.0f) + speed};
            Engine<SetVelocityFn>(OFFSET_HALO4_OBJECT_SET_VELOCITY)(datum, velocity, nullptr);
            if (!g_logged[0]) {
                g_logged[0] = true;
                LOG("Halo 4: launch on hit: first launch of unit %08X (speed %.2f)", datum, speed);
            }
        }
    }

    // --- Giant / tiny AI --------------------------------------------------------
    // Every AI unit is kept at the scale set for its side; object_set_scale's
    // core is called when its scale target (+0xA4) differs. Only units it
    // changed are put back when both sliders return to 1.
    std::unordered_map<uint32_t, float> g_scaledAi;  // unit datum -> the scale this set

    void ScaleAi(void** state, const Players& p, const Array& objects, float enemy, float friendly) {
        auto setScale = [&](uint32_t datum, float scale) {
            Engine<SetScaleFn>(OFFSET_HALO4_OBJECT_SET_SCALE)(datum, scale, 0);  // recurses into held weapons
            if (!g_logged[1]) {
                g_logged[1] = true;
                LOG("Halo 4: AI size: first object_set_scale on %08X to %.2f", datum, scale);
            }
        };
        bool wanted = fabsf(enemy - 1.0f) > kScaleEpsilon || fabsf(friendly - 1.0f) > kScaleEpsilon;
        if (!wanted && g_scaledAi.empty()) return;
        for (auto it = g_scaledAi.begin(); it != g_scaledAi.end();) {
            if (!ObjectFromDatum(objects, it->first)) { it = g_scaledAi.erase(it); continue; }
            if (!wanted) { setScale(it->first, 1.0f); it = g_scaledAi.erase(it); continue; }
            ++it;
        }
        if (!wanted) return;
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        int myTeam = p.count > 0 ? Field<int8_t>(p.units[0], HALO4_UNIT_TEAM) : HALO4_TEAM_PLAYER;
        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            if (!*(uint16_t*)actor) continue;
            uint32_t datum = Field<uint32_t>(actor, HALO4_ACTOR_UNIT);
            char* unit = ObjectFromDatum(objects, datum);
            if (!unit || p.Contains(unit) || Seated(unit)) continue;  // leave a vehicle's rider be
            float* health = Health(unit);
            if (health && *health <= 0.0f) continue;
            bool friends = Halo4Internal::TeamsFriends(state, Field<int8_t>(unit, HALO4_UNIT_TEAM), myTeam);
            float scale = friends ? friendly : enemy;
            float actual = Field<float>(unit, HALO4_FX_OBJECT_SCALE_TARGET);
            if (!(actual > 0.0f)) actual = 1.0f;
            auto known = g_scaledAi.find(datum);
            if (known == g_scaledAi.end()) {
                if (fabsf(scale - 1.0f) < kScaleEpsilon && fabsf(actual - 1.0f) < kScaleEpsilon) continue;
                if (fabsf(actual - scale) < kScaleEpsilon) continue;  // already the size wanted, nothing of ours
            } else if (fabsf(actual - scale) < kScaleEpsilon) {
                known->second = scale;
                continue;
            }
            setScale(datum, scale);
            g_scaledAi[datum] = scale;
        }
    }

    // --- Double / infinite jump ---------------------------------------------------
    // A jump press while in the air (no ground within kAirGap below the feet)
    // sets the unit's upward speed to the biped tag's jump velocity (the Jump
    // slider has already scaled it in the tag), keeping the horizontal speed.
    // Landing resets the count. Noclip, vehicles and the dead are skipped.
    int g_airJumpsUsed = 0;
    uint32_t g_jumpPressesSeen = 0;
    bool g_jumpSeenValid = false;

    void AirJump(const Players& p, const Games::Settings& s, const Games::FrameInput& in) {
        bool pressed = g_jumpSeenValid && in.jumpPresses != g_jumpPressesSeen;
        g_jumpPressesSeen = in.jumpPresses;
        g_jumpSeenValid = true;
        if (!s.doubleJump || p.count == 0 || s.noclip) { g_airJumpsUsed = 0; return; }
        char* unit = p.units[0];
        float* health = Health(unit);
        if (Seated(unit) || (health && *health <= 0.0f)) { g_airJumpsUsed = 0; return; }
        const float* pos = &Field<float>(unit, HALO4_OBJECT_POSITION);
        const float from[3] = {pos[0], pos[1], pos[2] + 0.1f};
        const float down[3] = {0.0f, 0.0f, -kAirGap};
        if (CastRay(p, from, down)) { g_airJumpsUsed = 0; return; }
        if (!pressed) return;
        int limit = s.infiniteJump ? 0x7FFFFFFF : (int)(s.airJumps + 0.5f);
        if (g_airJumpsUsed >= limit) return;
        char* tag = Halo4Tags::Get(Field<uint32_t>(unit, HALO4_OBJECT_TAG_DATUM));
        float jump = tag ? Field<float>(tag, HALO4_BIPD_JUMP_VELOCITY) : 0.0f;
        if (!(jump > 0.0f)) return;
        ++g_airJumpsUsed;
        const float* now = &Field<float>(unit, HALO4_OBJECT_VELOCITY);
        float velocity[3] = {now[0], now[1], jump};
        Engine<SetVelocityFn>(OFFSET_HALO4_OBJECT_SET_VELOCITY)(p.datums[0], velocity, nullptr);
        if (!g_logged[2]) {
            g_logged[2] = true;
            LOG("Halo 4: air jump at %.2f %.2f %.2f, upward speed %.2f (jump %d of %s)", pos[0], pos[1], pos[2], jump,
                g_airJumpsUsed, s.infiniteJump ? "unlimited" : "limit");
        }
    }

    // --- Poison touch ---------------------------------------------------------------
    // Any AI unit within reach of player 1 dies, friend or foe: made mortal
    // (the damage flags cleared), shield zeroed and health left at a sliver as
    // the damage multiplier's kill does, then unit_kill.
    void PoisonTouch(void** state, const Players& p, const Array& objects) {
        if (p.count == 0) return;
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        const float* me = &Field<float>(p.units[0], HALO4_OBJECT_POSITION);
        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            if (!*(uint16_t*)actor) continue;
            uint32_t datum = Field<uint32_t>(actor, HALO4_ACTOR_UNIT);
            char* unit = ObjectFromDatum(objects, datum);
            if (!unit || p.Contains(unit)) continue;
            float* health = Health(unit);
            if (health && *health <= 0.0f) continue;  // already dead
            const float* pos = &Field<float>(unit, HALO4_OBJECT_POSITION);
            float dx = pos[0] - me[0], dy = pos[1] - me[1], dz = pos[2] - me[2];
            if (dx * dx + dy * dy > kPoisonRange * kPoisonRange || fabsf(dz) > kPoisonHeight) continue;
            Field<uint32_t>(unit, HALO4_OBJECT_DAMAGE_FLAGS) &=
                ~(HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO4_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE);
            if (float* shield = Shield(unit)) *shield = 0.0f;
            if (health) *health = 0.0001f;
            Engine<UnitKillFn>(OFFSET_HALO4_UNIT_KILL)(datum, 0, true, 2, 0);
            if (!g_logged[3]) {
                g_logged[3] = true;
                LOG("Halo 4: poison touch: killed unit %08X", datum);
            }
        }
    }
}

namespace Halo4Fun {
    void Load(char* module) { g_module = module; }
    void Unload() { Reset(); }

    void Reset() {
        memset(g_launch, 0, sizeof(g_launch));
        g_scaledAi.clear();
        g_airJumpsUsed = 0;
        g_jumpSeenValid = false;
    }

    void Tick(void** state, const Games::Settings& s, const Games::FrameInput& in, const Players& p,
              const Array& objects) {
        if (!g_module || !objects.Valid()) return;
        __try { LaunchOnHit(p, objects, s.launchStrength, s.launchOnHit); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 4 launch on hit")) {}
        __try { ScaleAi(state, p, objects, s.aiScaleEnemy, s.aiScaleFriendly); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 4 AI size")) {}
        __try { AirJump(p, s, in); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 4 air jump")) {}
        if (s.poisonTouch) {
            __try { PoisonTouch(state, p, objects); }
            __except (Log::Exception(GetExceptionInformation(), "Halo 4 poison touch")) {}
        }
    }
}
