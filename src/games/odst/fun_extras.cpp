// ODST: launch on hit, giant/tiny AI, double/infinite jump, poison touch (the "fun_extras" work item; see NOTES.md).
// OdstFun::Tick runs on the game thread once per game tick inside SEH.
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "core/log.h"
#include "fun_extras_offsets.h"
#include "internal.h"
#include "tags.h"

using namespace OdstCheats;

namespace {
    // --- Tuning -------------------------------------------------------------
    constexpr int kMaxTracked = 4096;        // object table slots followed for launch on hit
    constexpr float kHealthEpsilon = 4e-3f;  // a drop smaller than this isn't a hit (unspawned/low-vitality AI flicker by 0.001 every tick, found live)
    constexpr float kPoisonRange = 1.0f;     // world units horizontally (a biped is ~0.6 wide)
    constexpr float kPoisonHeight = 1.5f;    // and vertically (about a body height)
    constexpr float kScaleEpsilon = 0.001f;  // scale difference that counts as "different"
    constexpr int kAirTicksBeforeJump = 4;   // game ticks off the ground before a press counts as an air jump
                                             // (the press that took off is still pending for a tick or two)

    using SetScaleFn = void(__fastcall*)(uint32_t object, float scale, uint16_t ticks);
    using SetVelocityFn = void(__fastcall*)(uint32_t object, const float* linear, const float* angular);
    using UnitKillFn = void(__fastcall*)(uint32_t unit);

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    uint32_t DatumOf(int index, uint16_t salt) { return ((uint32_t)salt << 16) | (uint32_t)index; }

    // The live object behind table slot `index`, or nullptr (free slot / no data).
    char* ObjectAt(Blam::DataArray* objects, int index, uint32_t* datum) {
        auto entry = (Blam::ObjectEntry*)objects->Entry(index);
        if (entry->salt == 0 || !entry->object) return nullptr;
        *datum = DatumOf(index, entry->salt);
        return entry->object;
    }

    bool IsBiped(char* object) { return Field<uint8_t>(object, ODST_OBJECT_TYPE) == ODST_OBJECT_TYPE_BIPED; }
    bool HasActor(char* object) { return Field<int32_t>(object, ODST_UNIT_ACTOR_INDEX) != -1; }
    bool Seated(char* object) { return Field<uint32_t>(object, ODST_OBJECT_PARENT) != 0xFFFFFFFF; }

    // Your side: a team player 1's team treats as friends (Players::allies, from
    // the game's allegiance table).
    bool Friendly(char* unit, const Players& p) {
        int16_t team = Field<int16_t>(unit, ODST_UNIT_TEAM);
        return team >= 0 && team < ODST_TEAM_COUNT && (p.allies & (1u << team)) != 0;
    }

    void AddVelocity(char* object, uint32_t datum, const float* add) {
        const float* now = &Field<float>(object, ODST_OBJECT_VELOCITY);
        float velocity[3] = {now[0] + add[0], now[1] + add[1], now[2] + add[2]};
        Engine<SetVelocityFn>(OFFSET_ODST_OBJECT_SET_VELOCITY)(datum, velocity, nullptr);
    }

    // --- Launch on hit --------------------------------------------------------
    // Every enemy AI biped is followed with its own health/shield table; when
    // either dropped since last tick (or it just died) it gets launchStrength
    // upward. The engine's own biped jump is a set-velocity call too (0x3CF490
    // -> 0x3411A4, AI included), so adding to the velocity is how a jump is
    // done; whether a standing biped leaves the floor is untested.
    struct Tracked {
        uint32_t datum;
        float health;
        float shield;
    };
    Tracked g_launch[kMaxTracked];

