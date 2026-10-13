// Halo 2: launch on hit, giant/tiny AI, double/infinite jump, poison touch (the "fun_extras" work item; see NOTES.md).
// Halo2Fun::Tick runs on the game thread once per game tick inside SEH (main_thread.cpp).
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "core/log.h"
#include "fun_extras_offsets.h"
#include "internal.h"
#include "tags.h"

using namespace Halo2Cheats;

namespace {
    // --- Tuning -------------------------------------------------------------
    constexpr int kMaxTracked = 2048;        // object table slots followed for launch on hit
    constexpr float kHealthEpsilon = 1e-4f;  // a drop smaller than this isn't a hit
    constexpr float kPoisonRange = 1.0f;     // world units horizontally (a biped is ~0.6 wide)
    constexpr float kPoisonHeight = 1.5f;    // and vertically (about a body height)
    constexpr float kScaleEpsilon = 0.001f;  // scale difference that counts as "different"
    constexpr int kAirTicksBeforeJump = 4;   // game ticks off the ground before a press counts as an air jump
                                             // (the press that took off is still pending for a tick or two)

    using SetScaleFn = void(__fastcall*)(uint32_t object, float scale, uint16_t ticks);
    using SetVelocityFn = void(__fastcall*)(uint32_t object, const float* linear, const float* angular);
    using UnitKillFn = void(__fastcall*)(uint32_t object);

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    uint32_t DatumOf(int index, char* entry) { return ((uint32_t)*(uint16_t*)entry << 16) | (uint32_t)index; }

    // Same rule as IsKillableEnemy's team part: player and human teams are one
    // side, and so is anyone on player 1's own team (the Arbiter missions: the
    // Covenant).
    bool Friendly(char* unit, const Players& p) {
        int16_t team = Field<int16_t>(unit, HALO2_UNIT_TEAM);
        if (team == HALO2_TEAM_PLAYER || team == HALO2_TEAM_HUMAN) return true;
        return p.count > 0 && team == Field<int16_t>(p.units[0], HALO2_UNIT_TEAM);
    }

    bool IsAiBiped(char* object, int type) {
        return type == HALO2_OBJECT_TYPE_BIPED && Field<uint32_t>(object, HALO2_UNIT_ACTOR) != 0xFFFFFFFF;
    }

    void AddVelocity(char* object, uint32_t datum, const float* add) {
        const float* now = &Field<float>(object, HALO2_OBJECT_VELOCITY);
        float velocity[3] = {now[0] + add[0], now[1] + add[1], now[2] + add[2]};
        Engine<SetVelocityFn>(OFFSET_HALO2_OBJECT_SET_VELOCITY)(datum, velocity, nullptr);
    }

    // --- Launch on hit --------------------------------------------------------
    // Every enemy AI biped is followed with its own health/shield table; when
    // either dropped since last tick (or it just died) it gets launchStrength
    // upward. Halo 2 bipeds keep their velocity between ticks (the biped jump
    // itself is a set-velocity call, 0x928ad0 -> 0x8DB9D0), so adding to it is how a
    // jump is done; whether a standing biped leaves the floor is untested.
    struct Tracked {
        uint32_t datum;
        float health;
        float shield;
    };
    Tracked g_launch[kMaxTracked];

    void LaunchOnHit(const Players& p, const Objects& objects, float speed, bool on) {
        if (!on) {
            memset(g_launch, 0, sizeof(g_launch));
            return;
        }
        int count = objects.table.HighWater();
        for (int i = 0; i < count && i < kMaxTracked; ++i) {
            char* entry = objects.table.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = -1;
            char* object = salt ? objects.FromEntry(entry, &type) : nullptr;
            Tracked& t = g_launch[i];
            uint32_t datum = DatumOf(i, entry);
            bool known = object && t.datum == datum;
            // A dead unit has lost its actor, so only the first look checks
            // the enemy test; after that the unit is followed to its death.
            if (!object || type != HALO2_OBJECT_TYPE_BIPED || p.Contains(object) ||
                (!known && !IsKillableEnemy(object, type, p))) {
                t.datum = 0;
                continue;
            }
            float health = Field<float>(object, HALO2_OBJECT_HEALTH);
            float shield = Field<float>(object, HALO2_OBJECT_SHIELD);
            if (!known) {
                t = {datum, health, shield};
                continue;
            }
            bool hit = health < t.health - kHealthEpsilon || shield < t.shield - kHealthEpsilon;
            bool died = health <= 0.0f;
            t.health = health;
            t.shield = shield;
            if (died) t.datum = 0;  // launched once, on the blow that killed it
            if (!hit || Field<uint32_t>(object, HALO2_OBJECT_PARENT) != 0xFFFFFFFF) continue;
            const float up[3] = {0.0f, 0.0f, speed};
            AddVelocity(object, datum, up);
        }
    }

