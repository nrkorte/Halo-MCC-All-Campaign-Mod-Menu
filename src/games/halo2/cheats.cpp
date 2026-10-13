#include "cheats.h"
#include "internal.h"

#include <atomic>
#include <intrin.h>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"

#include "cheat_util.h"
#include "core/log.h"
#include "core/patch.h"
#include "god_extras.h"
#include "tags.h"

namespace Halo2Cheats {
    // main_thread.cpp
    void SetMainThreadModule(HMODULE halo2);
    void ResetMainThread();
    void SpawnStatus(Games::Status& status);
}

using namespace Halo2Cheats;

namespace {
    // Skulls page, by byte index (the debug_ice_cream_flavor_status_*
    // globals). "(hidden)": debug or cut ones the game still checks for some.
    // Envy has its own toggle (Equipment page) and Acrophobia is Fly.
    const char* const kSkullNames[HALO2_SKULL_COUNT] = {
        nullptr, "Grunt Birthday Party", "Assassins", "Thunderstorm", "Famine", "IWHBYD", "Blind", "Ghost",
        "Black Eye", "Catch", "Sputnik", "Iron", "Mythic", "Anger", "That's Just Wrong (hidden)", "Bandanna",
        "Boom (hidden)", "Eye Patch (hidden)", "Fog (hidden)", "Foreign (hidden)", "Grunt Funeral (hidden)",
        "Pinata (hidden)", "Recession (hidden)", "Malfunction (hidden)", "Streaking (hidden)", "Jacked (hidden)",
        "Scarab (hidden)", "Styx (hidden)", "Feather (hidden)", "Bonded Pair (hidden)", "Masterblaster (hidden)",
        "So Angry (hidden)", "Prophet Birthday Party (hidden)", "They Come Back (hidden)", nullptr};

    constexpr int kMaxAimbotTags = 16;
    constexpr int kMaxAmmoBarrels = 16;
    constexpr int kMaxAiRecords = 512;
    constexpr float kAimAngle = 0.35f;  // moderate values: CE ignored extreme ones
    constexpr float kAimRange = 200.0f;
    constexpr int kSpeedFields[] = {0x24, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C};

    char* g_module = nullptr;

    // Vehicle god mode also stops section damage, which the damage flag
    // doesn't: god vehicles kept full health but still broke apart visually.
    using SectionDamageFn = uint64_t(__fastcall*)(void* damage, float scale, uint32_t object, void* result);
    SectionDamageFn g_origSectionDamage = nullptr;
    void* g_sectionDamageHook = nullptr;
    std::atomic<bool> g_sectionGod{false};