    void LaunchOnHit(const Players& p, Blam::DataArray* objects, float speed, bool on) {
        if (!on) {
            memset(g_launch, 0, sizeof(g_launch));
            return;
        }
        for (int i = 0; i < objects->highWater && i < kMaxTracked; ++i) {
            uint32_t datum = 0;
            char* object = ObjectAt(objects, i, &datum);
            Tracked& t = g_launch[i];
            bool known = object && t.datum == datum;
            // A dead unit has lost its actor, so only the first look checks
            // the enemy test; after that the unit is followed to its death.
            // Objects with no tag loaded are AI the game hasn't spawned yet (their health flickers); leave them.
            if (object && Field<uint32_t>(object, ODST_OBJECT_TAG_DATUM) == 0xFFFFFFFF) {
                t.datum = 0;
                continue;
            }
            if (!object || !IsBiped(object) || p.Contains(object) || (!known && !IsKillableEnemy(object, p))) {
                t.datum = 0;
                continue;
            }
            float health = Field<float>(object, ODST_OBJECT_HEALTH);
            float shield = Field<float>(object, ODST_OBJECT_SHIELD);
            if (!known) {
                t = {datum, health, shield};
                continue;
            }
            float prevHealth = t.health, prevShield = t.shield;
            bool hit = health < t.health - kHealthEpsilon || shield < t.shield - kHealthEpsilon;
            bool died = health <= 0.0f;
            t.health = health;
            t.shield = shield;
            if (died) t.datum = 0;  // launched once, on the blow that killed it
            if (!hit || Seated(object)) continue;
            static int s_logged = 0;
            if (s_logged++ < 20)
                LOG("ODST: launch on hit: unit %08X health %.3f shield %.3f (was %.3f / %.3f), tag %08X", datum,
                    health, shield, prevHealth, prevShield, Field<uint32_t>(object, ODST_OBJECT_TAG_DATUM));
            const float up[3] = {0.0f, 0.0f, speed};
            AddVelocity(object, datum, up);
        }
    }

    // --- Giant / tiny AI --------------------------------------------------------
    // Every AI biped is kept at the scale set for its side: object_set_scale's
    // core is called when its scale (+0x8C) differs; only units it changed are
    // put back when both sliders return to 1.
    std::unordered_map<uint32_t, float> g_scaledAi;  // unit datum -> the scale this set

