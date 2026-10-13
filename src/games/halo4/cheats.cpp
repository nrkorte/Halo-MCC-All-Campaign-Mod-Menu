#include "cheats.h"

#include <cmath>
#include <cstring>
#include <intrin.h>
#include <vector>

#include "cheat_util.h"
#include "core/log.h"
#include "core/patch.h"
#include "MinHook.h"
#include "internal.h"
#include "spawn.h"
#include "tags.h"

namespace Halo4Cheats {
    // main_thread.cpp
    void SetMainThreadModule(HMODULE halo4);
    void ResetMainThread();
}

#define OFFSET_HALO4_TEAMS_HOSTILE 0x2078E0  // team-pair hostility test (true = hostile); see TeamsHostileDetour

namespace {
    char* g_halo4 = nullptr;  // halo4.dll, for its globals (HUD)

    // Skulls page, by bit (the script skull enum; names from the DLL's own
    // skull string table, RVA 0xCDC898).
    const char* const kSkullNames[HALO4_SKULL_COUNT] = {
        "Iron", "Black Eye", "Tough Luck", "Catch", "Cloud", "Famine", "Thunderstorm", "Tilt", "Mythic",
        "Assassin", "Blind", "Cowbell", "Grunt Birthday Party", "IWHBYD", "Red", "Yellow", "Blue"};
}

using namespace Halo4Cheats;

namespace {
    constexpr int kMaxSoftCeilings = 128;
    constexpr int kMaxAimbotTags = 16;
    constexpr int kMaxAmmoBarrels = 16;
    constexpr int kMaxRecoilEffects = 64;
    constexpr int kMaxDeafActors = 512;
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
    // Enemies can't see you: per player, the unit we set ai_disregard on (datum)
    // and whether the bit was clear before, so it's only cleared if it was ours.
    struct BlindState {
        uint32_t unit = 0xFFFFFFFF;
        bool ours = false;
    };
    BlindState g_blind[kMaxLocalPlayers];

    // --- Auto turrets and "AI can't see you" ------------------------------------
    // The Knights' deployable turrets (equipment storm_auto_turret_pve ->
    // vehicle turret_mounted_pve; cs_deploy_turret) are not actors. They run the
    // sentry component (RVA 0x6A8000-0x6AE000: vehicle_auto_turret 0x63FB9C,
    // sentry_activate 0x6A84A0), which looks for targets itself and never reads
    // the ai_disregard flag (that only reaches the actor perception props). Its
    // target search (0x6AAC18) lists objects around the turret (type mask 0x2003),
    // and for each asks the team test 0x2078E0 (team bytes, true = hostile) before
    // ray-casting it; the tracking code asks it again for what its ray hit
    // (0x6AD208) and for each player (0x6ACD17, a walk over the player table).
    // The detour below answers "not hostile" at those three call sites for a
    // unit with ai_disregard set (sites 1 and 2: rdx = unit + 0x498, so the flags
    // word is at rdx - 8), and for the player walk while the cheat is on. Every
    // other caller, and AI vs AI, is untouched.
    constexpr int kTurretCallSearch = 0x6AAE18;  // return addresses (RVA) of the three calls
    constexpr int kTurretCallPlayers = 0x6ACD1C;
    constexpr int kTurretCallRay = 0x6AD20D;
    using TeamsHostileFn = bool(__fastcall*)(const void* a, const void* b);
    TeamsHostileFn g_origTeamsHostile = nullptr;
    void* g_teamsHostileHook = nullptr;
    volatile bool g_blindOn = false;  // the cheat is on (set by Blind on the render thread)
    int g_turretBlindLogs = 0;

    bool __fastcall TeamsHostileDetour(const void* a, const void* b) {
        bool hostile = g_origTeamsHostile(a, b);
        if (hostile && g_blindOn && g_halo4) {
            uintptr_t site = (uintptr_t)_ReturnAddress() - (uintptr_t)g_halo4;
            bool hidden = false;
            if (site == kTurretCallSearch || site == kTurretCallRay)
                hidden = (*(const uint64_t*)((const char*)b - 8) & HALO4_UNIT_FLAG_AI_DISREGARD) != 0;
            else if (site == kTurretCallPlayers)
                hidden = true;
            if (hidden) {
                if (g_turretBlindLogs < 3) LOG("Halo 4: an auto turret was blinded to the player (site %05zX)", site);
                if (g_turretBlindLogs < 3) ++g_turretBlindLogs;
                return false;
            }
        }
        return hostile;
    }
    Restorable<float> g_speed[10];                 // matg player information speeds
    Restorable<float> g_jump[kMaxLocalPlayers];    // per player biped tag
    constexpr Games::BlockField kVehicleSpeedFields[] = {
        {HALO4_VEHI_TANK, 0x94, 0x3C}, {HALO4_VEHI_JEEP, 0x5C, 0x18}, {HALO4_VEHI_WOLVERINE, 0x60, 0x18},
        {HALO4_VEHI_PLANE, 0x64, 0x0}, {HALO4_VEHI_PLANE, 0x64, 0x4}, {HALO4_VEHI_PLANE, 0x64, 0x8},
        {HALO4_VEHI_SCOUT, 0x78, 0x8}, {HALO4_VEHI_SCOUT, 0x78, 0xC}, {HALO4_VEHI_SCOUT, 0x78, 0x10},
        {HALO4_VEHI_SCOUT, 0x78, 0x50}, {HALO4_VEHI_SCOUT, 0x78, 0x64},
        {HALO4_VEHI_FIGHTER, 0x68, 0x14}, {HALO4_VEHI_FIGHTER, 0x68, 0x18}, {HALO4_VEHI_FIGHTER, 0x68, 0x1C},
        {HALO4_VEHI_MANTIS, 0xAC, 0x14}, {HALO4_VEHI_MANTIS, 0xAC, 0x18}, {HALO4_VEHI_MANTIS, 0xAC, 0x1C},
        {HALO4_VEHI_CHOPPER, 0x7C, 0x18},
        {HALO4_VEHI_GUARDIAN, 0x30, 0x8}, {HALO4_VEHI_GUARDIAN, 0x30, 0xC}, {HALO4_VEHI_GUARDIAN, 0x30, 0x10},
        {HALO4_VEHI_JACKAL_GLIDER, 0x170, 0x8}, {HALO4_VEHI_JACKAL_GLIDER, 0x170, 0xC},
        {HALO4_VEHI_JACKAL_GLIDER, 0x170, 0x10}, {HALO4_VEHI_JACKAL_GLIDER, 0x170, 0x3C},
        {HALO4_VEHI_JACKAL_GLIDER, 0x170, 0x50},
        {HALO4_VEHI_BOAT, 0x60, 0x24},
        {HALO4_VEHI_SPACE_FIGHTER, 0x110, 0x14}, {HALO4_VEHI_SPACE_FIGHTER, 0x110, 0x18},
        {HALO4_VEHI_SPACE_FIGHTER, 0x110, 0x1C}, {HALO4_VEHI_SPACE_FIGHTER, 0x110, 0x20},
        {HALO4_VEHI_REVENANT, 0xCC, 0x3C}, {HALO4_VEHI_REVENANT, 0xCC, 0x94}, {HALO4_VEHI_REVENANT, 0xCC, 0x98},
        {HALO4_VEHI_REVENANT, 0xCC, 0x9C},
    };
    Games::VehicleScaler<_countof(kVehicleSpeedFields)> g_vehicleSpeed;
    Games::VehicleGodState g_vehicleGod;
    Games::VehicleGodState g_vehicleNoEmp;
    Games::VehicleGodState g_vehicleReserved;  // cannot be hijacked: AI reservation
    // Infinite boost: boost burns no power.
    constexpr Games::TagValue kInfiniteBoost[] = {{HALO4_VEHI_BOOST_POWER_PER_SECOND, 0.0f}};
    Games::VehicleSetter<_countof(kInfiniteBoost)> g_infiniteBoost;
    // Cannot be hijacked: the seats of the vehicle player 1 is in (and of
    // what it's mounted on) are locked while they're in it.
    constexpr int kMaxLockedSeats = 32;
    Restorable<uint32_t> g_lockedSeats[kMaxLockedSeats];
    Restorable<int16_t> g_boardingIndex[kMaxLockedSeats];  // each seat's "boarding seat index"
    Restorable<float> g_gravity;
    Restorable<float> g_gameSpeed;
    Restorable<int32_t> g_killTriggerCount;
    Restorable<uint16_t> g_softCeilingFlags[kMaxSoftCeilings][2];
    Restorable<uint8_t> g_aiGrenades;
    bool g_hudHidden = false;  // we cleared the HUD's show bit
    Restorable<uint8_t> g_deaf[kMaxDeafActors];        // actors' flag byte
    Restorable<uint16_t> g_deafSquads[kMaxDeafActors];  // squads' flag word