    bool ProtectedVehicle(uint32_t datum) {
        __try {
            Array players;
            Objects objects;
            if (!ReadTables(g_module, players, objects)) return false;
            char* object = objects.OfType(datum, HALO2_OBJECT_TYPE_VEHICLE);
            return object && (Field<uint16_t>(object, HALO2_OBJECT_DAMAGE_FLAGS) & HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Cannot be ejected: the unit-action hook drops the flipped-vehicle
    // eject for players. Read by the hook on the game thread.
    volatile bool g_noEject = false;
    using UnitActionFn = uint64_t(__fastcall*)(uint32_t unit, int32_t action, uint64_t c, uint64_t d);
    UnitActionFn g_origUnitAction = nullptr;
    void* g_unitActionHook = nullptr;

    bool IsPlayerUnit(uint32_t unit) {
        __try {
            Array players;
            Objects objects;
            if (!ReadTables(g_module, players, objects)) return false;
            Players p = FindPlayers(players, objects);
            for (int i = 0; i < p.count; ++i)
                if (p.datums[i] == unit) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    uint64_t __fastcall UnitActionDetour(uint32_t unit, int32_t action, uint64_t c, uint64_t d) {
        if (g_noEject && action == HALO2_ACTION_EJECT &&
            (uintptr_t)_ReturnAddress() - (uintptr_t)g_module == HALO2_FLIP_EJECT_RETURN && IsPlayerUnit(unit)) {
            static int s_logged = 0;
            if (s_logged++ < 3) LOG("Halo 2: kept player unit %08X in its flipped vehicle", unit);
            return 0;  // the unit update ignores the result here
        }
        return g_origUnitAction(unit, action, c, d);
    }

    uint64_t __fastcall SectionDamageDetour(void* damage, float scale, uint32_t object, void* result) {
        if (g_sectionGod.load(std::memory_order_relaxed) && ProtectedVehicle(object)) return 0;
        return g_origSectionDamage(damage, scale, object, result);
    }

    // A flag we set on player units. `units` remembers which, so a unit that
    // stops being a player's (co-op turned off: the second player's body stays
    // in the world) gets it cleared instead of keeping it for good.
    struct FlagState {
        bool applied = false;
        uint32_t units[8] = {};
        int next = 0;
    };
    FlagState g_godApplied;
    Games::VehicleGodState g_vehicleGod;
    // Cannot be hijacked: the seats of the vehicle player 1 is in (and of
    // what it's mounted on) can't be boarded while they're in it.
    constexpr int kMaxSeats = 32;
    Restorable<uint32_t> g_seatFlags[kMaxSeats];
    Restorable<int16_t> g_seatBoarding[kMaxSeats];
    FlagState g_blindApplied;
    Restorable<float> g_speed[_countof(kSpeedFields)];
    Restorable<float> g_jump[kMaxLocalPlayers];
    Restorable<float> g_gravity;
    Restorable<float> g_gameSpeed;
    Restorable<uint8_t> g_acrophobia;
    Restorable<uint8_t> g_envy;
    int16_t g_camoTicks[kMaxLocalPlayers] = {};  // camo length when it was switched on
    Restorable<uint8_t> g_aiGrenades;
    Restorable<uint8_t> g_hud;
    Restorable<int32_t> g_killTriggerCount;
    constexpr int kMaxBsps = 16;
    Restorable<float> g_vehicleLimit[kMaxBsps];
    Restorable<float> g_vehicleFloor[kMaxBsps];
    Restorable<float> g_vehicleCeiling[kMaxBsps];
    Restorable<uint16_t> g_deaf[kMaxAiRecords];

    struct AimbotTag {
        char* tag = nullptr;
        Restorable<float> fields[5];
    };
    AimbotTag g_aimbot[kMaxAimbotTags];
    Restorable<uint32_t> g_ammo[kMaxAmmoBarrels];
    struct SwordTag {
        char* tag = nullptr;
        Restorable<float> fields[4];
    };
    SwordTag g_sword[4];

    ::Patch::Nop g_aiCantFire, g_grenades, g_fullAuto, g_fullAutoRocket, g_rocketLock, g_noRecoil, g_camoFire, g_camoShot, g_camoGrenade, g_thirdPerson;
    // Explosion size: two call sites redirected into a small code cave (see offsets.h).
    ::Patch::Nop g_boomQuery, g_boomRange;
    Restorable<uint8_t> g_boomSkull;
    uint8_t* g_boomCave = nullptr;  // [0] = the factor (float), [0x10] = cave for the query, [0x40] = for the range

    // Memory within reach of a rel32 call from the module.
    uint8_t* AllocateNear(void* module, size_t size) {
        uintptr_t base = (uintptr_t)module & ~(uintptr_t)0xFFFF;
        for (uintptr_t delta = 0x10000; delta < 0x70000000; delta += 0x10000)
            for (int sign : {-1, 1}) {
                void* want = (void*)(sign < 0 ? base - delta : base + delta);
                void* got = VirtualAlloc(want, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                if (got) return (uint8_t*)got;
            }
        return nullptr;
    }

    // Writes one cave: `sub rsp,0x28; call original; add rsp,0x28; test al,al; jne ret;
    // <what>; ret`. `query` multiplies xmm6 by the factor and returns 1 (the caller then
    // skips its own doubling); otherwise it loads the factor into xmm12 (the caller picks
    // xmm12 when the call returned 0).
    void WriteBoomCave(uint8_t* at, uint8_t* original, uint8_t* factor, bool query) {
        uint8_t* p = at;
        auto put = [&](std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) *p++ = b; };
        put({0x48, 0x83, 0xEC, 0x28});
        *p++ = 0xE8;
        *(int32_t*)p = (int32_t)(original - (p + 4));
        p += 4;
        put({0x48, 0x83, 0xC4, 0x28, 0x84, 0xC0});
        if (query) {
            put({0x75, 0x0A, 0xF3, 0x0F, 0x59, 0x35});  // jne ret; mulss xmm6, [rip+d]
            *(int32_t*)p = (int32_t)(factor - (p + 4));
            p += 4;
            put({0xB0, 0x01, 0xC3});
        } else {
            put({0x75, 0x09, 0xF3, 0x44, 0x0F, 0x10, 0x25});  // jne ret; movss xmm12, [rip+d]
            *(int32_t*)p = (int32_t)(factor - (p + 4));
            p += 4;
            put({0xC3});
        }
    }

    bool InitBoom(HMODULE halo2) {
        uint8_t* query = ::Patch::FindUnique(halo2, HALO2_AOB_BOOM_QUERY);
        uint8_t* range = ::Patch::FindUnique(halo2, HALO2_AOB_BOOM_RANGE);
        if (!query || !range) return false;
        query += HALO2_BOOM_CALL_OFFSET;
        range += HALO2_BOOM_CALL_OFFSET;
        g_boomCave = AllocateNear(halo2, 0x1000);
        if (!g_boomCave) return false;
        auto target = [](uint8_t* call) { return call + 5 + *(int32_t*)(call + 1); };
        *(float*)g_boomCave = 1.0f;
        WriteBoomCave(g_boomCave + 0x10, target(query), g_boomCave, true);
        WriteBoomCave(g_boomCave + 0x40, target(range), g_boomCave, false);
        uint8_t jump[2][5];
        uint8_t* caves[2] = {g_boomCave + 0x10, g_boomCave + 0x40};
        uint8_t* sites[2] = {query, range};
        for (int i = 0; i < 2; ++i) {
            jump[i][0] = 0xE8;
            *(int32_t*)(jump[i] + 1) = (int32_t)(caves[i] - (sites[i] + 5));
        }
        return g_boomQuery.Init(query, jump[0], 5) && g_boomRange.Init(range, jump[1], 5);
    }

    Restorable<int32_t> g_fov;  // the FOV setting while the slider is moved off the game's own
    uint8_t* g_directors = nullptr;  // local players' camera entries (0x150 each)

    // Rapid fire's tag edits: barrels' rounds per second (min, max) and fire
    // recovery, magazines' reload and chamber time.
    constexpr int kMaxRapidEdits = 64;
    Restorable<float> g_rapid[kMaxRapidEdits];
    constexpr int kMaxSpreadEdits = 64;
    Restorable<float> g_spread[kMaxSpreadEdits];  // no spread: barrel error angles, zeroed in the tags

    char* g_listsScenario = nullptr;
    std::vector<Games::TagChoice> g_weapons;
    std::vector<Games::TagChoice> g_characters;  // AI that can be spawned
    std::vector<Games::TagChoice> g_vehicles;
    std::vector<VehicleChoice> g_vehicleChoices;  // variant entries, by index
    std::mutex g_vehicleChoicesLock;              // read by the game thread
    std::vector<Games::TagChoice> g_objects;     // crates, equipment, scenery, machines, controls
    std::vector<Games::TagChoice> g_projectiles;
    std::vector<Games::TagChoice> g_bsps;

    template <typename T>
    T* Global(int rva) { return (T*)(g_module + rva); }

    // `sticky`: once the mod has set the flag this session, switching off clears it on the players' units
    // even if `applied` was lost (a checkpoint revert restores the saved unit with the flag still set).
    void UnitFlag(const Players& p, const Objects& objects, int offset, uint32_t mask, bool on, FlagState& state,
                  bool word, bool* sticky = nullptr) {
        for (uint32_t& d : state.units) {
            if (!d) continue;
            bool current = false;
            for (int i = 0; i < p.count; ++i) current |= p.datums[i] == d;
            if (current) continue;
            if (char* unit = objects.Get(d)) {
                if (word) Field<uint16_t>(unit, offset) &= (uint16_t)~mask;
                else Field<uint32_t>(unit, offset) &= ~mask;
            }
            d = 0;
        }
        if (on && sticky) *sticky = true;
        if (!on && !state.applied && !(sticky && *sticky)) return;
        for (int i = 0; i < p.count; ++i) {
            if (word) {
                auto& flags = Field<uint16_t>(p.units[i], offset);
                flags = on ? (uint16_t)(flags | mask) : (uint16_t)(flags & ~mask);
            } else {
                auto& flags = Field<uint32_t>(p.units[i], offset);
                flags = on ? (flags | mask) : (flags & ~mask);
            }
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

    // Vehicle god mode: the damage flag, and health kept full (as for the player).
    // Like object_cannot_take_damage, the flag also goes on attached parts that
    // share it (turrets); without that they still took damage.
    void SetCannotTakeDamage(char* o, bool god) {
        auto& flags = Field<uint16_t>(o, HALO2_OBJECT_DAMAGE_FLAGS);
        flags = god ? (uint16_t)(flags | HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE)
                    : (uint16_t)(flags & ~HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
        auto& health = Field<float>(o, HALO2_OBJECT_HEALTH);
        if (god && health > 0.0f && health < 1.0f) health = 1.0f;
    }

    void VehicleGod(const Players& p, const Objects& objects, bool on) {
        uint32_t seats[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i) seats[i] = Field<uint32_t>(p.units[i], HALO2_OBJECT_PARENT);
        g_vehicleGod.Update(
            seats, p.count, on, [&](uint32_t d) { return objects.OfType(d, HALO2_OBJECT_TYPE_VEHICLE); },
            [](char* o) { return Field<uint32_t>(o, HALO2_OBJECT_PARENT); },
            [](char* o) { return (Field<uint16_t>(o, HALO2_OBJECT_DAMAGE_FLAGS) & HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE) != 0; },
            [&](char* o, bool god) {
                SetCannotTakeDamage(o, god);
                if (god && Field<float>(o, HALO2_OBJECT_HEALTH) < 1.0f) Field<float>(o, HALO2_OBJECT_HEALTH) = 1.0f;
                uint32_t child = Field<uint32_t>(o, HALO2_OBJECT_FIRST_CHILD);
                for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
                    char* c = objects.Get(child);
                    if (!c) break;
                    if (Field<uint32_t>(c, HALO2_OBJECT_FLAGS) & HALO2_OBJECT_FLAG_SHARES_DAMAGE_FLAGS)
                        SetCannotTakeDamage(c, god);
                    child = Field<uint32_t>(c, HALO2_OBJECT_NEXT_SIBLING);
                }
            });
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
        char* tag = Halo2Tags::Get(Field<uint32_t>(vehicle, HALO2_OBJECT_TAG_DATUM));
        char* seat = Halo2Tags::BlockElement(tag, HALO2_VEHI_SEATS, HALO2_VEHI_SEAT_SIZE,
                                             Field<int16_t>(unit, HALO2_UNIT_SEAT));
        if (!seat) return false;
        uint32_t* at = &Field<uint32_t>(seat, HALO2_SEAT_FLAGS);
        uint32_t flags = *at;
        for (auto& r : g_seatFlags)
            if (r.Ours(at)) flags = r.original;
        return (flags & HALO2_SEAT_FLAG_BOARDING_SEAT) != 0;
    }

    // Cannot be hijacked: every seat of the vehicle player 1 is in (and of
    // what it's mounted on) loses "Boarding Seat" and its boarding seat
    // index. A tag edit, so same-type vehicles too, only while you're in one.
    // Not while player 1 is boarding a vehicle themselves: clearing the seat
    // they board through left them stuck mid-hijack (tested 2026-10-02).
    void VehicleNoHijack(const Players& p, const Objects& objects, bool on) {
        uint32_t* flags[kMaxSeats] = {};
        int16_t* boarding[kMaxSeats] = {};
        int count = 0;
        char* vehicle = on && p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        if (vehicle && Boarding(p.units[0], vehicle)) vehicle = nullptr;
        for (int depth = 0; vehicle && depth < 4; ++depth) {
            char* tag = Halo2Tags::Get(Field<uint32_t>(vehicle, HALO2_OBJECT_TAG_DATUM));
            int seats = Halo2Tags::BlockCount(tag, HALO2_VEHI_SEATS);
            for (int i = 0; i < seats && count < kMaxSeats; ++i) {
                char* seat = Halo2Tags::BlockElement(tag, HALO2_VEHI_SEATS, HALO2_VEHI_SEAT_SIZE, i);
                if (!seat) continue;
                flags[count] = &Field<uint32_t>(seat, HALO2_SEAT_FLAGS);
                boarding[count++] = &Field<int16_t>(seat, HALO2_SEAT_BOARDING_INDEX);
            }
            vehicle = VehicleOf(vehicle, objects);
        }
        SetFields(g_seatFlags, flags, count, [](uint32_t f) { return f & ~HALO2_SEAT_FLAG_BOARDING_SEAT; });
        SetFields(g_seatBoarding, boarding, count, [](int16_t) { return (int16_t)-1; });
    }

    // God mode also keeps health and shield full (falling damage gets past
    // the damage flag in CE; likely the same here).
    void KeepVitality(const Players& p) {
        for (int i = 0; i < p.count; ++i) {
            auto& health = Field<float>(p.units[i], HALO2_OBJECT_HEALTH);
            auto& shield = Field<float>(p.units[i], HALO2_OBJECT_SHIELD);
            if (health < 1.0f) health = 1.0f;
            if (shield < 1.0f) shield = 1.0f;
        }
    }

    // Tops up the players' weapons from their tags' magazine sizes, and keeps
    // energy weapons' battery and heat at zero where asked.
    void Weapons(const Players& p, const Objects& objects, const Games::Settings& s) {
        if (!s.infiniteAmmo && !s.bottomlessClip && !s.noOverheat && !s.noSpread) return;
        // No overheat also covers the vehicle (or turret) each player is in.
        for (int i = 0; s.noOverheat && i < p.count; ++i) {
            char* vehicle = VehicleOf(p.units[i], objects);
            for (int w = 0; vehicle && w < HALO2_UNIT_WEAPON_COUNT; ++w)
                if (char* weapon = WeaponInSlot(vehicle, objects, w)) Field<float>(weapon, HALO2_WEAPON_HEAT) = 0.0f;
        }
        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                if (!weapon) continue;
                if (s.noOverheat) Field<float>(weapon, HALO2_WEAPON_HEAT) = 0.0f;
                if (s.noSpread) {
                    Field<float>(weapon, HALO2_WEAPON_ERROR0) = 0.0f;
                    Field<float>(weapon, HALO2_WEAPON_ERROR1) = 0.0f;
                    Field<float>(weapon, HALO2_WEAPON_ERROR_SMOOTH) = 0.0f;
                }
                if (!s.infiniteAmmo && !s.bottomlessClip) continue;
                if (s.infiniteAmmo) Field<float>(weapon, HALO2_WEAPON_AGE) = 0.0f;
                char* tag = Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM));
                int magazines = Halo2Tags::BlockCount(tag, HALO2_WEAP_MAGAZINES);
                for (int m = 0; m < magazines && m < 2; ++m) {
                    char* def = Halo2Tags::BlockElement(tag, HALO2_WEAP_MAGAZINES, HALO2_WEAP_MAGAZINE_DEF_SIZE, m);
                    if (!def) continue;
                    char* mag = weapon + HALO2_WEAPON_MAGAZINES + m * HALO2_WEAPON_MAGAZINE_SIZE;
                    int16_t loadedMax = Field<int16_t>(def, HALO2_MAGDEF_ROUNDS_LOADED_MAX);
                    int16_t totalMax = Field<int16_t>(def, HALO2_MAGDEF_ROUNDS_TOTAL_MAX);
                    auto& loaded = Field<int16_t>(mag, HALO2_MAGAZINE_LOADED);
                    auto& reserve = Field<int16_t>(mag, HALO2_MAGAZINE_RESERVE);
                    if (s.bottomlessClip && loaded < loadedMax) loaded = loadedMax;
                    int16_t reserveMax = totalMax > loadedMax ? totalMax - loadedMax : totalMax;
                    if (s.infiniteAmmo && reserve < reserveMax) reserve = reserveMax;
                }
            }
        }
    }

    // Leaves enemy AI with no shield and a sliver of health, so any hit kills.
    void OneHitKill(const Players& p, const Objects& objects) {
        int count = objects.table.HighWater();
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            int type = -1;
            char* object = *(uint16_t*)entry ? objects.FromEntry(entry, &type) : nullptr;
            if (!object || !IsKillableEnemy(object, type, p)) continue;
            auto& health = Field<float>(object, HALO2_OBJECT_HEALTH);
            if (health <= 0.0f) continue;
            Field<float>(object, HALO2_OBJECT_SHIELD) = 0.0f;
            if (health > 0.01f) health = 0.01f;
        }
    }

    // Rapid fire, tag side: while player 1 fires, their weapons fire every
    // tick with no recovery (the carbine's rate is capped by its barrel tag,
    // not just the barrel counter; the rocket launcher's by its magazine's
    // chamber time). Restored when they let go.
    void RapidFireTags(const Players& p, const Objects& objects, bool own, bool vehicles) {
        struct Edit { float* at; float value; };
        Edit wanted[kMaxRapidEdits];
        int wantedCount = 0;
        auto want = [&](char* base, int offset, float value) {
            if (wantedCount < kMaxRapidEdits) wanted[wantedCount++] = {&Field<float>(base, offset), value};
        };
        char* units[2];
        int unitCount = Shooters(p, objects, own, vehicles, units, _countof(units), 1);
        for (int u = 0; u < unitCount; ++u)
        for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
            char* weapon = WeaponInSlot(units[u], objects, w);
            char* tag = weapon ? Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM)) : nullptr;
            int barrels = Halo2Tags::BlockCount(tag, HALO2_WEAP_BARRELS);
            for (int b = 0; b < barrels; ++b) {
                char* barrel = Halo2Tags::BlockElement(tag, HALO2_WEAP_BARRELS, HALO2_WEAP_BARREL_SIZE, b);
                if (!barrel) continue;
                want(barrel, HALO2_BARREL_ROUNDS_PER_SECOND, 60.0f);
                want(barrel, HALO2_BARREL_ROUNDS_PER_SECOND + 4, 60.0f);
                want(barrel, HALO2_BARREL_FIRE_RECOVERY, 0.0f);
            }
            int magazines = Halo2Tags::BlockCount(tag, HALO2_WEAP_MAGAZINES);
            for (int m = 0; m < magazines; ++m) {
                char* def = Halo2Tags::BlockElement(tag, HALO2_WEAP_MAGAZINES, HALO2_WEAP_MAGAZINE_DEF_SIZE, m);
                if (!def) continue;
                want(def, HALO2_MAGDEF_RELOAD_TIME, 0.0f);
                want(def, HALO2_MAGDEF_CHAMBER_TIME, 0.0f);
            }
        }
        for (auto& r : g_rapid) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i].at == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<float>* slot = nullptr;
            for (auto& r : g_rapid)
                if (r.addr == wanted[i].at) { slot = &r; break; }
            for (auto& r : g_rapid)
                if (!slot && !r.addr) slot = &r;
            if (slot) slot->Set(wanted[i].at, wanted[i].value);
        }
    }

    // No spread, tag side: the players' weapons' barrels have no minimum error,
    // error angle or pellet spread (put back when it's turned off or the
    // weapon is gone). The runtime error the weapon builds up is zeroed too
    // (Weapons()).
    void NoSpreadTags(const Players& p, const Objects& objects, bool on) {
        float* wanted[kMaxSpreadEdits];
        int wantedCount = 0;
        for (int i = 0; on && i < p.count; ++i)
            for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM)) : nullptr;
                int barrels = Halo2Tags::BlockCount(tag, HALO2_WEAP_BARRELS);
                for (int b = 0; b < barrels; ++b) {
                    char* barrel = Halo2Tags::BlockElement(tag, HALO2_WEAP_BARRELS, HALO2_WEAP_BARREL_SIZE, b);
                    if (!barrel) continue;
                    for (int offset : {HALO2_BARREL_MIN_ERROR_DUAL, HALO2_BARREL_ERROR_DUAL, HALO2_BARREL_ERROR_DUAL + 4,
                                       HALO2_BARREL_DISTRIBUTION, HALO2_BARREL_MIN_ERROR, HALO2_BARREL_ERROR,
                                       HALO2_BARREL_ERROR + 4})
                        if (wantedCount < kMaxSpreadEdits) wanted[wantedCount++] = &Field<float>(barrel, offset);
                }
            }
        for (auto& r : g_spread) {
            if (!r.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i] == r.addr;
            if (!keep) r.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<float>* slot = nullptr;
            for (auto& r : g_spread)
                if (r.addr == wanted[i]) { slot = &r; break; }
            for (auto& r : g_spread)
                if (!slot && !r.addr) slot = &r;
            if (slot) slot->Set(wanted[i], 0.0f);
        }
    }

    // Infinite equipment: active camo doesn't run out. While it's on, its
    // ticks left stay at what they were when it came on; turning it off
    // still works.
    void InfiniteCamo(const Players& p, bool on) {
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            char* unit = on && i < p.count ? p.units[i] : nullptr;
            if (!unit || Field<uint8_t>(unit, HALO2_UNIT_CAMO_STATE) != HALO2_CAMO_ON) {
                g_camoTicks[i] = 0;
                continue;
            }
            auto& ticks = Field<int16_t>(unit, HALO2_UNIT_CAMO_TICKS);
            if (ticks > g_camoTicks[i]) g_camoTicks[i] = ticks;
            ticks = g_camoTicks[i];
        }
    }

    void Speed(char* matg, float m) {
        char* info = Halo2Tags::BlockElement(matg, HALO2_MATG_PLAYER_INFORMATION, HALO2_MATG_PLAYER_INFO_SIZE, 0);
        if (!info) return;
        for (int f = 0; f < (int)_countof(kSpeedFields); ++f)
            Scale(g_speed[f], &Field<float>(info, kSpeedFields[f]), m);
    }

    // Vehicle speed and grip: the tag of the vehicle player 1 is in. Speed
    // scales its speeds and accelerations; the forces that turn its velocity
    // (deceleration, slide) scale with speed x grip, so it still corners.
    // Put back when they get out or switch vehicles.
    struct VehicleField { int offset; bool grip; };
    constexpr VehicleField kVehicleFields[] = {
        {HALO2_VEHI_MAX_FORWARD_SPEED, false}, {HALO2_VEHI_MAX_REVERSE_SPEED, false},
        {HALO2_VEHI_SPEED_ACCELERATION, false}, {HALO2_VEHI_ENGINE_MAX_ANGULAR_VELOCITY, false},
        {HALO2_VEHI_MAX_LEFT_SLIDE, false}, {HALO2_VEHI_MAX_RIGHT_SLIDE, false},
        {HALO2_VEHI_SPEED_DECELERATION, true}, {HALO2_VEHI_SLIDE_ACCELERATION, true},
        {HALO2_VEHI_SLIDE_DECELERATION, true},
    };
    char* g_speedVehicle = nullptr;
    Restorable<float> g_vehicleSpeed[_countof(kVehicleFields)];

    void VehicleSpeed(const Players& p, const Objects& objects, float speed, float grip) {
        bool on = speed != 1.0f || grip != 1.0f;
        char* vehicle = p.count > 0 && on ? VehicleOf(p.units[0], objects) : nullptr;
        char* tag = vehicle ? Halo2Tags::Get(Field<uint32_t>(vehicle, HALO2_OBJECT_TAG_DATUM)) : nullptr;
        if (tag != g_speedVehicle) {
            for (auto& r : g_vehicleSpeed) r.Restore();
            g_speedVehicle = tag;
        }
        if (!tag) return;
        for (int f = 0; f < (int)_countof(kVehicleFields); ++f)
            Scale(g_vehicleSpeed[f], &Field<float>(tag, kVehicleFields[f].offset),
                  kVehicleFields[f].grip ? speed * grip : speed);
    }

    void Jump(const Players& p, float m) {
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            char* tag = i < p.count ? Halo2Tags::Get(Field<uint32_t>(p.units[i], HALO2_OBJECT_TAG_DATUM)) : nullptr;
            if (tag) Scale(g_jump[i], &Field<float>(tag, HALO2_BIPD_JUMP_VELOCITY), m);
        }
    }

    void Aimbot(const Players& p, const Objects& objects, bool on) {
        if (!on) {
            for (auto& a : g_aimbot) {
                for (auto& f : a.fields) f.Restore();
                a.tag = nullptr;
            }
            return;
        }
        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM)) : nullptr;
                if (!tag || IsMeleeOnly(weapon)) continue;  // the sword keeps its own lunge aim
                AimbotTag* slot = nullptr;
                for (auto& a : g_aimbot)
                    if (a.tag == tag) { slot = &a; break; }
                for (auto& a : g_aimbot)
                    if (!slot && !a.tag) { slot = &a; slot->tag = tag; }
                if (!slot) continue;
                slot->fields[0].Set(&Field<float>(tag, HALO2_WEAP_AUTOAIM_ANGLE), kAimAngle);
                slot->fields[1].Set(&Field<float>(tag, HALO2_WEAP_AUTOAIM_RANGE), kAimRange);
                slot->fields[2].Set(&Field<float>(tag, HALO2_WEAP_MAGNETISM_ANGLE), kAimAngle);
                slot->fields[3].Set(&Field<float>(tag, HALO2_WEAP_MAGNETISM_RANGE), kAimRange);
                slot->fields[4].Set(&Field<float>(tag, HALO2_WEAP_DEVIATION_ANGLE), kAimAngle);
            }
        }
    }

    // Perfect sword fly: melee-only weapons (the energy sword) get huge
    // autoaim and magnetism ranges, so the reticle turns red and the lunge
    // locks on at any distance. Edits the weapon tag, restored when off.
    void SwordFly(const Players& p, const Objects& objects, bool on) {
        if (!on) {
            for (auto& a : g_sword) {
                for (auto& f : a.fields) f.Restore();
                a.tag = nullptr;
            }
            return;
        }
        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO2_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                if (!weapon || !IsMeleeOnly(weapon)) continue;
                char* tag = Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM));
                if (!tag) continue;
                SwordTag* slot = nullptr;
                for (auto& a : g_sword)
                    if (a.tag == tag) { slot = &a; break; }
                for (auto& a : g_sword)
                    if (!slot && !a.tag) { slot = &a; slot->tag = tag; }
                if (!slot) continue;
                slot->fields[0].Set(&Field<float>(tag, HALO2_WEAP_AUTOAIM_ANGLE), 1.2f);
                slot->fields[1].Set(&Field<float>(tag, HALO2_WEAP_AUTOAIM_RANGE), 1000.0f);
                slot->fields[2].Set(&Field<float>(tag, HALO2_WEAP_MAGNETISM_ANGLE), 1.2f);
                slot->fields[3].Set(&Field<float>(tag, HALO2_WEAP_MAGNETISM_RANGE), 1000.0f);
            }
        }
    }

    bool InList(const std::vector<Games::TagChoice>& list, uint32_t datum) {
        for (const auto& c : list)
            if (c.datum == datum) return true;
        return false;
    }

    void AmmoType(const Players& p, const Objects& objects, uint32_t projectile) {
        uint32_t* wanted[kMaxAmmoBarrels] = {};
        int wantedCount = 0;
        char* weapon = p.count > 0 && projectile != 0xFFFFFFFF && InList(g_projectiles, projectile)
                           ? HeldWeapon(p.units[0], objects) : nullptr;
        char* tag = weapon ? Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM)) : nullptr;
        int barrels = Halo2Tags::BlockCount(tag, HALO2_WEAP_BARRELS);
        for (int b = 0; b < barrels && wantedCount < kMaxAmmoBarrels; ++b) {
            char* barrel = Halo2Tags::BlockElement(tag, HALO2_WEAP_BARRELS, HALO2_WEAP_BARREL_SIZE, b);
            if (barrel) wanted[wantedCount++] = &Field<uint32_t>(barrel, HALO2_BARREL_PROJECTILE + HALO2_TAGREF_DATUM);
        }
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

    // Moves a set (nonzero) vehicle floor/ceiling far out of the way.
    void PushOut(Restorable<float>& r, float* at, float distant) {
        if (r.Ours(at)) return;
        if (*at != 0.0f) r.Set(at, distant);
    }

    // Kill triggers, and the sphere, floor and ceiling each BSP keeps
    // vehicles inside.
    void NoBoundaries(char* scnr, bool on) {
        if (!scnr) return;
        auto* count = &Field<int32_t>(scnr, HALO2_SCNR_KILL_TRIGGERS);
        if (on) g_killTriggerCount.Set(count, 0);
        else g_killTriggerCount.Restore();
        for (int i = 0; i < kMaxBsps; ++i) {
            char* sbsp = on && i < (int)g_bsps.size() ? Halo2Tags::Get(g_bsps[i].datum) : nullptr;
            if (sbsp) {
                g_vehicleLimit[i].Set(&Field<float>(sbsp, HALO2_SBSP_VEHICLE_LIMIT_RADIUS), 0.0f);
                PushOut(g_vehicleFloor[i], &Field<float>(sbsp, HALO2_SBSP_VEHICLE_FLOOR), -100000.0f);
                PushOut(g_vehicleCeiling[i], &Field<float>(sbsp, HALO2_SBSP_VEHICLE_CEILING), 100000.0f);
            } else {
                g_vehicleLimit[i].Restore();
                g_vehicleFloor[i].Restore();
                g_vehicleCeiling[i].Restore();
            }
        }
    }

    // AI can't hear: the deaf flag ai_set_deaf sets on each AI record.
    void AiDeaf(bool on) {
        Array records{*Global<char*>(OFFSET_HALO2_AI_RECORDS)};
        int count = on && records.header ? records.HighWater() : 0;
        for (int i = 0; i < kMaxAiRecords; ++i) {
            char* record = i < count ? records.Entry(i) : nullptr;
            if (record && *(uint16_t*)record != 0) {
                auto* flags = &Field<uint16_t>(record, HALO2_AI_RECORD_FLAGS);
                uint16_t base = g_deaf[i].Ours(flags) ? g_deaf[i].original : *flags;
                g_deaf[i].Set(flags, (uint16_t)(base | HALO2_AI_FLAG_DEAF));
                g_deaf[i].original = base;
            } else {
                g_deaf[i].Restore();
            }
        }
    }

    void Switch(Restorable<uint8_t>& r, uint8_t* at, bool on, uint8_t value) {
        if (on && at) r.Set(at, value);
        else r.Restore();
    }

    // A character with a unit to spawn as (templates like "generic" have none).
    bool HasUnit(uint32_t character) {
        __try {
            char* tag = Halo2Tags::Get(character);
            return tag && Field<uint32_t>(tag, HALO2_CHAR_UNIT) != 0xFFFFFFFF;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Objects list: each kind's tags prefixed by kind ("crate: barrel"),
    // sorted within each kind. Editing helpers are left out by name.
    void BuildObjectList() {
        struct Kind { uint32_t group; const char* prefix; };
        constexpr Kind kKinds[] = {{HALO2_TAG_BLOC, "crate"},
                                   {HALO2_TAG_EQIP, "equipment"},
                                   {HALO2_TAG_SCEN, "scenery"},
                                   {HALO2_TAG_MACH, "machine"},
                                   {HALO2_TAG_CTRL, "control"}};
        constexpr const char* kHelpers[] = {"invisible", "pathfinding", "ui_object", "fx_", "standin", "kill_"};
        g_objects.clear();
        for (const Kind& kind : kKinds) {
            for (auto& tag : Halo2Tags::ListGroup(kind.group)) {
                bool helper = false;
                for (const char* h : kHelpers) helper |= tag.name.find(h) != std::string::npos;
                if (!helper) g_objects.push_back({tag.datum, std::string(kind.prefix) + ": " + tag.name});
            }
        }
    }

    bool InVehiclePalette(char* scnr, uint32_t vehicle) {
        int count = Halo2Tags::BlockCount(scnr, HALO2_SCNR_VEHICLE_PALETTE);
        for (int i = 0; i < count && i < 1024; ++i) {
            char* entry = Halo2Tags::BlockElement(scnr, HALO2_SCNR_VEHICLE_PALETTE, HALO2_PALETTE_ENTRY_SIZE, i);
            if (entry && Field<uint32_t>(entry, HALO2_PALETTE_ENTRY_DATUM) == vehicle) return true;
        }
        return false;
    }

    // Vehicles list: the mission's vehicle palette (the other 'vehi' tags
    // are turrets and parts), each followed by its model's other variants
    // ("warthog (gauss)"). Damage-state variants are left out.
    void BuildVehicleList(char* scnr) {
        std::vector<Games::TagChoice> list;
        std::vector<VehicleChoice> choices;
        for (auto& vehicle : Halo2Tags::ListGroup(HALO2_TAG_VEHI)) {
            if (!InVehiclePalette(scnr, vehicle.datum)) continue;
            list.push_back(vehicle);
            char* tag = Halo2Tags::Get(vehicle.datum);
            char* model = tag ? Halo2Tags::Get(Field<uint32_t>(tag, HALO2_OBJE_MODEL)) : nullptr;
            uint32_t defaultName = tag ? Field<uint32_t>(tag, HALO2_OBJE_DEFAULT_VARIANT) : 0;
            int count = Halo2Tags::BlockCount(model, HALO2_HLMT_VARIANTS);
            for (int v = 0; v < count && v < 64; ++v) {
                char* variant = Halo2Tags::BlockElement(model, HALO2_HLMT_VARIANTS, HALO2_HLMT_VARIANT_SIZE, v);
                uint32_t name = variant ? Field<uint32_t>(variant, 0) : 0;
                std::string label = Halo2Tags::StringName(name);
                if (!name || name == defaultName || label.empty() || label == "default" || label == "damaged") continue;
                VehicleChoice c;
                c.vehicle = vehicle.datum;
                c.variant = name;
                c.name = vehicle.name + " (" + label + ")";
                list.push_back({kVehicleChoiceId | (uint32_t)choices.size(), c.name});
                choices.push_back(c);
            }
        }
        g_vehicles = list;
        std::lock_guard<std::mutex> lock(g_vehicleChoicesLock);
        g_vehicleChoices = choices;
    }

    void RefreshLists(char* scnr) {
        if (scnr == g_listsScenario) return;
        g_listsScenario = scnr;
        Halo2Tags::LoadNames();
        g_weapons = Halo2Tags::ListGroup(HALO2_TAG_WEAP);
        g_projectiles = Halo2Tags::ListGroup(HALO2_TAG_PROJ);
        g_bsps = Halo2Tags::ListGroup(HALO2_TAG_SBSP);
        for (auto* limits : {g_vehicleLimit, g_vehicleFloor, g_vehicleCeiling})  // old map's tags are gone
            for (int i = 0; i < kMaxBsps; ++i) limits[i] = {};
        g_characters.clear();
        for (auto& character : Halo2Tags::ListGroup(HALO2_TAG_CHAR))
            if (HasUnit(character.datum)) g_characters.push_back(character);
        BuildVehicleList(scnr);
        BuildObjectList();
        Fun::SetPools(&g_vehicles, &g_objects, &g_characters, &g_weapons, &g_projectiles, [](uint32_t tag) {
            uint32_t group = Halo2Tags::GroupOf(tag);
            return group != HALO2_TAG_SCEN && group != HALO2_TAG_MACH;
        });
        LOG("Halo 2: map has %zu weapons, %zu projectiles, %zu characters, %zu vehicles, %zu objects",
            g_weapons.size(), g_projectiles.size(), g_characters.size(), g_vehicles.size(), g_objects.size());
    }

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    bool ReadState(Array& players, Objects& objects, Players& p) {
        __try {
            if (!ReadTables(g_module, players, objects)) return false;
            p = FindPlayers(players, objects);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}

void Halo2Cheats::Load(HMODULE halo2) {
    g_module = (char*)halo2;
    SetMainThreadModule(halo2);
    void* sections = g_module + OFFSET_HALO2_SECTION_DAMAGE;
    if (MH_CreateHook(sections, (void*)&SectionDamageDetour, (void**)&g_origSectionDamage) == MH_OK &&
        MH_EnableHook(sections) == MH_OK) {
        g_sectionDamageHook = sections;
    } else {
        MH_RemoveHook(sections);
        LOG("Halo 2: failed to hook section damage at %p; god vehicles will still look damaged", sections);
    }
    void* unitAction = g_module + OFFSET_HALO2_UNIT_ACTION;
    if (MH_CreateHook(unitAction, (void*)&UnitActionDetour, (void**)&g_origUnitAction) == MH_OK &&
        MH_EnableHook(unitAction) == MH_OK) {
        g_unitActionHook = unitAction;
    } else {
        MH_RemoveHook(unitAction);
        LOG("Halo 2: failed to hook unit actions at %p; cannot be ejected unavailable", unitAction);
    }
    struct { ::Patch::Nop* patch; const char* pattern; size_t size; const char* name; int offset; } patches[] = {
        {&g_aiCantFire, HALO2_AOB_AI_CANT_FIRE, 5, "AI can't fire", 0},
        {&g_grenades, HALO2_AOB_GRENADES, 6, "grenades", 0},
        {&g_fullAuto, HALO2_AOB_FULL_AUTO, 6, "full auto", 0},
        {&g_fullAutoRocket, HALO2_AOB_FULL_AUTO_ROCKET, 6, "full auto (rocket launcher)", 0},
        {&g_noRecoil, HALO2_AOB_NO_RECOIL, 5, "no recoil", HALO2_NO_RECOIL_OFFSET},
        {&g_camoFire, HALO2_AOB_CAMO_FIRE, 5, "camo stays on (trigger)", HALO2_CAMO_FIRE_OFFSET},
        {&g_camoGrenade, HALO2_AOB_CAMO_GRENADE, 5, "camo stays on (grenade)", HALO2_CAMO_GRENADE_OFFSET},
    };
    for (auto& p : patches) {
        uint8_t* at = ::Patch::FindUnique(halo2, p.pattern);
        if (!at || !p.patch->Init(at + p.offset, p.size)) LOG("Halo 2: %s patch NOT FOUND", p.name);
    }
    if (!::Patch::FindUnique(halo2, HALO2_AOB_THIRD_PERSON)) LOG("Halo 2: third person patch NOT FOUND");
    else {
        uint8_t* at = ::Patch::FindUnique(halo2, HALO2_AOB_THIRD_PERSON);
        g_thirdPerson.Init(at + HALO2_THIRD_PERSON_OFFSET, 2);
    }
    if (uint8_t* lea = ::Patch::FindUnique(halo2, HALO2_AOB_DIRECTORS)) g_directors = lea + 7 + *(int32_t*)(lea + 3);
    else LOG("Halo 2: camera directors NOT FOUND (third person unavailable)");
    if (!InitBoom(halo2)) LOG("Halo 2: explosion size patches NOT FOUND");
    static const uint8_t kJump[] = {0xEB, 0x76};  // jmp 0x8F0032
    uint8_t* shot = ::Patch::FindUnique(halo2, HALO2_AOB_CAMO_SHOT);
    if (!shot || !g_camoShot.Init(shot + HALO2_CAMO_SHOT_OFFSET, kJump, sizeof(kJump))) LOG("Halo 2: camo stays on (shot) patch NOT FOUND");
    static const uint8_t kFireLocked[] = {0xE9, 0x00, 0xFF, 0xFF, 0xFF};  // jmp 0x8EE643
    uint8_t* lock = ::Patch::FindUnique(halo2, HALO2_AOB_ROCKET_LOCK);
    if (!lock || !g_rocketLock.Init(lock + HALO2_ROCKET_LOCK_OFFSET, kFireLocked, sizeof(kFireLocked)))
        LOG("Halo 2: full auto (rocket lock-on) patch NOT FOUND");
    LOG("Halo 2: patches checked");
    Halo2Extras::Load(g_module);
    Halo2God::Load(g_module);
    Halo2Fun::Load(g_module);
}

void Halo2Cheats::Unload() {
    Halo2Extras::Unload();
    Halo2God::Unload();
    Halo2Fun::Unload();
    g_directors = nullptr;
    g_sectionGod = false;
    if (g_sectionDamageHook) {
        MH_DisableHook(g_sectionDamageHook);
        MH_RemoveHook(g_sectionDamageHook);
        g_sectionDamageHook = nullptr;
    }
    if (g_unitActionHook) {
        MH_DisableHook(g_unitActionHook);
        MH_RemoveHook(g_unitActionHook);
        g_unitActionHook = nullptr;
    }
    ::Patch::Nop* patches[] = {&g_aiCantFire, &g_grenades, &g_fullAuto, &g_fullAutoRocket, &g_rocketLock, &g_noRecoil, &g_camoFire, &g_camoShot, &g_camoGrenade, &g_thirdPerson, &g_boomQuery, &g_boomRange};
    for (auto* patch : patches) {
        patch->Apply(false);
        patch->Forget();
    }
    if (g_boomCave) {
        VirtualFree(g_boomCave, 0, MEM_RELEASE);
        g_boomCave = nullptr;
    }
    g_boomSkull = {};
    g_godApplied = FlagState();
    g_vehicleGod.Forget();
    for (auto& r : g_seatFlags) r = {};
    for (auto& r : g_seatBoarding) r = {};
    g_blindApplied = FlagState();
    for (auto& r : g_speed) r = {};
    for (auto& r : g_jump) r = {};
    for (auto& r : g_vehicleSpeed) r = {};
    g_speedVehicle = nullptr;
    g_gravity = {};
    g_gameSpeed = {};
    g_acrophobia = {};
    g_envy = {};
    for (auto& t : g_camoTicks) t = 0;
    g_aiGrenades = {};
    g_hud = {};
    g_killTriggerCount = {};
    for (auto* limits : {g_vehicleLimit, g_vehicleFloor, g_vehicleCeiling})
        for (int i = 0; i < kMaxBsps; ++i) limits[i] = {};
    g_bsps.clear();
    for (auto& r : g_deaf) r = {};
    for (auto& a : g_aimbot) a = {};
    for (auto& a : g_sword) a = {};
    for (auto& r : g_ammo) r = {};
    for (auto& r : g_rapid) r = {};
    for (auto& r : g_spread) r = {};
    g_fov = {};
    g_listsScenario = nullptr;
    g_weapons.clear();
    g_characters.clear();
    g_vehicles.clear();
    {
        std::lock_guard<std::mutex> lock(g_vehicleChoicesLock);
        g_vehicleChoices.clear();
    }
    g_objects.clear();
    g_projectiles.clear();
    ResetMainThread();
    SetMainThreadModule(nullptr);
    g_module = nullptr;
}

bool Halo2Cheats::FindVehicleChoice(uint32_t id, VehicleChoice& out) {
    if ((id & 0xFFFF0000) != kVehicleChoiceId) return false;
    std::lock_guard<std::mutex> lock(g_vehicleChoicesLock);
    size_t index = id & 0xFFFF;
    if (index >= g_vehicleChoices.size()) return false;
    out = g_vehicleChoices[index];
    return true;
}

bool Halo2Cheats::Apply(const Games::Settings& s, const Games::FrameInput& in, Games::Status& status) {
    if (!g_module) return false;
    g_sectionGod = s.vehicleGodMode;
    g_noEject = s.vehicleNoEject;
    Array players;
    Objects objects;
    Players p;
    if (!ReadState(players, objects, p)) return false;

    char* scnr = nullptr;
    char* matg = nullptr;
    Guard("Halo 2 tags", [&] {
        scnr = Halo2Tags::First(HALO2_TAG_SCNR);
        matg = Halo2Tags::First(HALO2_TAG_MATG);
        RefreshLists(scnr);
    });
    Fun::SetSettings(s);
    status.skullNames = kSkullNames;
    status.skullCount = HALO2_SKULL_COUNT;
    for (int i = 0; i < HALO2_SKULL_COUNT; ++i)
        if (((const uint8_t*)(g_module + OFFSET_HALO2_SKULLS))[i]) status.skullsActive |= 1ull << i;
    status.weapons = &g_weapons;
    status.projectiles = &g_projectiles;
    status.spawnAi = &g_characters;
    status.spawnVehicles = &g_vehicles;
    status.spawnObjects = &g_objects;
    SpawnStatus(status);

    Guard("Halo 2 god/blind", [&] {
        static bool s_godTouched = false;
        UnitFlag(p, objects, HALO2_OBJECT_DAMAGE_FLAGS, HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE, s.godMode, g_godApplied, true, &s_godTouched);
        // AI can't see you: only the player's ai_disregard flag (what the script command sets, unit +0x138
        // bit 7). The AI copies it into each perception prop on every perception update (RVA 0x6568D2:
        // prop +0x24 = unit flag bit 7), and a prop with it set scores 0 as a target (0x6535D8) and is
        // not tracked (0x5F8CB2, 0x635CA1). Checked live 2026-10-10: the player's props have +0x24 = 1,
        // everyone else's 0. The old global code patch (RVA 0x6507F4) blinded every AI to everything,
        // allies included, so it is no longer applied.
        UnitFlag(p, objects, HALO2_UNIT_FLAGS, HALO2_UNIT_FLAG_AI_DISREGARD, s.aiBlind, g_blindApplied, false);
        if (s.godMode) KeepVitality(p);
        Halo2God::Tick(s.godMode, p, objects);
    });
    Guard("Halo 2 vehicle god mode", [&] { VehicleGod(p, objects, s.vehicleGodMode); });
    Guard("Halo 2 vehicle no hijack", [&] { VehicleNoHijack(p, objects, s.vehicleNoHijack); });
    Guard("Halo 2 weapons", [&] { Weapons(p, objects, s); });
    Guard("Halo 2 one-hit kill", [&] { if (s.oneHitKill) OneHitKill(p, objects); });
    Guard("Halo 2 speed", [&] { if (matg) Speed(matg, s.speed); });
    Guard("Halo 2 jump", [&] { Jump(p, s.jump); });
    Guard("Halo 2 vehicle speed", [&] { VehicleSpeed(p, objects, s.vehicleSpeed, s.vehicleGrip); });
    Guard("Halo 2 gravity", [&] {
        if (float* gravity = *Global<float*>(OFFSET_HALO2_GRAVITY)) Scale(g_gravity, gravity, s.gravity);
    });
    Guard("Halo 2 game speed", [&] {
        if (char* time = *Global<char*>(OFFSET_HALO2_GAME_TIME))
            Scale(g_gameSpeed, &Field<float>(time, HALO2_GAME_TIME_SPEED), s.gameSpeed);
    });
    Guard("Halo 2 explosion size", [&] {
        bool on = g_boomCave && g_boomQuery.Ready() && g_boomRange.Ready() && fabsf(s.explosionScale - 1.0f) > 0.001f;
        if (g_boomCave) *(float*)g_boomCave = s.explosionScale;
        g_boomQuery.Apply(on);
        g_boomRange.Apply(on);
        Switch(g_boomSkull, Global<uint8_t>(OFFSET_HALO2_SKULLS + HALO2_SKULL_BOOM), on, 1);
    });
    Guard("Halo 2 skulls", [&] {
        Switch(g_acrophobia, Global<uint8_t>(OFFSET_HALO2_SKULLS + HALO2_SKULL_ACROPHOBIA), s.fly, 1);
        Switch(g_envy, Global<uint8_t>(OFFSET_HALO2_SKULLS + HALO2_SKULL_ENVY), s.envySkull, 1);
    });
    Guard("Halo 2 camo", [&] { InfiniteCamo(p, s.infiniteEquipment); });
    g_camoFire.Apply(s.infiniteEquipment);
    g_camoShot.Apply(s.infiniteEquipment);
    g_camoGrenade.Apply(s.infiniteEquipment);
    Guard("Halo 2 no spread", [&] { NoSpreadTags(p, objects, s.noSpread); });
    Guard("Halo 2 rapid fire", [&] { RapidFireTags(p, objects, s.rapidFire && in.firing, s.vehicleRapidFire && in.firing); });
    Guard("Halo 2 boundaries", [&] { NoBoundaries(scnr, s.noBoundaries); });
    Guard("Halo 2 aimbot", [&] { Aimbot(p, objects, s.aimbot); });
    Guard("Halo 2 sword fly", [&] { SwordFly(p, objects, s.swordFly); });
    Guard("Halo 2 ammo type", [&] { AmmoType(p, objects, s.ammoType); });
    Guard("Halo 2 AI", [&] {
        AiDeaf(s.aiDeaf);
        char* ai = *Global<char*>(OFFSET_HALO2_AI_GLOBALS);
        Switch(g_aiGrenades, ai ? &Field<uint8_t>(ai, HALO2_AI_GLOBALS_GRENADES) : nullptr, s.aiCantShoot, 0);
    });
    Guard("Halo 2 HUD", [&] {
        char* hud = *Global<char*>(OFFSET_HALO2_HUD);
        Switch(g_hud, hud ? &Field<uint8_t>(hud, HALO2_HUD_SHOW) : nullptr, s.noHud, 0);
    });
    g_aiCantFire.Apply(s.aiCantShoot);
    g_grenades.Apply(s.infiniteGrenades);
    bool fullAuto = s.fullAuto || s.rapidFire || s.vehicleRapidFire;  // semi-auto weapons can't rapid fire otherwise
    g_fullAuto.Apply(fullAuto);
    g_fullAutoRocket.Apply(fullAuto);
    g_rocketLock.Apply(fullAuto);
    g_noRecoil.Apply(s.noRecoil);
    if (g_directors && g_thirdPerson.Ready()) {
        g_thirdPerson.Apply(s.thirdPerson);
        // The director only looks again when its mode word changes, so after
        // a toggle (or a level load, which resets the entry) give it a
        // bogus one until the camera is the wanted type.
        Guard("Halo 2 third person", [&] {
            uint64_t fn = *(uint64_t*)(g_directors + HALO2_DIRECTOR_CAMERA_FN);
            uint64_t other = (uint64_t)g_module + (s.thirdPerson ? HALO2_CAMERA_FIRST_PERSON : HALO2_CAMERA_THIRD_PERSON);
            if (fn == other) *(uint16_t*)(g_directors + HALO2_DIRECTOR_MODE) = 0x7777;
        });
    }

    Guard("Halo 2 extras", [&] { Halo2Extras::Apply(s, p, objects, status); });
    Guard("Halo 2 upgrade AI", [&] { Halo2Upgrade::Apply(s, status); });
    status.fovMin = HALO2_FOV_MIN;
    status.fovMax = HALO2_FOV_MAX;
    status.fovDefault = HALO2_FOV_DEFAULT;
    Guard("Halo 2 field of view", [&] {
        auto* setting = (int32_t*)(g_module + HALO2_FOV_SETTING);
        if (s.fov > 0.0f) g_fov.Set(setting, (int32_t)(s.fov + 0.5f));
        else if (g_fov.addr) g_fov.Restore();
    });
    status.playerCount = p.count;
    Guard("Halo 2 status", [&] {
        if (p.count == 0) return;
        status.health = Field<float>(p.units[0], HALO2_OBJECT_HEALTH);
        status.shield = Field<float>(p.units[0], HALO2_OBJECT_SHIELD);
    });
    return true;
}