    void ScaleAi(const Players& p, Blam::DataArray* objects, float enemy, float friendly) {
        auto setScale = [&](uint32_t datum, float scale) {
            Engine<SetScaleFn>(OFFSET_ODST_OBJECT_SET_SCALE)(datum, scale, 0);  // recurses into held weapons
        };
        bool wanted = fabsf(enemy - 1.0f) > kScaleEpsilon || fabsf(friendly - 1.0f) > kScaleEpsilon;
        if (!wanted && g_scaledAi.empty()) return;
        for (auto it = g_scaledAi.begin(); it != g_scaledAi.end();) {
            if (!Blam::ObjectFromDatum(objects, it->first)) { it = g_scaledAi.erase(it); continue; }
            if (!wanted) { setScale(it->first, 1.0f); it = g_scaledAi.erase(it); continue; }
            ++it;
        }
        if (!wanted) return;
        for (int i = 0; i < objects->highWater; ++i) {
            uint32_t datum = 0;
            char* unit = ObjectAt(objects, i, &datum);
            if (!unit || !IsBiped(unit) || !HasActor(unit) || p.Contains(unit)) continue;
            if (Seated(unit)) continue;  // leave a vehicle's rider be
            if (Field<float>(unit, ODST_OBJECT_HEALTH) <= 0.0f) continue;
            float scale = Friendly(unit, p) ? friendly : enemy;
            float actual = Field<float>(unit, ODST_FX_OBJECT_SCALE);
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
    // keeping the horizontal speed; written through object_set_velocity, which
    // is how the engine's own jump applies it. Airborne = the biped's airborne
    // bit (+0x4DC bit 2) while alive in ground mode and not seated; counted in
    // game ticks so the press that took off isn't spent. Noclip is skipped.
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
        bool airborne = (Field<uint8_t>(unit, ODST_FX_BIPED_AIRBORNE_BYTE) & ODST_FX_BIPED_AIRBORNE_BIT) != 0;
        bool usable = IsBiped(unit) && Field<uint8_t>(unit, ODST_FX_BIPED_PHYSICS_MODE) == ODST_FX_PHYSICS_GROUND &&
                      !Seated(unit) &&
                      !(Field<uint32_t>(unit, ODST_OBJECT_PHYSICS_FLAGS) & ODST_PHYSICS_FLAG_DISABLED) &&
                      Field<float>(unit, ODST_OBJECT_HEALTH) > 0.0f;
        if (!airborne || !usable) { g_airJumpsUsed = 0; g_airTicks = 0; return; }
        ++g_airTicks;
        if (!pressed) return;
        int limit = s.infiniteJump ? 0x7FFFFFFF : (int)(s.airJumps + 0.5f);
        if (g_airTicks < kAirTicksBeforeJump || g_airJumpsUsed >= limit) return;
        char* tag = OdstTags::Get(Field<uint32_t>(unit, ODST_OBJECT_TAG_DATUM));
        float jump = tag ? Field<float>(tag, ODST_BIPD_JUMP_VELOCITY) : 0.0f;
        if (!(jump > 0.0f)) return;
        ++g_airJumpsUsed;
        const float* now = &Field<float>(unit, ODST_OBJECT_VELOCITY);
        float velocity[3] = {now[0], now[1], jump};
        Engine<SetVelocityFn>(OFFSET_ODST_OBJECT_SET_VELOCITY)(p.datums[0], velocity, nullptr);
        if (!g_airJumpLogged) {
            g_airJumpLogged = true;
            const float* pos = &Field<float>(unit, ODST_OBJECT_POSITION);
            LOG("ODST: air jump at %.2f %.2f %.2f, upward speed %.3f (jump %d)", pos[0], pos[1], pos[2], jump,
                g_airJumpsUsed);
        }
    }

    // --- Poison touch ---------------------------------------------------------------
    // Any enemy AI biped within reach of player 1 dies (never a player or an ally:
    // the allegiance table decides, so marines/ODSTs and story allies are safe).
    // Made mortal (the damage flags cleared), shield zeroed and health left at a
    // sliver as the damage multiplier's kill does (main_thread.cpp), then
    // unit_kill. A seated unit counts only by its own position.
    void PoisonTouch(const Players& p, Blam::DataArray* objects) {
        if (p.count == 0) return;
        const float* me = &Field<float>(p.units[0], ODST_OBJECT_POSITION);
        for (int i = 0; i < objects->highWater; ++i) {
            uint32_t datum = 0;
            char* object = ObjectAt(objects, i, &datum);
            if (!object || !IsBiped(object) || !HasActor(object) || p.Contains(object)) continue;
            if (Field<float>(object, ODST_OBJECT_HEALTH) <= 0.0f) continue;  // already dead
            const float* pos = &Field<float>(object, ODST_OBJECT_POSITION);
            float dx = pos[0] - me[0], dy = pos[1] - me[1], dz = pos[2] - me[2];
            if (dx * dx + dy * dy > kPoisonRange * kPoisonRange || fabsf(dz) > kPoisonHeight) continue;
            Field<uint32_t>(object, ODST_OBJECT_DAMAGE_FLAGS) &=
                ~(ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | ODST_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE);
            Field<float>(object, ODST_OBJECT_SHIELD) = 0.0f;
            Field<float>(object, ODST_OBJECT_HEALTH) = 0.0001f;
            Engine<UnitKillFn>(OFFSET_ODST_UNIT_KILL)(datum);
        }
    }
}

namespace OdstFun {
    void Load(char* module) { g_module = module; }
    void Unload() { Reset(); }

    void Reset() {
        memset(g_launch, 0, sizeof(g_launch));
        g_scaledAi.clear();
        g_airJumpsUsed = 0;
        g_airTicks = 0;
        g_jumpSeenValid = false;
    }

    void Tick(void** state, const Games::Settings& s, const Games::FrameInput& in, const Players& p,
              Blam::DataArray* objects) {
        (void)state;
        if (!g_module || !objects) return;
        __try { LaunchOnHit(p, objects, s.launchStrength, s.launchOnHit); }
        __except (Log::Exception(GetExceptionInformation(), "ODST launch on hit")) {}
        __try { ScaleAi(p, objects, s.aiScaleEnemy, s.aiScaleFriendly); }
        __except (Log::Exception(GetExceptionInformation(), "ODST AI size")) {}
        __try { AirJump(p, s, in); }
        __except (Log::Exception(GetExceptionInformation(), "ODST air jump")) {}
        if (s.poisonTouch) {
            __try { PoisonTouch(p, objects); }
            __except (Log::Exception(GetExceptionInformation(), "ODST poison touch")) {}
        }
    }
}
