#include "cheats.h"

#include <cstring>
#include <vector>

#include "cheat_util.h"
#include "core/log.h"
#include "core/patch.h"
#include "internal.h"
#include "spawn.h"
#include "tags.h"

namespace OdstCheats {
    // main_thread.cpp
    void SetMainThreadModule(HMODULE odst);
    void ResetMainThread();
}

using namespace OdstCheats;

namespace {
    // Skulls page, by menu bit (see MenuSkulls).
    const char* const kSkullNames[kMenuSkullCount] = {
        "Iron", "Black Eye", "Tough Luck", "Catch", "Fog", "Famine", "Thunderstorm", "Tilt", "Mythic",
        "Assassin", "Blind", "Cowbell", "Grunt Birthday Party", "IWHBYD", "Third Person", "Director's Cut",
        "Bandanna"};

    constexpr int kMaxSoftCeilings = 128;
    constexpr int kMaxAimbotTags = 16;
    constexpr int kMaxAmmoBarrels = 16;
    constexpr float kPi = 3.14159265f;

    // --- State we've changed ---------------------------------------------
    // A flag we set on player units. `units` remembers which, so a unit that
    // stops being a player's (co-op turned off: the second player's body stays
    // in the world) gets it cleared instead of keeping it for good.
    struct FlagState {
        bool applied = false;
        uint32_t units[8] = {};
        int next = 0;
    };
    FlagState g_godApplied;
    // "AI can't see you": the ai_disregard flag on each local player's unit, and whether
    // the unit already had it before we set it (we only clear what we set).
    struct BlindUnit {
        char* unit = nullptr;
        bool hadFlag = false;
    };
    BlindUnit g_blind[kMaxLocalPlayers];
    Restorable<float> g_speed[10];                 // matg player information speeds
    Restorable<float> g_jump[kMaxLocalPlayers];    // per player biped tag
    constexpr Games::BlockField kVehicleSpeedFields[] = {
        {ODST_VEHI_TANK, 0x58, 0x30}, {ODST_VEHI_JEEP, 0x40, 0x18},
        {ODST_VEHI_PLANE, 0x4C, 0x0}, {ODST_VEHI_PLANE, 0x4C, 0x4}, {ODST_VEHI_PLANE, 0x4C, 0x8},
        {ODST_VEHI_SCOUT, 0x70, 0x8}, {ODST_VEHI_SCOUT, 0x70, 0xC}, {ODST_VEHI_SCOUT, 0x70, 0x10},
        {ODST_VEHI_SCOUT, 0x70, 0x48}, {ODST_VEHI_SCOUT, 0x70, 0x5C},
        {ODST_VEHI_FIGHTER, 0x64, 0x14}, {ODST_VEHI_FIGHTER, 0x64, 0x18}, {ODST_VEHI_FIGHTER, 0x64, 0x1C},
        {ODST_VEHI_MANTIS, 0xAC, 0x14}, {ODST_VEHI_MANTIS, 0xAC, 0x18}, {ODST_VEHI_MANTIS, 0xAC, 0x1C},
        {ODST_VEHI_VTOL, 0x74, 0x44}, {ODST_VEHI_VTOL, 0x74, 0x48},
        {ODST_VEHI_CHOPPER, 0x70, 0x18},
        {ODST_VEHI_GUARDIAN, 0x30, 0x8}, {ODST_VEHI_GUARDIAN, 0x30, 0xC}, {ODST_VEHI_GUARDIAN, 0x30, 0x10},
    };
    Games::VehicleScaler<_countof(kVehicleSpeedFields)> g_vehicleSpeed;
    Games::VehicleGodState g_vehicleGod;
    // Infinite boost: peak power lasts (practically) forever. Releasing ends it
    // with a cooldown of dead time x (time boosted / boost length), which the
    // huge peak time makes negligible. Dead time must stay above 0: a cooldown
    // of 0 means "not cooling down", so 0 left the boost stuck on (2026-10-02).
    constexpr Games::TagValue kInfiniteBoost[] = {{ODST_VEHI_BOOST_PEAK_TIME, 100000.0f},
                                                  {ODST_VEHI_BOOST_DEAD_TIME, 1.0f}};
    Games::VehicleSetter<_countof(kInfiniteBoost)> g_infiniteBoost;
    // Cannot be shut down: no EMP time on the tag of the vehicle player 1 is in.
    constexpr Games::TagValue kNoEmp[] = {{ODST_VEHI_EMP_DISABLED_TIME, 0.0f}};
    Games::VehicleSetter<_countof(kNoEmp)> g_noEmp;
    // Cannot be hijacked: the seats of the vehicle player 1 is in (and of
    // what it's mounted on) can't be boarded while they're in it.
    constexpr int kMaxSeats = 32;
    Restorable<uint32_t> g_seatFlags[kMaxSeats];
    Restorable<int16_t> g_seatBoarding[kMaxSeats];
    // No vehicle recoil: firing/rider damage datums and firing effect
    // accelerations on the guns of the vehicle you're in.
    constexpr int kMaxRecoilFields = 96;
    Restorable<uint32_t> g_vehicleRecoil[kMaxRecoilFields];
    Restorable<float> g_vehiclePush[kMaxRecoilFields];
    Restorable<float> g_gravity;
    Restorable<float> g_gameSpeed;
    bool g_flyApplied = false;
    bool g_flyWasOn = false;                  // skull state before we turned it on
    Restorable<int32_t> g_killTriggerCount;
    Restorable<uint16_t> g_softCeilingFlags[kMaxSoftCeilings][2];
    Restorable<uint8_t> g_aiGrenades;