    constexpr int kMaxAimAssistModes = 4;
    struct AimbotTag {
        char* tag = nullptr;
        Restorable<float> fields[4];  // angle, range, falloff, deviation
        Restorable<float> modes[kMaxAimAssistModes][4];  // the same in each aim assist mode
    };
    AimbotTag g_aimbot[kMaxAimbotTags];

    Restorable<uint32_t> g_ammo[kMaxAmmoBarrels];          // barrel projectile datums
    Restorable<uint32_t> g_recoil[kMaxRecoilEffects];      // firing effect damage datums
    Restorable<uint32_t> g_vehicleRecoil[kMaxRecoilEffects];  // the same, and rider responses, on vehicle guns
    Restorable<float> g_vehiclePush[kMaxRecoilEffects];       // firing effect accelerations on vehicle guns
    constexpr int kMaxRapidBarrels = 32;
    Restorable<float> g_rapidRate[kMaxRapidBarrels * 2];  // barrel rounds per second (min, max)
    Restorable<float> g_rapidRecovery[kMaxRapidBarrels];  // barrel fire recovery time

    ::Patch::Nop g_rapidFire;
    ::Patch::Nop g_fireTuning;  // ignore the TU weapon tuning's rate of fire
    ::Patch::Nop g_noHeat;
    ::Patch::Nop g_thirdPerson;

    // Read by the barrel-fire hook on the game thread.
    volatile bool g_aiCantShoot = false;
    volatile bool g_noEject = false;  // read by the seat-exit hook (game thread)

    // Pickable tags, rebuilt when the map changes.
    char* g_listsScenario = nullptr;
    std::vector<Games::TagChoice> g_weapons;
    std::vector<Games::TagChoice> g_projectiles;

    struct Frame {
        Array players;
        Array objects;
        Array actors;
        char* gameGlobals;
        char* gameTime;
        char* physics;
        char* aiGlobals;
        Array squads;
        char* chud;
        char* matg;
        char* scnr;
    };

    // --- Cheats -------------------------------------------------------------

    // Per-unit flag: set, or cleared once when switched off.
    // Per-unit flag: set, or cleared once when switched off. Units we flagged
    // that no longer belong to a player are cleared too.
    void UnitFlag(const Players& p, const Array& objects, int offset, uint32_t mask, bool on, FlagState& state) {
        for (uint32_t& d : state.units) {
            if (!d) continue;
            bool current = false;
            for (int i = 0; i < p.count; ++i) current |= p.datums[i] == d;
            if (current) continue;
            if (char* unit = ObjectFromDatum(objects, d)) Field<uint32_t>(unit, offset) &= ~mask;
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

    // Enemies can't see you: the player's own ai_disregard flag (unit +0x490
    // bit 0x20, what the ai_disregard script sets) and nothing else, so AI keep
    // seeing each other and your allies. The AI's perception loop (0x7CDFCC)
    // copies that bit of each perceived unit into the perceived-object record
    // (+0x31), and the target code drops records with it set (0x7BF38A,
    // 0x7C6E2E): the same shape as Reach's (0x73845F, 0x711FFE, 0x72E91E).
    // Put back only on units where the bit was clear before we set it.
    void Blind(const Players& p, bool on) {
        g_blindOn = on;
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            BlindState& b = g_blind[i];
            if (i >= p.count) {  // dead or gone: the new unit starts from the game's flags
                if (!on) b = {};
                continue;
            }
            auto& flags = Field<uint64_t>(p.units[i], HALO4_UNIT_FLAGS);
            if (b.unit != p.datums[i]) b = {p.datums[i], false};
            if (on) {
                if (!(flags & HALO4_UNIT_FLAG_AI_DISREGARD)) {
                    flags |= HALO4_UNIT_FLAG_AI_DISREGARD;
                    b.ours = true;
                }
            } else if (b.ours) {
                flags &= ~(uint64_t)HALO4_UNIT_FLAG_AI_DISREGARD;
                b.ours = false;
            }
        }
    }

    // Tops up the player's weapons from their weapon tag's magazine sizes.
    void Ammo(const Players& p, const Array& objects, bool infinite, bool bottomless) {
        if (!infinite && !bottomless) return;

        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                if (!weapon) continue;
                char* tag = Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM));
                int magazines = Halo4Tags::BlockCount(tag, HALO4_WEAP_MAGAZINES);

                // Energy weapons' battery is the weapon's age.
                Field<float>(weapon, HALO4_WEAPON_AGE) = 0.0f;