    // --- Giant / tiny AI --------------------------------------------------------
    // Every AI biped is kept at the scale set for its side: object_set_scale's
    // core is called when its scale (+0xA0) differs; only units it changed are
    // put back when both sliders return to 1.
    std::unordered_map<uint32_t, float> g_scaledAi;  // unit datum -> the scale this set

    void ScaleAi(const Players& p, const Objects& objects, float enemy, float friendly) {
        auto setScale = [&](uint32_t datum, float scale) {
            Engine<SetScaleFn>(OFFSET_HALO2_OBJECT_SET_SCALE)(datum, scale, 0);  // recurses into held weapons
        };
        bool wanted = fabsf(enemy - 1.0f) > kScaleEpsilon || fabsf(friendly - 1.0f) > kScaleEpsilon;
        if (!wanted && g_scaledAi.empty()) return;
        for (auto it = g_scaledAi.begin(); it != g_scaledAi.end();) {
            if (!objects.Get(it->first)) { it = g_scaledAi.erase(it); continue; }
            if (!wanted) { setScale(it->first, 1.0f); it = g_scaledAi.erase(it); continue; }
            ++it;
        }
        if (!wanted) return;
        int count = objects.table.HighWater();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* unit = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!unit || !IsAiBiped(unit, type) || p.Contains(unit)) continue;
            if (Field<uint32_t>(unit, HALO2_OBJECT_PARENT) != 0xFFFFFFFF) continue;  // seated: leave the vehicle's rider be
            if (Field<float>(unit, HALO2_OBJECT_HEALTH) <= 0.0f) continue;
            uint32_t datum = DatumOf(i, entry);
            float scale = Friendly(unit, p) ? friendly : enemy;
            float actual = Field<float>(unit, HALO2_FX_OBJECT_SCALE);
            if (!(actual > 0.0f)) actual = 1.0f;
            auto known = g_scaledAi.find(datum);
            if (known == g_scaledAi.end()) {
                if (fabsf(scale - 1.0f) < kScaleEpsilon && fabsf(actual - 1.0f) < kScaleEpsilon) continue;
                if (fabsf(actual - scale) < kScaleEpsilon) continue;  // already the size wanted, nothing of ours
            } else if (fabsf(actual - scale) < kScaleEpsilon) {
                continue;
            }
            setScale(datum, scale);
            g_scaledAi[datum] = scale;
        }
    }

    // --- Double / infinite jump ---------------------------------------------------
    // A jump press while airborne sets the unit's upward speed to the biped
    // tag's jump velocity (the Jump slider has already scaled it in the tag),
    // keeping the horizontal speed; written through object_set_velocity's core,
    // which is how the engine's own jump (0x928AD0) applies it. Airborne = the
    // character physics found no ground surface last tick (biped flag 0x1000
    // clear) while alive in ground mode and not seated; counted in game ticks
    // so the press that took off isn't spent. Noclip is skipped. Not applied
    // while dead, in a vehicle or in a tag without a jump velocity.
    int g_airJumpsUsed = 0;
    int g_airTicks = 0;
    uint32_t g_jumpPressesSeen = 0;
    bool g_jumpSeenValid = false;
    bool g_airJumpLogged = false;

    void AirJump(const Players& p, const Games::Settings& s, const Games::FrameInput& in) {
        bool pressed = g_jumpSeenValid && in.jumpPresses != g_jumpPressesSeen;
        g_jumpPressesSeen = in.jumpPresses;
        g_jumpSeenValid = true;
        if (!s.doubleJump || p.count == 0 || s.noclip) { g_airJumpsUsed = 0; g_airTicks = 0; return; }
        char* unit = p.units[0];
        bool grounded = (Field<uint16_t>(unit, HALO2_FX_BIPED_FLAGS) & HALO2_FX_BIPED_FLAG_GROUND) != 0;
        bool usable = Field<uint8_t>(unit, HALO2_FX_BIPED_PHYSICS_MODE) == HALO2_FX_PHYSICS_GROUND &&
                      Field<uint32_t>(unit, HALO2_OBJECT_PARENT) == 0xFFFFFFFF &&
                      Field<float>(unit, HALO2_OBJECT_HEALTH) > 0.0f;
        if (grounded || !usable) { g_airJumpsUsed = 0; g_airTicks = 0; return; }
        ++g_airTicks;
        if (!pressed) return;
        int limit = s.infiniteJump ? 0x7FFFFFFF : (int)(s.airJumps + 0.5f);
        if (g_airTicks < kAirTicksBeforeJump || g_airJumpsUsed >= limit) return;
        char* tag = Halo2Tags::Get(Field<uint32_t>(unit, HALO2_OBJECT_TAG_DATUM));
        float jump = tag ? Field<float>(tag, HALO2_BIPD_JUMP_VELOCITY) : 0.0f;
        if (!(jump > 0.0f)) return;
        ++g_airJumpsUsed;
        const float* now = &Field<float>(unit, HALO2_OBJECT_VELOCITY);
        float velocity[3] = {now[0], now[1], jump};
        Engine<SetVelocityFn>(OFFSET_HALO2_OBJECT_SET_VELOCITY)(p.datums[0], velocity, nullptr);
        if (!g_airJumpLogged) {
            g_airJumpLogged = true;
            const float* pos = &Field<float>(unit, HALO2_OBJECT_POSITION);
            LOG("Halo 2: air jump at %.2f %.2f %.2f, upward speed %.3f (jump %d)", pos[0], pos[1], pos[2], jump,
                g_airJumpsUsed);
        }
    }

    // --- Poison touch ---------------------------------------------------------------
    // Any AI biped within reach of player 1 dies, friend or foe: made mortal
    // (the cannot-take-damage flag cleared), health and shield zeroed, then
    // unit_kill's core. A seated unit counts only by its own position.
    void PoisonTouch(const Players& p, const Objects& objects) {
        if (p.count == 0) return;
        const float* me = &Field<float>(p.units[0], HALO2_OBJECT_POSITION);
        int count = objects.table.HighWater();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsAiBiped(object, type) || p.Contains(object)) continue;
            if (Field<float>(object, HALO2_OBJECT_HEALTH) <= 0.0f) continue;  // already dead
            const float* pos = &Field<float>(object, HALO2_OBJECT_POSITION);
            float dx = pos[0] - me[0], dy = pos[1] - me[1], dz = pos[2] - me[2];
            if (dx * dx + dy * dy > kPoisonRange * kPoisonRange || fabsf(dz) > kPoisonHeight) continue;
            Field<uint16_t>(object, HALO2_OBJECT_DAMAGE_FLAGS) &= (uint16_t)~HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE;
            Field<float>(object, HALO2_OBJECT_SHIELD) = 0.0f;
            Field<float>(object, HALO2_OBJECT_HEALTH) = 0.0f;
            Engine<UnitKillFn>(OFFSET_HALO2_UNIT_KILL_CORE)(DatumOf(i, entry));
        }
    }
}

namespace Halo2Fun {
    void Load(char* module) { g_module = module; }
    void Unload() { Reset(); }

    void Reset() {
        memset(g_launch, 0, sizeof(g_launch));
        g_scaledAi.clear();
        g_airJumpsUsed = 0;
        g_airTicks = 0;
        g_jumpSeenValid = false;
    }

    void Tick(const Games::Settings& s, const Games::FrameInput& in, const Players& p, const Objects& objects) {
        if (!g_module) return;
        __try { LaunchOnHit(p, objects, s.launchStrength, s.launchOnHit); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 2 launch on hit")) {}
        __try { ScaleAi(p, objects, s.aiScaleEnemy, s.aiScaleFriendly); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 2 AI size")) {}
        __try { AirJump(p, s, in); }
        __except (Log::Exception(GetExceptionInformation(), "Halo 2 air jump")) {}
        if (s.poisonTouch) {
            __try { PoisonTouch(p, objects); }
            __except (Log::Exception(GetExceptionInformation(), "Halo 2 poison touch")) {}
        }
    }
}