    struct AimbotTag {
        char* tag = nullptr;
        Restorable<float> fields[4];  // angle, range, falloff, deviation
    };
    AimbotTag g_aimbot[kMaxAimbotTags];

    Restorable<uint32_t> g_ammo[kMaxAmmoBarrels];  // barrel projectile datums


    ::Patch::Nop g_aiCantFire;
    ::Patch::Nop g_noRecoil;
    ::Patch::Nop g_aiDeaf;
    ::Patch::Nop g_thirdPerson;
    Restorable<uint8_t> g_hud;

    // Pickable tags, rebuilt when the map changes.
    char* g_listsScenario = nullptr;
    std::vector<Games::TagChoice> g_weapons;
    std::vector<Games::TagChoice> g_projectiles;

    // --- Cheats -------------------------------------------------------------

    // Per-unit flag: set, or cleared once when switched off.
    // Per-unit flag: set, or cleared once when switched off. Units we flagged
    // that no longer belong to a player are cleared too.
    void UnitFlag(const Players& p, Blam::DataArray* objects, int offset, uint32_t mask, bool on, FlagState& state) {
        for (uint32_t& d : state.units) {
            if (!d) continue;
            bool current = false;
            for (int i = 0; i < p.count; ++i) current |= p.datums[i] == d;
            if (current) continue;
            if (char* unit = Blam::ObjectFromDatum(objects, d)) Field<uint32_t>(unit, offset) &= ~mask;
            d = 0;
        }
        if (!on && !state.applied) return;
        for (int i = 0; i < p.count; ++i) {
            auto& flags = Field<uint32_t>(p.units[i], offset);
            if (on) flags |= mask;
            else flags &= ~mask;
            bool known = false;
            for (uint32_t d : state.units) known |= d == p.datums[i];
            if (on && !known) {
                state.units[state.next] = p.datums[i];
                state.next = (state.next + 1) % 8;
            }
        }
        if (!on)
            for (uint32_t& d : state.units) d = 0;
        state.applied = on;
    }

    // Player-only "AI can't see you": sets the ai_disregard flag (unit +0x168 bit 7, what the
    // ai_disregard script command sets) on every local player's unit. The AI copies it into each
    // perception prop it holds of that unit and skips those props (see offsets.h), so enemies stop
    // targeting the player while still seeing allies and each other. Off: the flag is cleared again
    // only on units where we set it.
    void Blind(const Players& p, bool on) {
        if (on) {
            for (auto& b : g_blind)  // units that died or were replaced
                if (b.unit && !p.Contains(b.unit)) b = {};
            for (int i = 0; i < p.count; ++i) {
                auto& flags = Field<uint32_t>(p.units[i], ODST_UNIT_FLAGS);
                BlindUnit* slot = nullptr;
                for (auto& b : g_blind)
                    if (b.unit == p.units[i]) { slot = &b; break; }
                if (!slot) {
                    for (auto& b : g_blind)
                        if (!b.unit) { slot = &b; break; }
                    if (!slot) continue;
                    slot->unit = p.units[i];
                    slot->hadFlag = (flags & ODST_UNIT_FLAG_AI_DISREGARD) != 0;
                }
                flags |= ODST_UNIT_FLAG_AI_DISREGARD;
            }
            return;
        }
        for (auto& b : g_blind) {
            if (!b.unit) continue;
            if (!b.hadFlag && p.Contains(b.unit)) Field<uint32_t>(b.unit, ODST_UNIT_FLAGS) &= ~ODST_UNIT_FLAG_AI_DISREGARD;
            b = {};
        }
    }

    // Tops up the player's weapons from their weapon tag's magazine sizes.
    void Ammo(const Players& p, Blam::DataArray* objects, bool infinite, bool bottomless) {
        if (!infinite && !bottomless) return;

        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < ODST_UNIT_WEAPON_COUNT; ++w) {
                uint32_t datum = Field<uint32_t>(p.units[i], ODST_UNIT_WEAPONS + w * 4);
                char* weapon = Blam::ObjectFromDatum(objects, datum);
                if (!weapon || Field<uint8_t>(weapon, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_WEAPON) continue;

                char* tag = OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM));
                int magazines = OdstTags::BlockCount(tag, ODST_WEAP_MAGAZINES);

                // Energy weapons' battery is the weapon's age. Some (the plasma
                // rifle) also list a magazine, so reset it for every weapon;
                // ballistic weapons don't age.
                Field<float>(weapon, ODST_WEAPON_AGE) = 0.0f;