                for (int m = 0; m < magazines && m < 2; ++m) {
                    char* def = Halo4Tags::BlockElement(tag, HALO4_WEAP_MAGAZINES, HALO4_WEAP_MAGAZINE_DEF_SIZE, m);
                    if (!def) continue;
                    char* mag = weapon + HALO4_WEAPON_MAGAZINES + m * HALO4_WEAPON_MAGAZINE_SIZE;

                    int16_t loadedMax = Field<int16_t>(def, HALO4_MAGDEF_ROUNDS_LOADED_MAX);
                    int16_t totalMax = Field<int16_t>(def, HALO4_MAGDEF_ROUNDS_TOTAL_MAX);
                    auto& loaded = Field<int16_t>(mag, HALO4_MAGAZINE_LOADED);
                    auto& reserve = Field<int16_t>(mag, HALO4_MAGAZINE_RESERVE);

                    if (bottomless && loaded < loadedMax) loaded = loadedMax;
                    int16_t reserveMax = totalMax > loadedMax ? totalMax - loadedMax : totalMax;
                    if (infinite && reserve < reserveMax) reserve = reserveMax;
                }
            }
        }
    }

    // Halo 4 lists grenade types in the grenade globals tag (matg reference).
    void Grenades(const Players& p, char* matg, bool on) {
        if (!on || !matg) return;
        char* list = Halo4Tags::Get(Field<uint32_t>(matg, HALO4_MATG_GRENADE_GLOBALS + HALO4_TAGREF_DATUM));
        int types = Halo4Tags::BlockCount(list, HALO4_GRENADE_LIST);
        for (int t = 0; t < types && t < HALO4_UNIT_GRENADE_TYPES; ++t) {
            char* def = Halo4Tags::BlockElement(list, HALO4_GRENADE_LIST, HALO4_GRENADE_DEF_SIZE, t);
            if (!def) continue;
            int16_t max = Field<int16_t>(def, 0);
            for (int i = 0; i < p.count; ++i) {
                auto& count = Field<int8_t>(p.units[i], HALO4_UNIT_GRENADE_COUNTS + t);
                if (max > 0 && count < max) count = (int8_t)max;
            }
        }
    }

    // Leaves enemy AI with no shield and a sliver of health, so any hit kills.
    void OneHitKill(const Players& p, const Array& objects, bool on) {
        if (!on) return;
        for (int i = 0; i < objects.HighWater(); ++i) {
            char* entry = objects.Entry(i);
            int type = Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE);
            char* object = Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
            if (*(uint16_t*)entry == 0 || !object || !IsKillableEnemy(object, type, p)) continue;

            float* health = Health(object);
            if (!health || *health <= 0.0f) continue;  // dying
            if (float* shield = Shield(object)) *shield = 0.0f;
            if (*health > 0.01f) *health = 0.01f;
        }
    }

    void Speed(char* matg, float m) {
        char* info = Halo4Tags::BlockElement(matg, HALO4_MATG_PLAYER_INFORMATION, HALO4_MATG_PLAYER_INFO_SIZE, 0);
        if (!info) return;
        // Walk, run forward/back/side, run accel, sneak forward/back/side, sneak accel, air accel.
        for (int f = 0; f < 10; ++f)
            Scale(g_speed[f], (float*)(info + f * 4), m);
    }

    bool Boarding(char* unit, const Array& objects);

    // Vehicle god mode: the damage flag (all god mode uses here, as in Reach).
    // Not while a player is boarding (hijacking) a vehicle: with the flag the
    // Wraith's hatch couldn't be punched open (tested 2026-10-02).
    void VehicleGod(const Players& p, const Array& objects, bool on) {
        uint32_t seats[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i)
            seats[i] = Boarding(p.units[i], objects) ? 0xFFFFFFFF : Field<uint32_t>(p.units[i], HALO4_OBJECT_PARENT);
        g_vehicleGod.Update(
            seats, p.count, on, [&](uint32_t d) { return ObjectOfType(objects, d, HALO4_OBJECT_TYPE_VEHICLE); },
            [](char* o) { return Field<uint32_t>(o, HALO4_OBJECT_PARENT); },
            [](char* o) { return (Field<uint32_t>(o, HALO4_OBJECT_DAMAGE_FLAGS) & HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE) != 0; },
            [](char* o, bool god) {
                auto& flags = Field<uint32_t>(o, HALO4_OBJECT_DAMAGE_FLAGS);
                flags = god ? (uint32_t)(flags | HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE)
                            : (uint32_t)(flags & ~HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
            });
    }

    // Cannot be shut down: the "ignores EMP" damage flag on the vehicles the
    // players are in, and any stun already running is cleared.
    void VehicleNoEmp(const Players& p, const Array& objects, bool on) {
        uint32_t seats[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i) seats[i] = Field<uint32_t>(p.units[i], HALO4_OBJECT_PARENT);
        g_vehicleNoEmp.Update(
            seats, p.count, on, [&](uint32_t d) { return ObjectOfType(objects, d, HALO4_OBJECT_TYPE_VEHICLE); },
            [](char* o) { return Field<uint32_t>(o, HALO4_OBJECT_PARENT); },
            [](char* o) { return (Field<uint32_t>(o, HALO4_OBJECT_DAMAGE_FLAGS) & HALO4_DAMAGE_FLAG_IGNORES_EMP) != 0; },
            [](char* o, bool on) {
                auto& flags = Field<uint32_t>(o, HALO4_OBJECT_DAMAGE_FLAGS);
                flags = on ? (flags | HALO4_DAMAGE_FLAG_IGNORES_EMP) : (flags & ~HALO4_DAMAGE_FLAG_IGNORES_EMP);
                if (on && Field<int16_t>(o, HALO4_UNIT_EMP_STUN) > 0) Field<int16_t>(o, HALO4_UNIT_EMP_STUN) = 0;
            });
    }

    // Cannot be hijacked: every seat of the vehicle player 1 is in (and of
    // what it's mounted on) is set "Locked", and its boarding seats lose
    // their "boarding seat index" (the seat they take over), a tag edit, so
    // same-type vehicles too, only while you're in one. The vehicles are also
    // reserved so AI won't pick them. Put back when they get out.
    // Reach clears "Boarding Seat" instead; in Halo 4 that turned a Ghost's
    // side boarding seats into ordinary seats, and allied Elites rode on
    // them (tested 2026-10-02), so the flag is left alone here.
    // True while the unit sits in a boarding seat, i.e. is hijacking.
    bool Boarding(char* unit, const Array& objects) {
        char* vehicle = VehicleOf(unit, objects);
        char* tag = vehicle ? Halo4Tags::Get(Field<uint32_t>(vehicle, HALO4_OBJECT_TAG_DATUM)) : nullptr;
        char* seat = Halo4Tags::BlockElement(tag, HALO4_VEHI_SEATS, HALO4_VEHI_SEAT_SIZE,
                                             Field<int16_t>(unit, HALO4_OBJECT_PARENT_SEAT));
        if (!seat) return false;
        uint32_t* at = &Field<uint32_t>(seat, 0);
        uint32_t flags = *at;
        for (auto& r : g_lockedSeats)
            if (r.Ours(at)) flags = r.original;
        return (flags & HALO4_SEAT_FLAG_BOARDING_SEAT) != 0;
    }

    // Not while a player is boarding a vehicle themselves: locking the seat
    // they board through left them stuck mid-hijack (Reach and Halo 2, 2026-10-02).
    void VehicleNoHijack(const Players& p, const Array& objects, bool on) {
        uint32_t seatsOf[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i)
            seatsOf[i] = Boarding(p.units[i], objects) ? 0xFFFFFFFF : Field<uint32_t>(p.units[i], HALO4_OBJECT_PARENT);
        g_vehicleReserved.Update(
            seatsOf, p.count, on, [&](uint32_t d) { return ObjectOfType(objects, d, HALO4_OBJECT_TYPE_VEHICLE); },
            [](char* o) { return Field<uint32_t>(o, HALO4_OBJECT_PARENT); },
            [](char* o) { return Field<uint64_t>(o, HALO4_VEHICLE_AI_RESERVED) == ~0ull; },
            [](char* o, bool reserve) { Field<uint64_t>(o, HALO4_VEHICLE_AI_RESERVED) = reserve ? ~0ull : 0; });

        uint32_t* wanted[kMaxLockedSeats] = {};
        int16_t* indices[kMaxLockedSeats] = {};
        int count = 0;
        char* vehicle = on && p.count > 0 && !Boarding(p.units[0], objects) ? VehicleOf(p.units[0], objects) : nullptr;
        for (int depth = 0; vehicle && depth < 4; ++depth) {
            char* tag = Halo4Tags::Get(Field<uint32_t>(vehicle, HALO4_OBJECT_TAG_DATUM));
            int seats = Halo4Tags::BlockCount(tag, HALO4_VEHI_SEATS);
            for (int i = 0; i < seats && count < kMaxLockedSeats; ++i)
                if (char* seat = Halo4Tags::BlockElement(tag, HALO4_VEHI_SEATS, HALO4_VEHI_SEAT_SIZE, i)) {
                    indices[count] = &Field<int16_t>(seat, HALO4_SEAT_BOARDING_INDEX);
                    wanted[count++] = &Field<uint32_t>(seat, 0);
                }
            vehicle = VehicleOf(vehicle, objects);
        }
        for (auto& r : g_lockedSeats) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < count; ++i) keep |= wanted[i] == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < count; ++i) {
            Restorable<uint32_t>* slot = nullptr;
            for (auto& r : g_lockedSeats)
                if (r.addr == wanted[i]) { slot = &r; break; }
            for (auto& r : g_lockedSeats)
                if (!slot && !r.addr) slot = &r;
            if (!slot) continue;
            uint32_t base = slot->Ours(wanted[i]) ? slot->original : *wanted[i];
            slot->Set(wanted[i], base | HALO4_SEAT_FLAG_LOCKED);
            slot->original = base;
        }
        for (auto& r : g_boardingIndex) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < count; ++i) keep |= indices[i] == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < count; ++i) {
            if (*indices[i] == -1) continue;  // not a boarding seat
            Restorable<int16_t>* slot = nullptr;
            for (auto& r : g_boardingIndex)
                if (r.addr == indices[i]) { slot = &r; break; }
            for (auto& r : g_boardingIndex)
                if (!slot && !r.addr) slot = &r;
            if (slot) slot->Set(indices[i], -1);
        }
    }

    // Vehicle speed: the tag of the vehicle player 1 is in (same-type vehicles too).
    void VehicleSpeed(const Players& p, const Array& objects, float m) {
        char* vehicle = p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        char* tag = vehicle ? Halo4Tags::Get(Field<uint32_t>(vehicle, HALO4_OBJECT_TAG_DATUM)) : nullptr;
        g_vehicleSpeed.Update(tag, nullptr, 0, kVehicleSpeedFields, _countof(kVehicleSpeedFields), m,
                              [](char* t, int block, int size) { return Halo4Tags::BlockElement(t, block, size, 0); });
    }

    void Jump(const Players& p, float m) {
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            char* tag = i < p.count ? Halo4Tags::Get(Field<uint32_t>(p.units[i], HALO4_OBJECT_TAG_DATUM)) : nullptr;
            if (tag) Scale(g_jump[i], &Field<float>(tag, HALO4_BIPD_JUMP_VELOCITY), m);
        }
    }

    void NoBoundaries(char* scnr, bool on) {
        if (!scnr) return;

        auto* killCount = &Field<int32_t>(scnr, HALO4_SCNR_KILL_TRIGGERS);
        if (on) g_killTriggerCount.Set(killCount, 0);
        else g_killTriggerCount.Restore();

        int ceilings = Halo4Tags::BlockCount(scnr, HALO4_SCNR_SOFT_CEILINGS);
        for (int i = 0; i < ceilings && i < kMaxSoftCeilings; ++i) {
            char* ceiling = Halo4Tags::BlockElement(scnr, HALO4_SCNR_SOFT_CEILINGS, HALO4_SCNR_SOFT_CEILING_SIZE, i);
            if (!ceiling) continue;
            for (int f = 0; f < 2; ++f) {  // flags, runtime flags
                auto* flags = &Field<uint16_t>(ceiling, f * 2);
                Restorable<uint16_t>& patch = g_softCeilingFlags[i][f];
                if (on) {
                    uint16_t base = patch.Ours(flags) ? patch.original : *flags;
                    patch.Set(flags, base | HALO4_SOFT_CEILING_IGNORE_ALL);
                    patch.original = base;
                } else if (patch.addr) {
                    patch.Restore();
                }
            }
        }
    }

    // Maxes out the built-in autoaim on the player's weapons, so shots bend
    // onto any enemy in view: the weapon's own fields and each of its aim
    // assist modes (which Halo 4 aims with).
    void Aimbot(const Players& p, const Array& objects, bool on) {
        if (!on) {
            for (auto& a : g_aimbot) {
                for (auto& f : a.fields) f.Restore();
                for (auto& mode : a.modes)
                    for (auto& f : mode) f.Restore();
                a.tag = nullptr;
            }
            return;
        }

        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
                if (!tag || IsMeleeOnly(weapon)) continue;  // melee weapons keep their own aim

                AimbotTag* slot = nullptr;
                for (auto& a : g_aimbot)
                    if (a.tag == tag) { slot = &a; break; }
                for (auto& a : g_aimbot)
                    if (!slot && !a.tag) { slot = &a; slot->tag = tag; }
                if (!slot) continue;

                slot->fields[0].Set(&Field<float>(tag, HALO4_WEAP_AUTOAIM_ANGLE), kPi);
                slot->fields[1].Set(&Field<float>(tag, HALO4_WEAP_AUTOAIM_RANGE), 1000.0f);
                slot->fields[2].Set(&Field<float>(tag, HALO4_WEAP_AUTOAIM_FALLOFF), 1000.0f);
                slot->fields[3].Set(&Field<float>(tag, HALO4_WEAP_DEVIATION_ANGLE), kPi);
                for (int m = 0; m < kMaxAimAssistModes; ++m) {
                    char* mode = Halo4Tags::BlockElement(tag, HALO4_WEAP_AIM_ASSIST_MODES, HALO4_WEAP_AIM_ASSIST_SIZE, m);
                    if (!mode) break;
                    slot->modes[m][0].Set(&Field<float>(mode, HALO4_AIM_ASSIST_ANGLE), kPi);
                    slot->modes[m][1].Set(&Field<float>(mode, HALO4_AIM_ASSIST_RANGE), 1000.0f);
                    slot->modes[m][2].Set(&Field<float>(mode, HALO4_AIM_ASSIST_FALLOFF), 1000.0f);
                    slot->modes[m][3].Set(&Field<float>(mode, HALO4_AIM_ASSIST_DEVIATION), kPi);
                }
            }
        }
    }

    bool InList(const std::vector<Games::TagChoice>& list, uint32_t datum) {
        for (const auto& c : list)
            if (c.datum == datum) return true;
        return false;
    }

    // Sets each wanted field (tag datums, floats), and restores the ones no longer wanted.
    template <typename T, size_t N>
    void SetValues(Restorable<T> (&patches)[N], T* const* wanted, int wantedCount, T value) {
        for (auto& patch : patches) {
            if (!patch.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i] == patch.addr;
            if (!keep) patch.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<T>* slot = nullptr;
            for (auto& patch : patches)
                if (patch.addr == wanted[i]) { slot = &patch; break; }
            for (auto& patch : patches)
                if (!slot && !patch.addr) slot = &patch;
            if (slot) slot->Set(wanted[i], value);
        }
    }

    // Points player 1's held weapon's barrels at the chosen projectile. Other
    // weapons go back to normal.
    void AmmoType(const Players& p, const Array& objects, uint32_t projectile) {
        uint32_t* wanted[kMaxAmmoBarrels] = {};
        int wantedCount = 0;

        char* weapon = p.count > 0 && projectile != 0xFFFFFFFF && InList(g_projectiles, projectile)
                           ? HeldWeapon(p.units[0], objects) : nullptr;
        char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
        int barrels = Halo4Tags::BlockCount(tag, HALO4_WEAP_BARRELS);
        for (int b = 0; b < barrels && wantedCount < kMaxAmmoBarrels; ++b) {
            char* barrel = Halo4Tags::BlockElement(tag, HALO4_WEAP_BARRELS, HALO4_WEAP_BARREL_SIZE, b);
            if (barrel) wanted[wantedCount++] = &Field<uint32_t>(barrel, HALO4_BARREL_PROJECTILE + HALO4_TAGREF_DATUM);
        }
        SetValues(g_ammo, wanted, wantedCount, projectile);
    }

    // No recoil: as in Reach, the view kick comes from each barrel's firing damage
    // effect. Clears it on the players' weapons; other weapons go back.
    void NoRecoil(const Players& p, const Array& objects, bool on) {
        uint32_t* wanted[kMaxRecoilEffects] = {};
        int wantedCount = 0;
        for (int i = 0; on && i < p.count; ++i) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
                int barrels = Halo4Tags::BlockCount(tag, HALO4_WEAP_BARRELS);
                for (int b = 0; b < barrels; ++b) {
                    char* barrel = Halo4Tags::BlockElement(tag, HALO4_WEAP_BARRELS, HALO4_WEAP_BARREL_SIZE, b);
                    int effects = Halo4Tags::BlockCount(barrel, HALO4_BARREL_FIRING_EFFECTS);
                    for (int e = 0; e < effects && wantedCount < kMaxRecoilEffects; ++e) {
                        char* effect = Halo4Tags::BlockElement(barrel, HALO4_BARREL_FIRING_EFFECTS,
                                                               HALO4_FIRING_EFFECT_SIZE, e);
                        if (effect)
                            wanted[wantedCount++] = &Field<uint32_t>(effect, HALO4_FIRING_EFFECT_DAMAGE + HALO4_TAGREF_DATUM);
                    }
                }
            }
        }
        SetValues(g_recoil, wanted, wantedCount, 0xFFFFFFFF);
    }

    // The acceleration fields of an effect tag's events, appended to `out`.
    void EffectAccelerations(uint32_t effect, float** out, int& count) {
        char* tag = Halo4Tags::Get(effect);
        int events = Halo4Tags::BlockCount(tag, HALO4_EFFE_EVENTS);
        for (int e = 0; e < events; ++e) {
            char* event = Halo4Tags::BlockElement(tag, HALO4_EFFE_EVENTS, HALO4_EFFE_EVENT_SIZE, e);
            int accelerations = Halo4Tags::BlockCount(event, HALO4_EFFE_EVENT_ACCELERATIONS);
            for (int a = 0; a < accelerations && count < kMaxRecoilEffects; ++a) {
                char* acceleration = Halo4Tags::BlockElement(event, HALO4_EFFE_EVENT_ACCELERATIONS,
                                                             HALO4_EFFE_ACCELERATION_SIZE, a);
                if (acceleration) out[count++] = &Field<float>(acceleration, HALO4_EFFE_ACCELERATION);
            }
        }
    }

    // No vehicle recoil: the same for the guns of the vehicle (or turret)
    // each player is in, plus the rider responses on those firing effects
    // (the camera jolt) and the firing effects' accelerations (the push back).
    void VehicleNoRecoil(const Players& p, const Array& objects, bool on) {
        constexpr int kFields[] = {HALO4_FIRING_EFFECT_DAMAGE, HALO4_FIRING_EFFECT_SECONDARY_DAMAGE,
                                   HALO4_FIRING_EFFECT_RIDER_RESPONSE, HALO4_FIRING_EFFECT_SECONDARY_RIDER_DAMAGE};
        uint32_t* wanted[kMaxRecoilEffects] = {};
        int wantedCount = 0;
        float* pushes[kMaxRecoilEffects] = {};
        int pushCount = 0;
        for (int i = 0; on && i < p.count; ++i) {
            char* parts[16];
            int partCount = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < partCount; ++k) {
                for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                    char* weapon = WeaponInSlot(parts[k], objects, w);
                    char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
                    int barrels = Halo4Tags::BlockCount(tag, HALO4_WEAP_BARRELS);
                    for (int b = 0; b < barrels; ++b) {
                        char* barrel = Halo4Tags::BlockElement(tag, HALO4_WEAP_BARRELS, HALO4_WEAP_BARREL_SIZE, b);
                        int effects = Halo4Tags::BlockCount(barrel, HALO4_BARREL_FIRING_EFFECTS);
                        for (int e = 0; e < effects; ++e) {
                            char* effect = Halo4Tags::BlockElement(barrel, HALO4_BARREL_FIRING_EFFECTS,
                                                                   HALO4_FIRING_EFFECT_SIZE, e);
                            if (!effect) continue;
                            for (int field : kFields)
                                if (wantedCount < kMaxRecoilEffects)
                                    wanted[wantedCount++] = &Field<uint32_t>(effect, field + HALO4_TAGREF_DATUM);
                            for (int field : {HALO4_FIRING_EFFECT_EFFECT, HALO4_FIRING_EFFECT_SECONDARY_EFFECT})
                                EffectAccelerations(Field<uint32_t>(effect, field + HALO4_TAGREF_DATUM), pushes, pushCount);
                        }
                    }
                }
            }
        }
        SetValues(g_vehicleRecoil, wanted, wantedCount, 0xFFFFFFFF);
        SetValues(g_vehiclePush, pushes, pushCount, 0.0f);
    }

    // AI can't shoot: the barrel-fire hook (see WeaponMayFire) plus the
    // global "AI throw grenades" switch.
    void AiCantShoot(char* aiGlobals, bool on) {
        g_aiCantShoot = on;
        if (!aiGlobals) return;
        auto* grenades = &Field<uint8_t>(aiGlobals, HALO4_AI_GLOBALS_GRENADES);
        if (on) g_aiGrenades.Set(grenades, 0);
        else g_aiGrenades.Restore();
    }

    // Sets `bit` in a flags field, keeping what it was before we set it (the
    // game may change the field's other bits meanwhile).
    template <typename T>
    void SetBit(Restorable<T>& r, T* at, T bit) {
        T base = r.Ours(at) ? r.original : r.addr == at ? (T)((*at & ~bit) | (r.original & bit)) : *at;
        r.Set(at, (T)(base | bit));
        r.original = base;
    }

    // AI can't hear: the "deaf" flags ai_set_deaf sets, on every actor and
    // every squad (it sets one or the other, depending on what it's given).
    void AiDeaf(const Array& actors, const Array& squads, bool on) {
        int count = on && actors.Valid() ? actors.HighWater() : 0;
        for (int i = 0; i < kMaxDeafActors; ++i) {
            char* actor = i < count ? actors.Entry(i) : nullptr;
            if (actor && *(uint16_t*)actor != 0) {
                SetBit<uint8_t>(g_deaf[i], &Field<uint8_t>(actor, HALO4_ACTOR_FLAGS), HALO4_ACTOR_FLAG_DEAF);
            } else {
                g_deaf[i].Restore();
            }
        }
        count = on && squads.Valid() ? squads.HighWater() : 0;
        for (int i = 0; i < kMaxDeafActors; ++i) {
            char* squad = i < count ? squads.Entry(i) : nullptr;
            if (squad && *(uint16_t*)squad != 0) {
                SetBit<uint16_t>(g_deafSquads[i], &Field<uint16_t>(squad, HALO4_SQUAD_FLAGS), HALO4_SQUAD_FLAG_DEAF);
            } else {
                g_deafSquads[i].Restore();
            }
        }
    }

    // No HUD: clears the bit chud_show sets (the whole HUD; checked live).
    // Only that bit is touched, so scripts' other HUD bits stay theirs.
    void NoHud(char* chud, bool on) {
        if (!chud) return;
        auto& mask = Field<uint32_t>(chud, HALO4_CHUD_SHOW_MASK);
        if (on) {
            mask &= ~HALO4_CHUD_SHOW_BIT;
            g_hudHidden = true;
        } else if (g_hudHidden) {
            mask |= HALO4_CHUD_SHOW_BIT;
            g_hudHidden = false;
        }
    }

    // Sprint is a hidden equipment object ("hero_assist") parented to the
    // unit but not in its armor ability slot; its energy is the stamina.
    void InfiniteSprint(const Players& p, const Array& objects, bool on) {
        if (!on) return;
        for (int i = 0; i < objects.HighWater(); ++i) {
            char* entry = objects.Entry(i);
            char* object = Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
            if (*(uint16_t*)entry == 0 || !object || Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE) != HALO4_OBJECT_TYPE_EQUIPMENT) continue;
            uint32_t parent = Field<uint32_t>(object, HALO4_OBJECT_PARENT);
            for (int k = 0; k < p.count; ++k)
                if (parent == p.datums[k]) Field<float>(object, HALO4_EQUIPMENT_ENERGY) = 1.0f;
        }
    }

    // Keeps the players' armor abilities fully charged.
    void InfiniteEquipment(const Players& p, const Array& objects, bool on) {
        if (!on) return;
        for (int i = 0; i < p.count; ++i) {
            char* equipment = ObjectOfType(objects, Field<uint32_t>(p.units[i], HALO4_UNIT_EQUIPMENT),
                                           HALO4_OBJECT_TYPE_EQUIPMENT);
            if (equipment) Field<float>(equipment, HALO4_EQUIPMENT_ENERGY) = 1.0f;
        }
    }

    // Weapon state edits, player weapons only: the trigger always counts as
    // released (full-auto), no spread, no heat. Rapid fire is a code patch.
    // Rapid fire, part two: the patch only skips the delay after each shot,
    // which is what single-shot weapons wait on (rockets). Automatic weapons
    // (the SAW, the AR) fire at their barrels' rounds per second instead, so
    // that's raised to one a tick on the players' weapons (and the vehicle's
    // with vehicle rapid fire), and fire recovery is cleared. A tag edit:
    // same-type weapons held by AI fire fast too, as with the patch.
    void RapidFireTags(const Players& p, const Array& objects, bool weapons, bool vehicles) {
        float* rates[kMaxRapidBarrels * 2] = {};
        float* recoveries[kMaxRapidBarrels] = {};
        int rateCount = 0, recoveryCount = 0;
        auto add = [&](char* holder) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(holder, objects, w);
                char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
                int barrels = Halo4Tags::BlockCount(tag, HALO4_WEAP_BARRELS);
                for (int b = 0; b < barrels && recoveryCount < kMaxRapidBarrels; ++b) {
                    char* barrel = Halo4Tags::BlockElement(tag, HALO4_WEAP_BARRELS, HALO4_WEAP_BARREL_SIZE, b);
                    if (!barrel) continue;
                    rates[rateCount++] = &Field<float>(barrel, HALO4_BARREL_ROUNDS_PER_SECOND);
                    rates[rateCount++] = &Field<float>(barrel, HALO4_BARREL_ROUNDS_PER_SECOND + 4);
                    recoveries[recoveryCount++] = &Field<float>(barrel, HALO4_BARREL_FIRE_RECOVERY);
                }
            }
        };
        for (int i = 0; i < p.count; ++i) {
            if (weapons) add(p.units[i]);
            if (!vehicles) continue;
            char* parts[16];
            int partCount = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < partCount; ++k) add(parts[k]);
        }
        SetValues(g_rapidRate, rates, rateCount, HALO4_RAPID_FIRE_ROUNDS_PER_SECOND);
        SetValues(g_rapidRecovery, recoveries, recoveryCount, 0.0f);
    }

    void WeaponMods(const Players& p, const Array& objects, const Games::Settings& s) {
        RapidFireTags(p, objects, s.rapidFire, s.vehicleRapidFire);
        // These patch the shared weapon code, so they cover vehicle weapons too (and the AI's).
        g_rapidFire.Apply(s.rapidFire || s.vehicleRapidFire);
        g_fireTuning.Apply(s.rapidFire || s.vehicleRapidFire);
        bool vehicleFullAuto = s.fullAuto || s.vehicleRapidFire;
        if (!s.fullAuto && !s.noSpread && !s.noOverheat && !vehicleFullAuto) return;

        // No overheat and full auto also cover the vehicle (or turret) each
        // player is in; full auto there is what lets the Sabre's missiles
        // fire while the trigger is held. Vehicle rapid fire turns it on too.
        for (int i = 0; (s.noOverheat || vehicleFullAuto) && i < p.count; ++i) {
            char* parts[16];
            int partCount = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < partCount; ++k)
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(parts[k], objects, w);
                if (!weapon) continue;
                if (s.noOverheat) Field<float>(weapon, HALO4_WEAPON_HEAT) = 0.0f;
                if (vehicleFullAuto)
                    for (int t = 0; t < HALO4_WEAPON_TRIGGER_COUNT; ++t)
                        Field<uint8_t>(weapon, HALO4_WEAPON_TRIGGERS + t * HALO4_WEAPON_TRIGGER_SIZE +
                                                   HALO4_TRIGGER_FLAGS) |= HALO4_TRIGGER_FLAG_READY;
            }
        }

        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                if (!weapon) continue;
                if (s.fullAuto)
                    for (int t = 0; t < HALO4_WEAPON_TRIGGER_COUNT; ++t)
                        Field<uint8_t>(weapon, HALO4_WEAPON_TRIGGERS + t * HALO4_WEAPON_TRIGGER_SIZE +
                                                   HALO4_TRIGGER_FLAGS) |= HALO4_TRIGGER_FLAG_READY;
                if (s.noSpread)
                    for (int b = 0; b < HALO4_WEAPON_BARREL_COUNT; ++b)
                        Field<float>(weapon, HALO4_WEAPON_BARRELS + b * HALO4_WEAPON_BARREL_SIZE + HALO4_BARREL_ERROR) = 0.0f;
                if (s.noOverheat) Field<float>(weapon, HALO4_WEAPON_HEAT) = 0.0f;
            }
        }
    }

    // Weapons flagged "Cannot Be Used By Player". Halo 4's vehicle and
    // turret guns, enemy heads and armor-ability turrets don't have the flag
    // (checked live in Midnight), so those are left out by their tag path.
    bool FlaggedNotForPlayer(uint32_t weaponTag) {
        __try {
            char* tag = Halo4Tags::Get(weaponTag);
            return !tag || (Field<uint32_t>(tag, HALO4_WEAP_FLAGS) & HALO4_WEAP_FLAG_NOT_FOR_PLAYER);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return true;
        }
    }

    bool PlayerCanUse(uint32_t weaponTag) {
        if (FlaggedNotForPlayer(weaponTag)) return false;
        std::string path = Halo4Tags::FullName(weaponTag);
        for (const char* skip : {"\\vehicles\\", "\\characters\\", "\\equipment\\", "\\turrets\\",
                                 "\\unarmed\\"})
            if (path.find(skip) != std::string::npos) return false;
        return true;
    }

    void RefreshLists(char* scnr) {
        if (scnr == g_listsScenario) return;
        g_listsScenario = scnr;
        Halo4Teams::Reset();
        Halo4Extras::Reset();
        Halo4Checkpoints::Reset();
        Halo4Fun::Reset();
        Halo4Equipment::Reset();
        Halo4Vehicles::Reset();
        Halo4Upgrade::Reset();
        Halo4Tags::LoadNames();
        g_weapons.clear();
        for (auto& weapon : Halo4Tags::ListGroup(HALO4_TAG_WEAP))
            if (PlayerCanUse(weapon.datum)) g_weapons.push_back(weapon);
        g_projectiles = Halo4Tags::ListGroup(HALO4_TAG_PROJ);
        LOG("Halo 4: map has %zu weapons, %zu projectiles", g_weapons.size(), g_projectiles.size());
        if (scnr) Halo4Spawn::RefreshLists(scnr);
        Games::Status lists;
        Halo4Spawn::FillStatus(lists);
        Fun::SetPools(lists.spawnVehicles, lists.spawnObjects, lists.spawnAi, &g_weapons, &g_projectiles,
                      [](uint32_t tag) {
                          uint32_t group = Halo4Tags::GroupOf(tag);
                          return group != HALO4_TAG_SCEN && group != HALO4_TAG_MACH;
                      });
    }

    // Reads the pointers the cheats need. SEH: they can be mid-teardown.
    bool ReadFrame(void** state, Frame& f) {
        __try {
            f.players.header = (char*)state[HALO4_TLS_SLOT_PLAYERS];
            f.objects.header = (char*)state[HALO4_TLS_SLOT_OBJECTS];
            f.actors.header = (char*)state[HALO4_TLS_SLOT_ACTORS];
            f.gameGlobals = (char*)state[HALO4_TLS_SLOT_GAME_GLOBALS];
            f.gameTime = (char*)state[HALO4_TLS_SLOT_GAME_TIME];
            f.physics = (char*)state[HALO4_TLS_SLOT_PHYSICS];
            f.aiGlobals = (char*)state[HALO4_TLS_SLOT_AI_GLOBALS];
            f.squads.header = (char*)state[HALO4_TLS_SLOT_SQUADS];
            f.chud = g_halo4 ? *(char**)(g_halo4 + OFFSET_HALO4_CHUD_GLOBALS) : nullptr;
            f.matg = Halo4Tags::Globals();
            f.scnr = Halo4Tags::Scenario();
            return f.players.Valid() && f.objects.Valid() && f.gameGlobals;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Each group gets its own guard, so a bad offset in one can't stop the others.
    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    bool ApplyAll(const Frame& f, const Games::Settings& s, Games::Status& status) {
        Players p;
        __try {
            p = FindPlayers(f.players, f.objects);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        Guard("god mode", [&] {
            UnitFlag(p, f.objects, HALO4_OBJECT_DAMAGE_FLAGS, HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE, s.godMode, g_godApplied);
        });
        Guard("ai can't see you", [&] { Blind(p, s.aiBlind); });
        Guard("ammo", [&] { Ammo(p, f.objects, s.infiniteAmmo, s.bottomlessClip); });
        Guard("grenades", [&] { Grenades(p, f.matg, s.infiniteGrenades); });
        Guard("one-hit kill", [&] { OneHitKill(p, f.objects, s.oneHitKill); });
        Guard("speed", [&] { if (f.matg) Speed(f.matg, s.speed); });
        Guard("jump", [&] { Jump(p, s.jump); });
        Guard("vehicle speed", [&] { VehicleSpeed(p, f.objects, s.vehicleSpeed); });
        Guard("vehicle god mode", [&] { VehicleGod(p, f.objects, s.vehicleGodMode); });
        Guard("vehicle no EMP", [&] { VehicleNoEmp(p, f.objects, s.vehicleNoEmp); });
        Guard("vehicle no hijack", [&] { VehicleNoHijack(p, f.objects, s.vehicleNoHijack); });
        Guard("vehicle infinite boost", [&] {
            char* vehicle = p.count > 0 ? VehicleOf(p.units[0], f.objects) : nullptr;
            char* tag = vehicle ? Halo4Tags::Get(Field<uint32_t>(vehicle, HALO4_OBJECT_TAG_DATUM)) : nullptr;
            g_infiniteBoost.Update(tag, kInfiniteBoost, _countof(kInfiniteBoost), s.vehicleInfiniteBoost);
        });
        Guard("gravity", [&] {
            if (f.physics) Scale(g_gravity, &Field<float>(f.physics, HALO4_PHYSICS_GRAVITY), s.gravity);
        });
        Guard("game speed", [&] {
            if (f.gameTime) Scale(g_gameSpeed, &Field<float>(f.gameTime, HALO4_GAME_TIME_SPEED), s.gameSpeed);
        });
        Guard("boundaries", [&] { NoBoundaries(f.scnr, s.noBoundaries); });
        Guard("aimbot", [&] { Aimbot(p, f.objects, s.aimbot); });
        Guard("ammo type", [&] { AmmoType(p, f.objects, s.ammoType); });
        Guard("no recoil", [&] { NoRecoil(p, f.objects, s.noRecoil); });
        Guard("vehicle no recoil", [&] { VehicleNoRecoil(p, f.objects, s.vehicleNoRecoil); });
        Guard("ai can't shoot", [&] { AiCantShoot(f.aiGlobals, s.aiCantShoot); });
        Guard("ai deaf", [&] { AiDeaf(f.actors, f.squads, s.aiDeaf); });
        Guard("no hud", [&] { NoHud(f.chud, s.noHud); });
        Guard("equipment", [&] { InfiniteEquipment(p, f.objects, s.infiniteEquipment); });
        Guard("infinite sprint", [&] { InfiniteSprint(p, f.objects, s.infiniteSprint); });
        Guard("weapon mods", [&] { WeaponMods(p, f.objects, s); });
        g_thirdPerson.Apply(s.thirdPerson);
        g_noHeat.Apply(s.noOverheat);  // covers vehicle guns the heat reset misses (and the AI's)

        status.playerCount = p.count;
        Guard("status", [&] {
            if (p.count == 0) return;
            if (float* health = Health(p.units[0])) status.health = *health;
            if (float* shield = Shield(p.units[0])) status.shield = *shield;
        });
        return true;
    }

    bool InitPatch(::Patch::Nop& patch, HMODULE halo4, const char* pattern, size_t size, int offset = 0,
                   const uint8_t* bytes = nullptr) {
        uint8_t* at = ::Patch::FindUnique(halo4, pattern);
        if (!at) return false;
        return bytes ? patch.Init(at + offset, bytes, size) : patch.Init(at + offset, size);
    }
}

void Halo4Cheats::Load(HMODULE halo4) {
    SetMainThreadModule(halo4);
    Halo4Spawn::SetModule(halo4);
    g_halo4 = (char*)halo4;
    bool rapid = InitPatch(g_rapidFire, halo4, HALO4_AOB_RAPID_FIRE, 7);
    bool third = InitPatch(g_thirdPerson, halo4, HALO4_AOB_THIRD_PERSON, 2);
    bool heat = InitPatch(g_noHeat, halo4, HALO4_AOB_NO_HEAT, 8);
    const uint8_t tuningBytes[] = HALO4_FIRE_TUNING_PATCH;
    if (!InitPatch(g_fireTuning, halo4, HALO4_AOB_FIRE_TUNING, sizeof(tuningBytes), HALO4_FIRE_TUNING_OFFSET,
                   tuningBytes))
        LOG("Halo 4: weapon-tuning patch NOT FOUND (rapid fire won't speed up tuned weapons like the AR)");
    LOG("Halo 4: rapid-fire patch %s, third-person patch %s, no-heat patch %s", rapid ? "found" : "NOT FOUND",
        third ? "found" : "NOT FOUND", heat ? "found" : "NOT FOUND");
    {  // Knights' auto turrets: see TeamsHostileDetour
        void* target = (char*)halo4 + OFFSET_HALO4_TEAMS_HOSTILE;
        if (MH_CreateHook(target, (void*)&TeamsHostileDetour, (void**)&g_origTeamsHostile) == MH_OK &&
            MH_EnableHook(target) == MH_OK) {
            g_teamsHostileHook = target;
        } else {
            MH_RemoveHook(target);
            LOG("Halo 4: failed to hook the team test at %p (auto turrets will still see you)", target);
        }
    }
    Halo4Teams::Load((char*)halo4);
    Halo4Extras::Load((char*)halo4);
    Halo4Checkpoints::Load((char*)halo4);
    Halo4Fun::Load((char*)halo4);
    Halo4Equipment::Load((char*)halo4);
    Halo4Vehicles::Load((char*)halo4);
}

void Halo4Cheats::Unload() {
    ::Patch::Nop* patches[] = {&g_rapidFire, &g_fireTuning, &g_thirdPerson, &g_noHeat};
    for (auto* patch : patches) {
        patch->Apply(false);
        patch->Forget();
    }
    g_aiCantShoot = false;
    if (g_teamsHostileHook) {
        MH_DisableHook(g_teamsHostileHook);
        MH_RemoveHook(g_teamsHostileHook);
        g_teamsHostileHook = nullptr;
    }
    g_blindOn = false;

    // The game's memory goes with the module: forget, don't restore.
    g_godApplied = FlagState();
    for (auto& b : g_blind) b = {};
    for (auto& r : g_speed) r = {};
    for (auto& r : g_jump) r = {};
    g_vehicleSpeed.Forget();
    g_vehicleGod.Forget();
    g_vehicleNoEmp.Forget();
    g_vehicleReserved.Forget();
    g_infiniteBoost.Forget();
    for (auto& r : g_lockedSeats) r = {};
    for (auto& r : g_boardingIndex) r = {};
    g_gravity = {};
    g_gameSpeed = {};
    g_killTriggerCount = {};
    for (auto& row : g_softCeilingFlags)
        for (auto& r : row) r = {};
    g_aiGrenades = {};
    g_hudHidden = false;
    for (auto& r : g_deaf) r = {};
    for (auto& r : g_deafSquads) r = {};
    for (auto& a : g_aimbot) a = {};
    for (auto& r : g_ammo) r = {};
    for (auto& r : g_recoil) r = {};
    for (auto& r : g_vehicleRecoil) r = {};
    for (auto& r : g_vehiclePush) r = {};
    for (auto& r : g_rapidRate) r = {};
    for (auto& r : g_rapidRecovery) r = {};
    g_listsScenario = nullptr;
    g_weapons.clear();
    g_projectiles.clear();
    ResetMainThread();
    Halo4Vehicles::Unload();
    Halo4Equipment::Unload();
    Halo4Fun::Unload();
    Halo4Checkpoints::Unload();
    Halo4Extras::Unload();
    Halo4Teams::Unload();
    Halo4Upgrade::Reset();
    Halo4Spawn::Reset();
    Halo4Spawn::SetModule(nullptr);
    SetMainThreadModule(nullptr);
    g_halo4 = nullptr;
}

bool Halo4Cheats::Apply(void** gameState, const Games::Settings& settings, Games::Status& status) {
    g_noEject = settings.vehicleNoEject;
    Fun::SetSettings(settings);
    status.skullNames = kSkullNames;
    status.skullCount = HALO4_SKULL_COUNT;
    Frame f{};
    if (!ReadFrame(gameState, f)) return false;
    Guard("skulls", [&] { status.skullsActive = Field<uint64_t>(f.gameGlobals, HALO4_GAME_GLOBALS_SKULLS); });

    Guard("tag lists", [&] { RefreshLists(f.scnr); });
    status.weapons = &g_weapons;
    status.projectiles = &g_projectiles;
    Halo4Spawn::FillStatus(status);
    Halo4Upgrade::Apply(settings, status);
    status.upgradeEnemiesApplicable = Halo4Upgrade::UpgradeApplicable(false);
    status.upgradeAlliesApplicable = Halo4Upgrade::UpgradeApplicable(true);
    Halo4Checkpoints::Apply(settings, status);
    Halo4Equipment::Apply(settings, status);

    return ApplyAll(f, settings, status);
}

// A weapon may fire unless AI can't shoot is on and it isn't held by a local
// player or by a vehicle a player is in (so turrets the player uses still work).
// The flip check's eject action is dropped for players while "Cannot be
// ejected" is on (the hook only asks for that one call site).
bool Halo4Cheats::FlipEjectAllowed(void** state, uint32_t unit) {
    if (!g_noEject || !state) return true;
    __try {
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        if (!objects.Valid()) return true;
        char* object = ObjectFromDatum(objects, unit);
        if (!object || Field<uint32_t>(object, HALO4_UNIT_PLAYER) == 0xFFFFFFFF) return true;
        static int s_logged = 0;
        if (s_logged++ < 3) LOG("Halo 4: kept player unit %08X in its flipped vehicle", unit);
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

namespace {
    // Whose weapon this is: the local player holding it, or seated in the
    // vehicle (or on the turret) it belongs to. -1 for anyone else.
    int ShooterOf(const Array& objects, const Players& p, uint32_t weapon, bool* vehicleGun) {
        char* object = ObjectFromDatum(objects, weapon);
        uint32_t holder = object ? Field<uint32_t>(object, HALO4_OBJECT_PARENT) : 0xFFFFFFFF;
        char* holderObject = ObjectFromDatum(objects, holder);
        if (!holderObject) return -1;
        for (int i = 0; i < p.count; ++i) {
            if (p.datums[i] == holder) { *vehicleGun = false; return i; }
            char* parts[16];
            int count = VehicleParts(p.units[i], objects, parts, _countof(parts));
            for (int k = 0; k < count; ++k)
                if (parts[k] == holderObject) { *vehicleGun = true; return i; }
        }
        return -1;
    }
}

// Recoil push, from barrel fire (once per shot). Halo 4's barrel fire only
// marks the barrel as fired; the projectile is made later and looks its tag
// up through 0x6826E0, so the chaos gun is ChaosProjectileFor instead.
void Halo4Cheats::BeforeShot(void** state, uint32_t weapon, int16_t, Fun::Shot& shot) {
    if (!Fun::Active() || !state) return;
    __try {
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
        if (!objects.Valid() || !players.Valid()) return;
        Players p = FindPlayers(players, objects);
        bool vehicleGun = false;
        int shooter = ShooterOf(objects, p, weapon, &vehicleGun);
        if (shooter < 0) return;

        char* control = (char*)state[HALO4_TLS_SLOT_PLAYER_CONTROL] + shooter * HALO4_PLAYER_CONTROL_SIZE;
        float yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        // Seated, the whole vehicle moves: the top vehicle above the shooter.
        uint32_t target = p.datums[shooter];
        for (int depth = 0; depth < 5; ++depth) {
            char* at = ObjectFromDatum(objects, target);
            uint32_t parent = at ? Field<uint32_t>(at, HALO4_OBJECT_PARENT) : 0xFFFFFFFF;
            if (!ObjectOfType(objects, parent, HALO4_OBJECT_TYPE_VEHICLE)) break;
            target = parent;
        }
        if (target == p.datums[shooter] && vehicleGun) target = 0xFFFFFFFF;
        Fun::BeforeShot(nullptr, 0, target, dir, shot);
    } __except (Log::Exception(GetExceptionInformation(), "Halo 4 shot effects")) {
    }
}

uint32_t Halo4Cheats::ChaosProjectileFor(void** state, uint32_t weapon) {
    if (!state) return 0xFFFFFFFF;
    __try {
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
        if (!objects.Valid() || !players.Valid()) return 0xFFFFFFFF;
        Players p = FindPlayers(players, objects);
        bool vehicleGun = false;
        if (ShooterOf(objects, p, weapon, &vehicleGun) < 0) return 0xFFFFFFFF;
    } __except (Log::Exception(GetExceptionInformation(), "Halo 4 chaos gun")) {
        return 0xFFFFFFFF;
    }
    uint32_t pick = Fun::ChaosProjectile();
    return pick != 0xFFFFFFFF && Halo4Tags::Get(pick) ? pick : 0xFFFFFFFF;
}

bool Halo4Cheats::WeaponMayFire(void** state, uint32_t weapon) {
    if (!g_aiCantShoot || !state) return true;
    __try {
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
        if (!objects.Valid() || !players.Valid()) return true;
        char* object = ObjectFromDatum(objects, weapon);
        if (!object) return true;
        uint32_t holder = Field<uint32_t>(object, HALO4_OBJECT_PARENT);
        if (holder == 0xFFFFFFFF) return true;  // not held (e.g. a dropped weapon)

        Players p = FindPlayers(players, objects);
        for (int i = 0; i < p.count; ++i) {
            if (p.datums[i] == holder) return true;
            if (Field<uint32_t>(p.units[i], HALO4_OBJECT_PARENT) == holder) return true;  // player's vehicle
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}