                for (int m = 0; m < magazines && m < 2; ++m) {
                    char* def = OdstTags::BlockElement(tag, ODST_WEAP_MAGAZINES, ODST_WEAP_MAGAZINE_DEF_SIZE, m);
                    if (!def) continue;
                    char* mag = weapon + ODST_WEAPON_MAGAZINES + m * ODST_WEAPON_MAGAZINE_SIZE;

                    int16_t loadedMax = Field<int16_t>(def, ODST_MAGDEF_ROUNDS_LOADED_MAX);
                    int16_t totalMax = Field<int16_t>(def, ODST_MAGDEF_ROUNDS_TOTAL_MAX);
                    auto& loaded = Field<int16_t>(mag, ODST_MAGAZINE_LOADED);
                    auto& reserve = Field<int16_t>(mag, ODST_MAGAZINE_RESERVE);

                    if (bottomless && loaded < loadedMax) loaded = loadedMax;
                    int16_t reserveMax = totalMax > loadedMax ? totalMax - loadedMax : totalMax;
                    if (infinite && reserve < reserveMax) reserve = reserveMax;
                }
            }
        }
    }

    void Grenades(const Players& p, char* matg, bool on) {
        if (!on || !matg) return;
        int types = OdstTags::BlockCount(matg, ODST_MATG_GRENADES);
        for (int t = 0; t < types && t < ODST_UNIT_GRENADE_TYPES; ++t) {
            char* def = OdstTags::BlockElement(matg, ODST_MATG_GRENADES, ODST_MATG_GRENADE_DEF_SIZE, t);
            if (!def) continue;
            int16_t max = Field<int16_t>(def, 0);
            for (int i = 0; i < p.count; ++i) {
                auto& count = Field<int8_t>(p.units[i], ODST_UNIT_GRENADE_COUNTS + t);
                if (max > 0 && count < max) count = (int8_t)max;
            }
        }
    }

    // Leaves enemy AI with no shield and a sliver of health, so any hit kills.
    void OneHitKill(const Players& p, Blam::DataArray* objects, bool on) {
        if (!on) return;
        for (int i = 0; i < objects->highWater; ++i) {
            auto entry = (Blam::ObjectEntry*)objects->Entry(i);
            char* object = entry->object;
            if (entry->salt == 0 || !object || !IsKillableEnemy(object, p)) continue;

            auto& health = Field<float>(object, ODST_OBJECT_HEALTH);
            if (health <= 0.0f) continue;  // dying
            Field<float>(object, ODST_OBJECT_SHIELD) = 0.0f;
            if (health > 0.01f) health = 0.01f;
        }
    }

    void Speed(char* matg, float m) {
        char* info = OdstTags::BlockElement(matg, ODST_MATG_PLAYER_INFORMATION, ODST_MATG_PLAYER_INFO_SIZE, 0);
        if (!info) return;
        // Walk, run forward/back/side, run accel, sneak forward/back/side, sneak accel, air accel.
        for (int f = 0; f < 10; ++f)
            Scale(g_speed[f], (float*)(info + f * 4), m);
    }

    // Vehicle god mode: the damage flag (as for the player).
    void VehicleGod(const Players& p, Blam::DataArray* objects, bool on) {
        uint32_t seats[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i) seats[i] = Field<uint32_t>(p.units[i], ODST_OBJECT_PARENT);
        g_vehicleGod.Update(
            seats, p.count, on, [&](uint32_t d) -> char* {
                char* o = Blam::ObjectFromDatum(objects, d);
                return o && Field<uint8_t>(o, ODST_OBJECT_TYPE) == ODST_OBJECT_TYPE_VEHICLE ? o : nullptr;
            },
            [](char* o) { return Field<uint32_t>(o, ODST_OBJECT_PARENT); },
            [](char* o) { return (Field<uint32_t>(o, ODST_OBJECT_DAMAGE_FLAGS) & ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE) != 0; },
            [](char* o, bool god) {
                auto& flags = Field<uint32_t>(o, ODST_OBJECT_DAMAGE_FLAGS);
                flags = god ? (uint32_t)(flags | ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE)
                            : (uint32_t)(flags & ~ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
            });
    }

    // Cannot be shut down: the tag edit stops new EMPs on player 1's vehicle
    // type; a shutdown already running on any player's vehicle is cleared.
    void VehicleNoEmp(const Players& p, Blam::DataArray* objects, bool on) {
        char* vehicle = p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        char* tag = vehicle ? OdstTags::Get(Field<uint32_t>(vehicle, ODST_OBJECT_TAG_DATUM)) : nullptr;
        g_noEmp.Update(tag, kNoEmp, _countof(kNoEmp), on);
        for (int i = 0; on && i < p.count; ++i) {
            char* v = VehicleOf(p.units[i], objects);
            for (int depth = 0; v && depth < 4; ++depth) {
                Field<int16_t>(v, ODST_UNIT_EMP_TICKS) = 0;
                v = VehicleOf(v, objects);
            }
        }
    }

    // Puts `value` in each wanted field, keeping the originals in `slots`, and
    // restores the ones no longer wanted.
    template <typename T, typename Fn>
    void SetFields(Restorable<T> (&slots)[kMaxSeats], T* const* wanted, int count, Fn value) {
        for (auto& r : slots) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < count; ++i) keep |= wanted[i] == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < count; ++i) {
            Restorable<T>* slot = nullptr;
            for (auto& r : slots)
                if (r.addr == wanted[i]) { slot = &r; break; }
            for (auto& r : slots)
                if (!slot && !r.addr) slot = &r;
            if (!slot) continue;
            T base = slot->Ours(wanted[i]) ? slot->original : *wanted[i];
            slot->Set(wanted[i], value(base));
            slot->original = base;
        }
    }

    // True while the unit sits in a boarding seat, i.e. is hijacking. Reads
    // the seat's own flags, or what they were before we cleared them.
    bool Boarding(char* unit, char* vehicle) {
        char* tag = OdstTags::Get(Field<uint32_t>(vehicle, ODST_OBJECT_TAG_DATUM));
        char* seat = OdstTags::BlockElement(tag, ODST_VEHI_SEATS, ODST_VEHI_SEAT_SIZE,
                                             Field<int16_t>(unit, ODST_UNIT_SEAT));
        if (!seat) return false;
        uint32_t* at = &Field<uint32_t>(seat, ODST_SEAT_FLAGS);
        uint32_t flags = *at;
        for (auto& r : g_seatFlags)
            if (r.Ours(at)) flags = r.original;
        return (flags & ODST_SEAT_FLAG_BOARDING_SEAT) != 0;
    }

    // Cannot be hijacked: every seat of the vehicle player 1 is in (and of
    // what it's mounted on) loses "Boarding Seat" and its boarding seat
    // index, as in Halo 2. A tag edit, so same-type vehicles too, only while
    // you're in one, and not while player 1 is hijacking a vehicle themselves.
    void VehicleNoHijack(const Players& p, Blam::DataArray* objects, bool on) {
        uint32_t* flags[kMaxSeats] = {};
        int16_t* boarding[kMaxSeats] = {};
        int count = 0;
        char* vehicle = on && p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        if (vehicle && Boarding(p.units[0], vehicle)) vehicle = nullptr;
        for (int depth = 0; vehicle && depth < 4; ++depth) {
            char* tag = OdstTags::Get(Field<uint32_t>(vehicle, ODST_OBJECT_TAG_DATUM));
            int seats = OdstTags::BlockCount(tag, ODST_VEHI_SEATS);
            for (int i = 0; i < seats && count < kMaxSeats; ++i) {
                char* seat = OdstTags::BlockElement(tag, ODST_VEHI_SEATS, ODST_VEHI_SEAT_SIZE, i);
                if (!seat) continue;
                flags[count] = &Field<uint32_t>(seat, ODST_SEAT_FLAGS);
                boarding[count++] = &Field<int16_t>(seat, ODST_SEAT_BOARDING_INDEX);
            }
            vehicle = VehicleOf(vehicle, objects);
        }
        SetFields(g_seatFlags, flags, count, [](uint32_t f) { return f & ~ODST_SEAT_FLAG_BOARDING_SEAT; });
        SetFields(g_seatBoarding, boarding, count, [](int16_t) { return (int16_t)-1; });
    }

    // Sets each wanted field, and restores the ones no longer wanted.
    template <typename T, size_t N>
    void SetValues(Restorable<T> (&slots)[N], T* const* wanted, int wantedCount, T value) {
        for (auto& r : slots) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i] == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<T>* slot = nullptr;
            for (auto& r : slots)
                if (r.addr == wanted[i]) { slot = &r; break; }
            for (auto& r : slots)
                if (!slot && !r.addr) slot = &r;
            if (slot) slot->Set(wanted[i], value);
        }
    }

    // The acceleration fields of an effect tag's events, appended to `out`.
    void EffectAccelerations(uint32_t effect, float** out, int& count) {
        char* tag = OdstTags::Get(effect);
        int events = OdstTags::BlockCount(tag, ODST_EFFE_EVENTS);
        for (int e = 0; e < events; ++e) {
            char* event = OdstTags::BlockElement(tag, ODST_EFFE_EVENTS, ODST_EFFE_EVENT_SIZE, e);
            int accelerations = OdstTags::BlockCount(event, ODST_EFFE_EVENT_ACCELERATIONS);
            for (int a = 0; a < accelerations && count < kMaxRecoilFields; ++a) {
                char* acceleration = OdstTags::BlockElement(event, ODST_EFFE_EVENT_ACCELERATIONS,
                                                             ODST_EFFE_ACCELERATION_SIZE, a);
                if (acceleration) out[count++] = &Field<float>(acceleration, ODST_EFFE_ACCELERATION);
            }
        }
    }

    // No vehicle recoil: on the guns of the vehicle (and its turrets) each
    // player is in, the barrels' firing effects lose their damage on the
    // shooter and riders (the camera jolt), and the effects' accelerations
    // (a push back) are zeroed. Restored when they get out.
    void VehicleNoRecoil(const Players& p, Blam::DataArray* objects, bool on) {
        constexpr int kFields[] = {ODST_FIRING_EFFECT_DAMAGE, ODST_FIRING_EFFECT_SECONDARY_DAMAGE,
                                   ODST_FIRING_EFFECT_RIDER_DAMAGE, ODST_FIRING_EFFECT_SECONDARY_RIDER_DAMAGE};
        uint32_t* wanted[kMaxRecoilFields] = {};
        int wantedCount = 0;
        float* pushes[kMaxRecoilFields] = {};
        int pushCount = 0;
        for (int i = 0; on && i < p.count; ++i) {
            char* parts[16];
            int partCount = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < partCount; ++k) {
                for (int w = 0; w < ODST_UNIT_WEAPON_COUNT; ++w) {
                    char* weapon = Blam::ObjectFromDatum(objects, Field<uint32_t>(parts[k], ODST_UNIT_WEAPONS + w * 4));
                    if (!weapon || Field<uint8_t>(weapon, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_WEAPON) continue;
                    char* tag = OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM));
                    int barrels = OdstTags::BlockCount(tag, ODST_WEAP_BARRELS);
                    for (int b = 0; b < barrels; ++b) {
                        char* barrel = OdstTags::BlockElement(tag, ODST_WEAP_BARRELS, ODST_WEAP_BARREL_SIZE, b);
                        int effects = OdstTags::BlockCount(barrel, ODST_BARREL_FIRING_EFFECTS);
                        for (int e = 0; e < effects; ++e) {
                            char* effect = OdstTags::BlockElement(barrel, ODST_BARREL_FIRING_EFFECTS,
                                                                   ODST_FIRING_EFFECT_SIZE, e);
                            if (!effect) continue;
                            for (int field : kFields)
                                if (wantedCount < kMaxRecoilFields)
                                    wanted[wantedCount++] = &Field<uint32_t>(effect, field + ODST_TAGREF_DATUM);
                            for (int field : {ODST_FIRING_EFFECT_EFFECT, ODST_FIRING_EFFECT_SECONDARY_EFFECT})
                                EffectAccelerations(Field<uint32_t>(effect, field + ODST_TAGREF_DATUM), pushes, pushCount);
                        }
                    }
                }
            }
        }
        SetValues(g_vehicleRecoil, wanted, wantedCount, 0xFFFFFFFFu);
        SetValues(g_vehiclePush, pushes, pushCount, 0.0f);
    }

    // Vehicle speed: the tag of the vehicle player 1 is in (same-type vehicles too).
    void VehicleSpeed(const Players& p, Blam::DataArray* objects, float m) {
        char* vehicle = p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        char* tag = vehicle ? OdstTags::Get(Field<uint32_t>(vehicle, ODST_OBJECT_TAG_DATUM)) : nullptr;
        g_vehicleSpeed.Update(tag, nullptr, 0, kVehicleSpeedFields, _countof(kVehicleSpeedFields), m,
                              [](char* t, int block, int size) { return OdstTags::BlockElement(t, block, size, 0); });
    }

    void Jump(const Players& p, float m) {
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            char* tag = i < p.count ? OdstTags::Get(Field<uint32_t>(p.units[i], ODST_OBJECT_TAG_DATUM)) : nullptr;
            if (tag) Scale(g_jump[i], &Field<float>(tag, ODST_BIPD_JUMP_VELOCITY), m);
        }
    }

    // The Acrophobia skull is MCC's built-in flight. Re-applied every frame
    // because level loads reset skulls.
    void Fly(char* gameGlobals, bool on) {
        int skull = ODST_SKULL_ACROPHOBIA;
        uint64_t& word = Field<uint64_t>(gameGlobals, ODST_GAME_GLOBALS_SKULLS + (skull / 64) * 8);
        uint64_t mask = 1ull << (skull % 64);
        if (on) {
            if (!g_flyApplied) g_flyWasOn = (word & mask) != 0;
            word |= mask;
            g_flyApplied = true;
        } else if (g_flyApplied) {
            if (!g_flyWasOn) word &= ~mask;
            g_flyApplied = false;
        }
    }

    void NoBoundaries(char* scnr, bool on) {
        if (!scnr) return;

        auto* killCount = &Field<int32_t>(scnr, ODST_SCNR_KILL_TRIGGERS);
        if (on) g_killTriggerCount.Set(killCount, 0);
        else g_killTriggerCount.Restore();

        int ceilings = OdstTags::BlockCount(scnr, ODST_SCNR_SOFT_CEILINGS);
        for (int i = 0; i < ceilings && i < kMaxSoftCeilings; ++i) {
            char* ceiling = OdstTags::BlockElement(scnr, ODST_SCNR_SOFT_CEILINGS, ODST_SCNR_SOFT_CEILING_SIZE, i);
            if (!ceiling) continue;
            for (int f = 0; f < 2; ++f) {  // flags, runtime flags
                auto* flags = &Field<uint16_t>(ceiling, f * 2);
                Restorable<uint16_t>& patch = g_softCeilingFlags[i][f];
                if (on) {
                    uint16_t base = patch.Ours(flags) ? patch.original : *flags;
                    patch.Set(flags, base | ODST_SOFT_CEILING_IGNORE_ALL);
                    patch.original = base;
                } else if (patch.addr) {
                    patch.Restore();
                }
            }
        }
    }

    // Maxes out the built-in autoaim on the player's weapons, so shots bend
    // onto any enemy in view (same idea as the Cheat Engine table's aimbot).
    void Aimbot(const Players& p, Blam::DataArray* objects, bool on) {
        if (!on) {
            for (auto& a : g_aimbot) {
                for (auto& f : a.fields) f.Restore();
                a.tag = nullptr;
            }
            return;
        }

        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < ODST_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = Blam::ObjectFromDatum(objects, Field<uint32_t>(p.units[i], ODST_UNIT_WEAPONS + w * 4));
                if (!weapon || Field<uint8_t>(weapon, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_WEAPON) continue;
                char* tag = OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM));
                if (!tag || IsMeleeOnly(weapon)) continue;  // melee weapons keep their own aim

                // Reuse this tag's slot, or take a free one.
                AimbotTag* slot = nullptr;
                for (auto& a : g_aimbot)
                    if (a.tag == tag) { slot = &a; break; }
                for (auto& a : g_aimbot)
                    if (!slot && !a.tag) { slot = &a; slot->tag = tag; }
                if (!slot) continue;

                slot->fields[0].Set(&Field<float>(tag, ODST_WEAP_AUTOAIM_ANGLE), kPi);
                slot->fields[1].Set(&Field<float>(tag, ODST_WEAP_AUTOAIM_RANGE), 1000.0f);
                slot->fields[2].Set(&Field<float>(tag, ODST_WEAP_AUTOAIM_FALLOFF), 1000.0f);
                slot->fields[3].Set(&Field<float>(tag, ODST_WEAP_DEVIATION_ANGLE), kPi);
            }
        }
    }

    bool InList(const std::vector<Games::TagChoice>& list, uint32_t datum) {
        for (const auto& c : list)
            if (c.datum == datum) return true;
        return false;
    }

    // Points player 1's held weapon's barrels at the chosen projectile. Other
    // weapons go back to normal.
    void AmmoType(const Players& p, Blam::DataArray* objects, uint32_t projectile) {
        uint32_t* wanted[kMaxAmmoBarrels] = {};
        int wantedCount = 0;

        char* weapon = p.count > 0 && projectile != 0xFFFFFFFF && InList(g_projectiles, projectile)
                           ? HeldWeapon(p.units[0], objects) : nullptr;
        char* tag = weapon ? OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM)) : nullptr;
        int barrels = OdstTags::BlockCount(tag, ODST_WEAP_BARRELS);
        for (int b = 0; b < barrels && wantedCount < kMaxAmmoBarrels; ++b) {
            char* barrel = OdstTags::BlockElement(tag, ODST_WEAP_BARRELS, ODST_WEAP_BARREL_SIZE, b);
            if (barrel) wanted[wantedCount++] = &Field<uint32_t>(barrel, ODST_BARREL_PROJECTILE + ODST_TAGREF_DATUM);
        }

        // Restore barrels we no longer want, then set the ones we do.
        for (auto& patch : g_ammo) {
            if (!patch.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i] == patch.addr;
            if (!keep) patch.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<uint32_t>* slot = nullptr;
            for (auto& patch : g_ammo)
                if (patch.addr == wanted[i]) { slot = &patch; break; }
            for (auto& patch : g_ammo)
                if (!slot && !patch.addr) slot = &patch;
            if (slot) slot->Set(wanted[i], projectile);
        }
    }

    // AI can't shoot: code patch plus the global "AI throw grenades" switch.
    void AiCantShoot(char* aiGlobals, bool on) {
        g_aiCantFire.Apply(on);
        if (!aiGlobals) return;
        auto* grenades = &Field<uint8_t>(aiGlobals, ODST_AI_GLOBALS_GRENADES);
        if (on) g_aiGrenades.Set(grenades, 0);
        else g_aiGrenades.Restore();
    }

    // Weapons flagged "Cannot Be Used By Player" (turrets, vehicle guns).
    bool PlayerCanUse(uint32_t weaponTag) {
        __try {
            char* tag = OdstTags::Get(weaponTag);
            return tag && !(Field<uint32_t>(tag, ODST_WEAP_FLAGS) & ODST_WEAP_FLAG_NOT_FOR_PLAYER);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void RefreshLists(char* scnr) {
        if (scnr == g_listsScenario) return;
        g_listsScenario = scnr;
        OdstTeams::Reset();
        OdstExtras::Reset();
        OdstCheckpoints::Reset();
        OdstFun::Reset();
        OdstEquipment::Reset();
        OdstVehicles::Reset();
        OdstUpgrade::Reset();
        OdstTags::LoadNames();
        g_weapons.clear();
        for (auto& weapon : OdstTags::ListGroup(ODST_TAG_WEAP))
            if (PlayerCanUse(weapon.datum)) g_weapons.push_back(weapon);
        g_projectiles = OdstTags::ListGroup(ODST_TAG_PROJ);
        if (scnr) OdstSpawn::RefreshLists(scnr);
        LOG("ODST: map has %zu weapons, %zu projectiles", g_weapons.size(), g_projectiles.size());
        Games::Status lists;
        OdstSpawn::FillStatus(lists);
        Fun::SetPools(lists.spawnVehicles, lists.spawnObjects, lists.spawnAi, &g_weapons, &g_projectiles,
                      [](uint32_t tag) {
                          uint32_t group = OdstTags::GroupOf(tag);
                          return group != ODST_TAG_SCEN && group != ODST_TAG_MACH;
                      });
    }

    struct Frame {
        Blam::DataArray* players;
        Blam::DataArray* objects;
        char* gameGlobals;
        char* gameTime;
        char* physics;
        char* aiGlobals;
        char* allegiance;
        char* chud;
        char* matg;
        char* scnr;
    };

    // Reads the pointers the cheats need. SEH: they can be mid-teardown.
    bool ReadFrame(void** state, Frame& f) {
        __try {
            f.players = (Blam::DataArray*)state[ODST_TLS_SLOT_PLAYERS];
            f.objects = (Blam::DataArray*)state[ODST_TLS_SLOT_OBJECTS];
            f.gameGlobals = (char*)state[ODST_TLS_SLOT_GAME_GLOBALS];
            f.gameTime = (char*)state[ODST_TLS_SLOT_GAME_TIME];
            f.physics = (char*)state[ODST_TLS_SLOT_PHYSICS_CONSTANTS];
            f.aiGlobals = (char*)state[ODST_TLS_SLOT_AI_GLOBALS];
            f.allegiance = (char*)state[ODST_TLS_SLOT_ALLEGIANCE];
            f.chud = (char*)state[ODST_TLS_SLOT_CHUD];
            f.matg = OdstTags::GetGlobal(ODST_TAG_MATG);
            f.scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
            return f.players && f.objects && f.players->data && f.objects->data && f.gameGlobals;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool ApplyAll(const Frame& f, const Games::Settings& s, Games::Status& status) {
        __try {
            Players p = FindPlayers(f.players, f.objects);
            ReadAllies(p, f.gameGlobals, f.allegiance);

            // Both "cannot take damage" and "cannot die from damage": the first
            // alone let a player die when the Warthog they were in blew up
            // (2026-10-02). A pending kill is cleared too.
            UnitFlag(p, f.objects, ODST_OBJECT_DAMAGE_FLAGS,
                     ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | ODST_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE, s.godMode,
                     g_godApplied);
            for (int i = 0; s.godMode && i < p.count; ++i)
                Field<uint32_t>(p.units[i], ODST_OBJECT_DAMAGE_FLAGS) &= ~ODST_DAMAGE_FLAG_KILL;
            Blind(p, s.aiBlind);
            Ammo(p, f.objects, s.infiniteAmmo, s.bottomlessClip);
            Grenades(p, f.matg, s.infiniteGrenades);
            OneHitKill(p, f.objects, s.oneHitKill);
            Speed(f.matg, s.speed);
            Jump(p, s.jump);
            VehicleSpeed(p, f.objects, s.vehicleSpeed);
            VehicleGod(p, f.objects, s.vehicleGodMode);
            {
                char* vehicle = p.count > 0 ? VehicleOf(p.units[0], f.objects) : nullptr;
                char* tag = vehicle ? OdstTags::Get(Field<uint32_t>(vehicle, ODST_OBJECT_TAG_DATUM)) : nullptr;
                g_infiniteBoost.Update(tag, kInfiniteBoost, _countof(kInfiniteBoost), s.vehicleInfiniteBoost);
            }
            VehicleNoEmp(p, f.objects, s.vehicleNoEmp);
            VehicleNoHijack(p, f.objects, s.vehicleNoHijack);
            VehicleNoRecoil(p, f.objects, s.vehicleNoRecoil);
            if (f.physics) Scale(g_gravity, &Field<float>(f.physics, ODST_PHYSICS_GRAVITY), s.gravity);
            if (f.gameTime) Scale(g_gameSpeed, &Field<float>(f.gameTime, ODST_GAME_TIME_SPEED), s.gameSpeed);
            Fly(f.gameGlobals, s.fly);
            NoBoundaries(f.scnr, s.noBoundaries);
            Aimbot(p, f.objects, s.aimbot);
            AmmoType(p, f.objects, s.ammoType);
            AiCantShoot(f.aiGlobals, s.aiCantShoot);
            g_noRecoil.Apply(s.noRecoil);
            g_aiDeaf.Apply(s.aiDeaf);
            g_thirdPerson.Apply(s.thirdPerson);
            if (f.chud) {
                auto* show = &Field<uint8_t>(f.chud, ODST_CHUD_SHOW);
                if (s.noHud) g_hud.Set(show, 0);
                else g_hud.Restore();
            }

            status.playerCount = p.count;
            if (p.count > 0) {
                status.health = Field<float>(p.units[0], ODST_OBJECT_HEALTH);
                status.shield = Field<float>(p.units[0], ODST_OBJECT_SHIELD);
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}

void OdstCheats::Load(HMODULE odst) {
    SetMainThreadModule(odst);
    OdstSpawn::SetModule(odst);
    OdstTeams::Load((char*)odst);
    OdstExtras::Load((char*)odst);
    OdstCheckpoints::Load((char*)odst);
    OdstFun::Load((char*)odst);
    OdstEquipment::Load((char*)odst);
    OdstVehicles::Load((char*)odst);
    bool fire = g_aiCantFire.Init(::Patch::FindUnique(odst, ODST_AOB_AI_CANT_FIRE), ODST_AI_CANT_FIRE_SIZE);
    uint8_t* recoil = ::Patch::FindUnique(odst, ODST_AOB_RECOIL);
    bool noRecoil = recoil && recoil[ODST_RECOIL_CALL_OFFSET] == 0xE8 &&
                    g_noRecoil.Init(recoil + ODST_RECOIL_CALL_OFFSET, 5);
    const uint8_t deafBytes[] = ODST_AI_HEARING_PATCH;
    bool deaf = g_aiDeaf.Init(::Patch::FindUnique(odst, ODST_AOB_AI_HEARING), deafBytes, sizeof(deafBytes));
    uint8_t* director = ::Patch::FindUnique(odst, ODST_AOB_THIRD_PERSON);
    const uint8_t thirdBytes[] = ODST_THIRD_PERSON_PATCH;
    bool third = director && g_thirdPerson.Init(director + ODST_THIRD_PERSON_OFFSET, thirdBytes, sizeof(thirdBytes));
    LOG("ODST: AI-can't-fire patch %s, no-recoil patch %s, AI-hearing patch %s, "
        "third-person patch %s",
        fire ? "found" : "NOT FOUND", noRecoil ? "found" : "NOT FOUND",
        deaf ? "found" : "NOT FOUND", third ? "found" : "NOT FOUND");
}

void OdstCheats::Unload() {
    g_aiCantFire.Apply(false);
    g_noRecoil.Apply(false);
    g_aiDeaf.Apply(false);
    g_thirdPerson.Apply(false);
    g_aiCantFire.Forget();
    g_noRecoil.Forget();
    g_aiDeaf.Forget();
    g_thirdPerson.Forget();
    g_hud = {};

    g_godApplied = FlagState();
    for (auto& b : g_blind) b = {};
    for (auto& p : g_speed) p = {};
    for (auto& p : g_jump) p = {};
    g_vehicleSpeed.Forget();
    g_vehicleGod.Forget();
    g_infiniteBoost.Forget();
    g_noEmp.Forget();
    for (auto& r : g_seatFlags) r = {};
    for (auto& r : g_seatBoarding) r = {};
    for (auto& r : g_vehicleRecoil) r = {};
    for (auto& r : g_vehiclePush) r = {};
    g_gravity = {};
    g_gameSpeed = {};
    g_flyApplied = false;
    g_flyWasOn = false;
    g_killTriggerCount = {};
    for (auto& row : g_softCeilingFlags)
        for (auto& p : row) p = {};
    g_aiGrenades = {};
    for (auto& a : g_aimbot) a = {};
    for (auto& p : g_ammo) p = {};
    g_listsScenario = nullptr;
    g_weapons.clear();
    g_projectiles.clear();
    OdstVehicles::Unload();
    OdstEquipment::Unload();
    OdstFun::Unload();
    OdstCheckpoints::Unload();
    OdstExtras::Unload();
    OdstTeams::Unload();
    OdstUpgrade::Reset();
    ResetMainThread();
    OdstSpawn::Reset();
    OdstSpawn::SetModule(nullptr);
    SetMainThreadModule(nullptr);
}

namespace {
    uint64_t ReadMenuSkulls(char* gameGlobals) {
        __try {
            return gameGlobals ? MenuSkulls(gameGlobals) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
}

bool OdstCheats::Apply(void** gameState, const Games::Settings& settings, Games::Status& status) {
    Fun::SetSettings(settings);
    status.skullNames = kSkullNames;
    status.skullCount = kMenuSkullCount;
    Frame f{};
    if (!ReadFrame(gameState, f)) return false;
    status.skullsActive = ReadMenuSkulls(f.gameGlobals);

    RefreshLists(f.scnr);
    status.weapons = &g_weapons;
    status.projectiles = &g_projectiles;
    OdstSpawn::FillStatus(status);

    OdstUpgrade::Apply(settings, status);
    OdstCheckpoints::Apply(settings, status);
    OdstEquipment::Apply(settings, status);
    return ApplyAll(f, settings, status);
}

// The chaos gun and recoil push around one barrel's shot. Halo 3's barrel
// fire runs after the objects update, once per barrel marked as fired.
void OdstCheats::BeforeShot(void** state, uint32_t weapon, int16_t barrel, Fun::Shot& shot) {
    if (!Fun::Active() || !state) return;
    __try {
        auto objects = (Blam::DataArray*)state[ODST_TLS_SLOT_OBJECTS];
        auto players = (Blam::DataArray*)state[ODST_TLS_SLOT_PLAYERS];
        if (!objects || !players) return;
        char* object = Blam::ObjectFromDatum(objects, weapon);
        uint32_t holder = object ? Field<uint32_t>(object, ODST_OBJECT_PARENT) : 0xFFFFFFFF;
        char* holderObject = Blam::ObjectFromDatum(objects, holder);
        if (!holderObject) return;

        // Whose shot: a player's own weapon, or a gun of the vehicle (or
        // turret) a player is in.
        Players p = FindPlayers(players, objects);
        int shooter = -1;
        bool vehicleGun = false;
        for (int i = 0; i < p.count && shooter < 0; ++i) {
            if (p.datums[i] == holder) { shooter = i; break; }
            char* parts[16];
            int count = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < count; ++k)
                if (parts[k] == holderObject) { shooter = i; vehicleGun = true; break; }
        }
        if (shooter < 0) return;

        // Every projectile reference this shot may read (see offsets.h).
        uint32_t* fields[Fun::Shot::kMax];
        int fieldCount = 0;
        char* tag = OdstTags::Get(Field<uint32_t>(object, ODST_OBJECT_TAG_DATUM));
        auto add = [&](int index, bool crate) {
            char* def = OdstTags::BlockElement(tag, ODST_WEAP_BARRELS, ODST_WEAP_BARREL_SIZE, index);
            if (!def) return;
            fields[fieldCount++] = &Field<uint32_t>(def, ODST_BARREL_PROJECTILE + ODST_TAGREF_DATUM);
            fields[fieldCount++] = &Field<uint32_t>(def, ODST_BARREL_SECONDARY_PROJECTILE + ODST_TAGREF_DATUM);
            if (crate) fields[fieldCount++] = &Field<uint32_t>(def, ODST_BARREL_CRATE_PROJECTILE + ODST_TAGREF_DATUM);
        };
        add(barrel, true);
        if (barrel == 0) add(1, false);

        char* control = (char*)state[ODST_TLS_SLOT_PLAYER_CONTROL] + shooter * ODST_PLAYER_CONTROL_SIZE;
        float yaw = Field<float>(control, ODST_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, ODST_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        // Seated, the whole vehicle moves: the top vehicle above the shooter.
        uint32_t target = p.datums[shooter];
        for (int depth = 0; depth < 5; ++depth) {
            char* at = Blam::ObjectFromDatum(objects, target);
            uint32_t parent = at ? Field<uint32_t>(at, ODST_OBJECT_PARENT) : 0xFFFFFFFF;
            char* up = Blam::ObjectFromDatum(objects, parent);
            if (!up || Field<uint8_t>(up, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_VEHICLE) break;
            target = parent;
        }
        if (target == p.datums[shooter] && vehicleGun) target = 0xFFFFFFFF;
        Fun::BeforeShot(fields, fieldCount, target, dir, shot);
    } __except (Log::Exception(GetExceptionInformation(), "ODST shot effects")) {
    }
}
