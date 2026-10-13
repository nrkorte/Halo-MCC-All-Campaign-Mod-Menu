#include "cheats.h"

#include <intrin.h>
#include <string>
#include <vector>

#include "MinHook.h"
#include "cheat_util.h"
#include "core/log.h"
#include "core/patch.h"
#include "games/fun.h"
#include "games/squads.h"
#include "tags.h"

namespace Halo1Cheats {
    // main_thread.cpp
    void SetMainThreadModule(HMODULE halo1);
    void ResetMainThread();
    void InstallObjectHook();
    void RemoveObjectHook();
    void SpawnStatus(Games::Status& status);
}

using namespace Halo1Cheats;

namespace {
    constexpr int kMaxAimbotTags = 16;
    constexpr int kMaxAmmoTriggers = 16;
    constexpr int kMaxEncounters = 512;
    // Aim assist cone and range. CE ignored the extreme values used in the
    // other titles; these were checked live (shots lock on within ~20 degrees).
    constexpr float kAimAngle = 0.35f;
    constexpr float kAimRange = 200.0f;

    // Player information speeds scaled by the speed slider (see offsets.h).
    constexpr int kSpeedFields[] = {0x2C, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54};

    char* g_module = nullptr;

    // Cannot be ejected: the seat-detach hook skips the flipped-vehicle
    // ejection for players. Read by the hook on the game thread.
    volatile bool g_noEject = false;
    using UnitExitFn = uint64_t(__fastcall*)(uint32_t unit, uint64_t flag, uint64_t c, uint64_t d);
    UnitExitFn g_origUnitExit = nullptr;
    void* g_unitExitHook = nullptr;

    // Friendly fire and betrayal hooks. Read on the game thread.
    volatile bool g_noFriendlyFire = false;
    volatile bool g_noBetrayal = false;
    using CauseDamageFn = void(__fastcall*)(void* data, uint32_t victim, uint64_t c, uint64_t d, uint64_t e, uint64_t f);
    CauseDamageFn g_origCauseDamage = nullptr;
    void* g_causeDamageHook = nullptr;
    using BetrayalFn = bool(__fastcall*)(uint16_t a, uint16_t b, uint16_t mode, bool* out);
    BetrayalFn g_origBetrayal = nullptr;
    void* g_betrayalHook = nullptr;

    // A hit between player 1 and an AI on their side, either way, is dropped.
    bool FriendlyHit(void* data, uint32_t victimDatum) {
        __try {
            uint32_t shooterDatum = *(uint32_t*)((char*)data + HALO1_DAMAGE_DATA_INSTIGATOR_UNIT);
            if (shooterDatum == 0xFFFFFFFF || shooterDatum == victimDatum) return false;
            Array players;
            Objects objects;
            if (!ReadTables(g_module, players, objects)) return false;
            Players p = FindPlayers(players, objects);
            if (p.count == 0) return false;
            char* shooter = objects.Get(shooterDatum);
            char* victim = objects.Get(victimDatum);
            if (!shooter || !victim) return false;
            int myTeam = Field<int16_t>(p.units[0], HALO1_OBJECT_TEAM);
            auto alliedAi = [&](char* o) {
                return !p.Contains(o) && Field<uint32_t>(o, HALO1_UNIT_ACTOR) != 0xFFFFFFFF &&
                       TeamsFriends(g_module, Field<int16_t>(o, HALO1_OBJECT_TEAM), myTeam);
            };
            return (p.Contains(shooter) && alliedAi(victim)) || (alliedAi(shooter) && p.Contains(victim));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void __fastcall CauseDamageDetour(void* data, uint32_t victim, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
        if (g_noFriendlyFire && FriendlyHit(data, victim)) return;
        g_origCauseDamage(data, victim, c, d, e, f);
    }

    bool __fastcall BetrayalDetour(uint16_t a, uint16_t b, uint16_t mode, bool* out) {
        if (!g_noBetrayal) return g_origBetrayal(a, b, mode, out);
        __try { if (out) *out = false; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

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

    uint64_t __fastcall UnitExitDetour(uint32_t unit, uint64_t flag, uint64_t c, uint64_t d) {
        uintptr_t caller = (uintptr_t)_ReturnAddress() - (uintptr_t)g_module;
        if (g_noEject && caller == HALO1_FLIP_EJECT_RETURN && IsPlayerUnit(unit)) {
            static int s_logged = 0;
            if (s_logged++ < 3) LOG("Halo CE: kept player unit %08X in its flipped vehicle", unit);
            return 0;  // the unit update ignores the result here
        }
        return g_origUnitExit(unit, flag, c, d);
    }

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
    FlagState g_blindApplied;
    Restorable<uint8_t> g_deathless, g_infiniteAmmo, g_bottomless, g_omnipotent, g_jetpack;
    Restorable<float> g_speed[_countof(kSpeedFields)];
    Restorable<float> g_jump[kMaxLocalPlayers];
    constexpr int kVehicleSpeedFields[] = {HALO1_VEHI_MAX_FORWARD_SPEED, HALO1_VEHI_MAX_REVERSE_SPEED,
                                           HALO1_VEHI_SPEED_ACCELERATION};
    Games::VehicleScaler<_countof(kVehicleSpeedFields)> g_vehicleSpeed;
    Restorable<float> g_gravity;
    Restorable<float> g_gameSpeed;
    Restorable<uint8_t> g_aiGrenades;
    Restorable<uint8_t> g_hud;
    Restorable<uint8_t> g_deaf[kMaxEncounters];

    struct AimbotTag {
        char* tag = nullptr;
        Restorable<float> fields[5];  // autoaim angle, range, magnetism angle, range, deviation
    };
    AimbotTag g_aimbot[kMaxAimbotTags];
    Restorable<uint32_t> g_ammo[kMaxAmmoTriggers];  // trigger projectile datums
    constexpr int kMaxFiringShake = 32;
    Restorable<uint32_t> g_firingShake[kMaxFiringShake];  // firing damage datums cleared by no recoil

    ::Patch::Nop g_aiCantFire, g_aiInvisible, g_grenades, g_fullAuto, g_noSpread, g_noOverheat, g_noRecoil,
        g_thirdPerson;

    // Pickable tags, rebuilt when the map changes.
    char* g_listsScenario = nullptr;
    std::vector<Games::TagChoice> g_weapons;
    std::vector<Games::TagChoice> g_projectiles;
    std::vector<Games::TagChoice> g_characters;  // AI that can be spawned (actor variants)
    std::vector<Games::TagChoice> g_vehicles;
    std::vector<Games::TagChoice> g_objects;     // equipment, scenery, machines, controls

    // Skull menu: bit = byte index at OFFSET_HALO1_SKULLS (nullptr = none);
    // bits 32-35 are the engine's cheat switches beside them. Which are real
    // was read from the engine's code: ones marked (?) are read somewhere but
    // their effect was only inferred.
    const char* const kSkullNames[36] = {
        nullptr, "Iron (mission restarts on death)", "Fog (?)", "Mythic (Covenant and Flood have double health)",
        "Famine (?)", "Grunt Funeral (?)", "Foreign (?)", "Eye Patch (no aim assist)", nullptr, "Malfunction (?)",
        "Black Eye (?)", "Grunt Birthday Party (?)", "Pinata (?)", nullptr, nullptr, "Boom (?)", nullptr,
        "Blind (hides the HUD?)", "Ghost (?)", "Assassins (?)", "Catch (?)", "Sputnik (double knockback)",
        "Anger (?)", "Thunderstorm (?)", "That's Just Wrong (?)", nullptr, "Styx (Hunters have triple health)",
        "Feather (more knockback when armed)", "Bonded Pair (?)", "Masterblaster (?)", "Unnamed skull 30 (AI combat)",
        nullptr, "Bump possession (touch a unit to take it over)", "Super jump (4x jump height)",
        "Reflexive damage (your hits hurt you too)", "Medusa (AI looking at you die?)"};

    template <typename T>
    T* Global(int rva) { return (T*)(g_module + rva); }

    // A byte switch: set to `value` while on, back to the game's value when off.
    void Switch(Restorable<uint8_t>& r, uint8_t* at, bool on, uint8_t value = 1) {
        if (on) r.Set(at, value);
        else r.Restore();
    }

    // --- Cheats -------------------------------------------------------------

    void UnitFlag(const Players& p, const Objects& objects, int offset, uint32_t mask, bool on, FlagState& state,
                  bool word) {
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
        if (!on && !state.applied) return;
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
    Games::VehicleGodState g_vehicleGod;
    void VehicleGod(const Players& p, const Objects& objects, bool on) {
        uint32_t seats[kMaxLocalPlayers];
        for (int i = 0; i < p.count; ++i) seats[i] = Field<uint32_t>(p.units[i], HALO1_OBJECT_PARENT);
        g_vehicleGod.Update(
            seats, p.count, on, [&](uint32_t d) { return objects.OfType(d, HALO1_OBJECT_TYPE_VEHICLE); },
            [](char* o) { return Field<uint32_t>(o, HALO1_OBJECT_PARENT); },
            [](char* o) { return (Field<uint16_t>(o, HALO1_OBJECT_DAMAGE_FLAGS) & HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE) != 0; },
            [](char* o, bool god) {
                auto& flags = Field<uint16_t>(o, HALO1_OBJECT_DAMAGE_FLAGS);
                flags = god ? (uint16_t)(flags | HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE)
                            : (uint16_t)(flags & ~HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
                if (god && Field<float>(o, HALO1_OBJECT_HEALTH) < 1.0f) Field<float>(o, HALO1_OBJECT_HEALTH) = 1.0f;
            });
    }

    // The damage flag doesn't stop falling damage, so keep shield and health
    // topped up too (an overcharged shield is left alone).
    void KeepVitality(const Players& p) {
        for (int i = 0; i < p.count; ++i) {
            auto& health = Field<float>(p.units[i], HALO1_OBJECT_HEALTH);
            auto& shield = Field<float>(p.units[i], HALO1_OBJECT_SHIELD);
            if (health < 1.0f) health = 1.0f;
            if (shield < 1.0f) shield = 1.0f;
        }
    }

    // cheat_infinite_ammo didn't stop the reserve running out in testing, so
    // the reserve is also kept at the tag's maximum.
    void TopUpReserve(const Players& p, const Objects& objects) {
        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO1_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo1Tags::Get(Field<uint32_t>(weapon, HALO1_OBJECT_TAG_DATUM)) : nullptr;
                // Plasma weapons have no magazine: their battery is the weapon's age (0 = full,
                // 1 = drained; the weapon update at RVA 0xB795A1 adds to it and clamps at 1).
                if (weapon && Field<float>(weapon, HALO1_WEAPON_AGE) > 0.0f) Field<float>(weapon, HALO1_WEAPON_AGE) = 0.0f;
                int magazines = Halo1Tags::BlockCount(tag, HALO1_WEAP_MAGAZINES);
                for (int m = 0; m < magazines && m < 2; ++m) {
                    char* def = Halo1Tags::BlockElement(tag, HALO1_WEAP_MAGAZINES, HALO1_WEAP_MAGAZINE_DEF_SIZE, m);
                    if (!def) continue;
                    int16_t max = Field<int16_t>(def, HALO1_MAGDEF_ROUNDS_TOTAL_MAX);
                    auto& reserve = Field<int16_t>(weapon, HALO1_WEAPON_MAGAZINES + m * HALO1_WEAPON_MAGAZINE_SIZE +
                                                               HALO1_MAGAZINE_RESERVE);
                    if (reserve < max) reserve = max;
                    // A weapon picked up empty: fill the magazine too, so there's something to fire
                    // before the first reload (the reload itself is fine, but only once it has rounds).
                    auto& loaded = Field<int16_t>(weapon, HALO1_WEAPON_MAGAZINES + m * HALO1_WEAPON_MAGAZINE_SIZE +
                                                              HALO1_MAGAZINE_LOADED);
                    int16_t loadedMax = Field<int16_t>(def, HALO1_MAGDEF_ROUNDS_LOADED_MAX);
                    if (loaded == 0 && loadedMax > 0) {
                        // Empty: fill it and drop any reload in progress. A plasma weapon's
                        // battery is a magazine with no reserve, so a reload under way would
                        // finish by overwriting the fill with nothing from the reserve.
                        loaded = loadedMax;
                        int base = HALO1_WEAPON_MAGAZINES + m * HALO1_WEAPON_MAGAZINE_SIZE;
                        Field<int16_t>(weapon, base + HALO1_MAGAZINE_STATE) = 0;
                        Field<int16_t>(weapon, base + HALO1_MAGAZINE_TIMER) = 0;
                    }
                }
            }
        }
    }

    // Infinite grenades: the code patch only stops the count dropping, so with
    // none in hand there was nothing to throw. Both kinds are topped up to the
    // usual maximum of 4.
    void TopUpGrenades(const Players& p) {
        for (int i = 0; i < p.count; ++i)
            for (int type = 0; type < 2; ++type) {
                auto& count = Field<int8_t>(p.units[i], HALO1_UNIT_GRENADES + type);
                if (count < 4) count = 4;
            }
    }

    // In third person a zoom can't be undone (the scope view gets stuck), so
    // zooming is held off, as the other titles don't zoom in third person.
    void NoZoom(const Players& p) {
        for (int i = 0; i < p.count; ++i) {
            Field<int8_t>(p.units[i], HALO1_UNIT_ZOOM_LEVEL) = -1;
            Field<int8_t>(p.units[i], HALO1_UNIT_DESIRED_ZOOM) = -1;
        }
    }

    void Speed(char* matg, float m) {
        char* info = Halo1Tags::BlockElement(matg, HALO1_MATG_PLAYER_INFORMATION, HALO1_MATG_PLAYER_INFO_SIZE, 0);
        if (!info) return;
        for (int f = 0; f < (int)_countof(kSpeedFields); ++f)
            Scale(g_speed[f], &Field<float>(info, kSpeedFields[f]), m);
    }

    // Vehicle speed: the tag of the vehicle player 1 is in (same-type vehicles too).
    void VehicleSpeed(const Players& p, const Objects& objects, float m) {
        char* vehicle = p.count > 0 ? VehicleOf(p.units[0], objects) : nullptr;
        char* tag = vehicle ? Halo1Tags::Get(Field<uint32_t>(vehicle, HALO1_OBJECT_TAG_DATUM)) : nullptr;
        g_vehicleSpeed.Update(tag, kVehicleSpeedFields, _countof(kVehicleSpeedFields), nullptr, 0, m,
                              [](char*, int, int) -> char* { return nullptr; });
    }

    void Jump(const Players& p, float m) {
        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            char* tag = i < p.count ? Halo1Tags::Get(Field<uint32_t>(p.units[i], HALO1_OBJECT_TAG_DATUM)) : nullptr;
            if (tag) Scale(g_jump[i], &Field<float>(tag, HALO1_BIPD_JUMP_VELOCITY), m);
        }
    }

    // Maxes out autoaim and magnetism on the player's weapons, so shots bend
    // onto any enemy in view.
    void Aimbot(const Players& p, const Objects& objects, bool on) {
        if (!on) {
            for (auto& a : g_aimbot) {
                for (auto& f : a.fields) f.Restore();
                a.tag = nullptr;
            }
            return;
        }
        for (int i = 0; i < p.count; ++i) {
            for (int w = 0; w < HALO1_UNIT_WEAPON_COUNT; ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo1Tags::Get(Field<uint32_t>(weapon, HALO1_OBJECT_TAG_DATUM)) : nullptr;
                if (!tag || IsMeleeOnly(weapon)) continue;  // melee weapons keep their own aim

                AimbotTag* slot = nullptr;
                for (auto& a : g_aimbot)
                    if (a.tag == tag) { slot = &a; break; }
                for (auto& a : g_aimbot)
                    if (!slot && !a.tag) { slot = &a; slot->tag = tag; }
                if (!slot) continue;

                slot->fields[0].Set(&Field<float>(tag, HALO1_WEAP_AUTOAIM_ANGLE), kAimAngle);
                slot->fields[1].Set(&Field<float>(tag, HALO1_WEAP_AUTOAIM_RANGE), kAimRange);
                slot->fields[2].Set(&Field<float>(tag, HALO1_WEAP_MAGNETISM_ANGLE), kAimAngle);
                slot->fields[3].Set(&Field<float>(tag, HALO1_WEAP_MAGNETISM_RANGE), kAimRange);
                slot->fields[4].Set(&Field<float>(tag, HALO1_WEAP_DEVIATION_ANGLE), kAimAngle);
            }
        }
    }

    bool InList(const std::vector<Games::TagChoice>& list, uint32_t datum) {
        for (const auto& c : list)
            if (c.datum == datum) return true;
        return false;
    }

    // Points player 1's held weapon's triggers at the chosen projectile. Other
    // weapons go back to normal.
    void AmmoType(const Players& p, const Objects& objects, uint32_t projectile) {
        uint32_t* wanted[kMaxAmmoTriggers] = {};
        int wantedCount = 0;
        char* weapon = p.count > 0 && projectile != 0xFFFFFFFF && InList(g_projectiles, projectile)
                           ? HeldWeapon(p.units[0], objects) : nullptr;
        char* tag = weapon ? Halo1Tags::Get(Field<uint32_t>(weapon, HALO1_OBJECT_TAG_DATUM)) : nullptr;
        int triggers = Halo1Tags::BlockCount(tag, HALO1_WEAP_TRIGGERS);
        for (int t = 0; t < triggers && wantedCount < kMaxAmmoTriggers; ++t) {
            char* trigger = Halo1Tags::BlockElement(tag, HALO1_WEAP_TRIGGERS, HALO1_WEAP_TRIGGER_SIZE, t);
            if (trigger) wanted[wantedCount++] = &Field<uint32_t>(trigger, HALO1_TRIGGER_PROJECTILE + HALO1_TAGREF_DATUM);
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

    // No recoil also stops the camera shake each shot gives the shooter: a
    // trigger's firing effects carry a "firing damage" damage effect (the
    // magnum's is weapons\pistol	rigger, checked live) with camera shake.
    // With rapid fire every tick fires, and the shakes queued up for 20-30 s.
    // The weapons the players carry have that reference cleared while it's
    // on, and keep it cleared until it's turned off.
    void NoFiringShake(const Players& p, const Objects& objects, bool on) {
        if (!on) {
            for (auto& r : g_firingShake) r.Restore();
            return;
        }
        for (int i = 0; i < p.count; ++i) {
            for (int slot = 0; slot < HALO1_UNIT_WEAPON_COUNT; ++slot) {
                char* weapon = WeaponInSlot(p.units[i], objects, slot);
                char* tag = weapon ? Halo1Tags::Get(Field<uint32_t>(weapon, HALO1_OBJECT_TAG_DATUM)) : nullptr;
                int triggers = Halo1Tags::BlockCount(tag, HALO1_WEAP_TRIGGERS);
                for (int t = 0; t < triggers && t < 4; ++t) {
                    char* trigger = Halo1Tags::BlockElement(tag, HALO1_WEAP_TRIGGERS, HALO1_WEAP_TRIGGER_SIZE, t);
                    int effects = Halo1Tags::BlockCount(trigger, HALO1_TRIGGER_FIRING_EFFECTS);
                    for (int e = 0; e < effects && e < 8; ++e) {
                        char* effect = Halo1Tags::BlockElement(trigger, HALO1_TRIGGER_FIRING_EFFECTS,
                                                               HALO1_FIRING_EFFECT_SIZE, e);
                        if (!effect) continue;
                        uint32_t* damage = &Field<uint32_t>(effect, HALO1_FIRING_EFFECT_DAMAGE + HALO1_TAGREF_DATUM);
                        Restorable<uint32_t>* slotFor = nullptr;
                        for (auto& r : g_firingShake)
                            if (r.addr == damage) { slotFor = &r; break; }
                        for (auto& r : g_firingShake)
                            if (!slotFor && !r.addr) slotFor = &r;
                        if (slotFor && *damage != 0xFFFFFFFF) slotFor->Set(damage, 0xFFFFFFFF);
                    }
                }
            }
        }
    }

    // AI can't hear: CE's AI is grouped in encounters, each with a deaf flag
    // (what ai_set_deaf sets).
    void AiDeaf(bool on) {
        Array encounters{*Global<char*>(OFFSET_HALO1_ENCOUNTERS)};
        int count = on && encounters.header ? encounters.Count() : 0;
        for (int i = 0; i < kMaxEncounters; ++i) {
            char* encounter = i < count ? encounters.Entry(i) : nullptr;
            if (encounter && *(uint16_t*)encounter != 0) {
                auto* flags = &Field<uint8_t>(encounter, HALO1_ENCOUNTER_FLAGS);
                uint8_t base = g_deaf[i].Ours(flags) ? g_deaf[i].original : *flags;
                g_deaf[i].Set(flags, base | HALO1_ENCOUNTER_DEAF);
                g_deaf[i].original = base;
            } else {
                g_deaf[i].Restore();
            }
        }
    }

    void AiGrenades(bool cantShoot) {
        uint8_t* flags = *Global<uint8_t*>(OFFSET_HALO1_AI_GLOBALS);
        if (!flags) return;
        if (cantShoot) {
            uint8_t base = g_aiGrenades.Ours(flags) ? g_aiGrenades.original : *flags;
            g_aiGrenades.Set(flags, base & ~HALO1_AI_GLOBALS_GRENADES);
            g_aiGrenades.original = base;
        } else {
            g_aiGrenades.Restore();
        }
    }

    // An actor variant with a unit to spawn as.
    bool HasUnit(uint32_t variant) {
        __try {
            char* tag = Halo1Tags::Get(variant);
            return tag && Field<uint32_t>(tag, HALO1_ACTV_UNIT) != 0xFFFFFFFF;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Objects list: each kind's tags prefixed by kind ("scenery: index"),
    // sorted within each kind. Emitters and editing helpers are left out.
    void BuildObjectList() {
        struct Kind { uint32_t group; const char* prefix; };
        constexpr Kind kKinds[] = {{HALO1_TAG_EQIP, "equipment"},
                                   {HALO1_TAG_SCEN, "scenery"},
                                   {HALO1_TAG_MACH, "machine"},
                                   {HALO1_TAG_CTRL, "control"}};
        constexpr const char* kHelpers[] = {"invisible", "pathfinding", "sparks", "smoke", "drip", "emitter"};
        g_objects.clear();
        for (const Kind& kind : kKinds) {
            for (auto& tag : Halo1Tags::ListGroup(kind.group)) {
                bool helper = false;
                for (const char* h : kHelpers) helper |= tag.name.find(h) != std::string::npos;
                if (!helper) g_objects.push_back({tag.datum, std::string(kind.prefix) + ": " + tag.name});
            }
        }
    }

    void RefreshLists(char* scnr) {
        if (scnr == g_listsScenario) return;
        g_listsScenario = scnr;
        for (auto& r : g_firingShake) r = {};  // the old map's tags are gone
        g_weapons = Halo1Tags::ListGroup(HALO1_TAG_WEAP);
        g_projectiles = Halo1Tags::ListGroup(HALO1_TAG_PROJ);
        // CE keeps every tag in the map loaded, so all of them can be
        // spawned anywhere (no designer zones as in Reach).
        g_characters.clear();
        for (auto& variant : Halo1Tags::ListGroup(HALO1_TAG_ACTV))
            if (HasUnit(variant.datum)) g_characters.push_back(variant);
        g_vehicles = Halo1Tags::ListGroup(HALO1_TAG_VEHI);
        BuildObjectList();
        Fun::SetPools(&g_vehicles, &g_objects, &g_characters, &g_weapons, &g_projectiles, [](uint32_t tag) {
            uint32_t group = Halo1Tags::GroupOf(tag);
            return group != HALO1_TAG_SCEN && group != HALO1_TAG_MACH;
        });
        LOG("Halo CE: map has %zu weapons, %zu projectiles, %zu actor variants, %zu vehicles, %zu objects",
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

    bool InitPatch(::Patch::Nop& patch, HMODULE halo1, const char* pattern, size_t size, int offset = 0) {
        uint8_t* at = ::Patch::FindUnique(halo1, pattern);
        return at && patch.Init(at + offset, size);
    }
}

void Halo1Cheats::Load(HMODULE halo1) {
    g_module = (char*)halo1;
    SetMainThreadModule(halo1);
    void* exitTarget = g_module + OFFSET_HALO1_UNIT_DETACH_SEAT;
    if (MH_CreateHook(exitTarget, (void*)&UnitExitDetour, (void**)&g_origUnitExit) == MH_OK &&
        MH_EnableHook(exitTarget) == MH_OK) {
        g_unitExitHook = exitTarget;
    } else {
        MH_RemoveHook(exitTarget);
        LOG("Halo CE: failed to hook the seat detach at %p; cannot be ejected unavailable", exitTarget);
    }
    InstallObjectHook();
    void* damageTarget = g_module + OFFSET_HALO1_CAUSE_DAMAGE;
    if (MH_CreateHook(damageTarget, (void*)&CauseDamageDetour, (void**)&g_origCauseDamage) == MH_OK &&
        MH_EnableHook(damageTarget) == MH_OK) {
        g_causeDamageHook = damageTarget;
    } else {
        MH_RemoveHook(damageTarget);
        LOG("Halo CE: failed to hook cause-damage at %p; disable friendly fire unavailable", damageTarget);
    }
    void* betrayalTarget = g_module + OFFSET_HALO1_BETRAYAL_ADD;
    if (MH_CreateHook(betrayalTarget, (void*)&BetrayalDetour, (void**)&g_origBetrayal) == MH_OK &&
        MH_EnableHook(betrayalTarget) == MH_OK) {
        g_betrayalHook = betrayalTarget;
    } else {
        MH_RemoveHook(betrayalTarget);
        LOG("Halo CE: failed to hook the betrayal score at %p; allies-never-turn unavailable", betrayalTarget);
    }
    struct { ::Patch::Nop* patch; const char* pattern; size_t size; int offset; const char* name; } patches[] = {
        {&g_aiCantFire, HALO1_AOB_AI_CANT_FIRE, 7, 0, "AI can't fire"},
        {&g_aiInvisible, HALO1_AOB_AI_INVISIBLE, 6, 0, "AI invisible"},
        {&g_grenades, HALO1_AOB_GRENADES, 7, 0, "grenades"},
        {&g_fullAuto, HALO1_AOB_FULL_AUTO, 8, 0, "full auto"},
        {&g_noSpread, HALO1_AOB_NO_SPREAD, 9, 0, "no spread"},
        {&g_noOverheat, HALO1_AOB_NO_OVERHEAT, 6, 0, "no overheat"},
        {&g_noRecoil, HALO1_AOB_NO_RECOIL, 11, 0, "no recoil"},
        {&g_thirdPerson, HALO1_AOB_THIRD_PERSON, 2, HALO1_THIRD_PERSON_OFFSET, "third person"},
    };
    for (auto& p : patches)
        if (!InitPatch(*p.patch, halo1, p.pattern, p.size, p.offset)) LOG("Halo CE: %s patch NOT FOUND", p.name);
    LOG("Halo CE: patches checked");
}

void Halo1Cheats::Unload() {
    RemoveObjectHook();
    for (void** hook : {&g_causeDamageHook, &g_betrayalHook}) {
        if (!*hook) continue;
        MH_DisableHook(*hook);
        MH_RemoveHook(*hook);
        *hook = nullptr;
    }
    if (g_unitExitHook) {
        MH_DisableHook(g_unitExitHook);
        MH_RemoveHook(g_unitExitHook);
        g_unitExitHook = nullptr;
    }
    ::Patch::Nop* patches[] = {&g_aiCantFire, &g_aiInvisible, &g_grenades, &g_fullAuto, &g_noSpread,
                               &g_noOverheat, &g_noRecoil, &g_thirdPerson};
    for (auto* patch : patches) {
        patch->Apply(false);
        patch->Forget();
    }

    // Globals live in the module and go with it: forget, don't restore.
    g_godApplied = FlagState();
    g_blindApplied = FlagState();
    g_deathless = {};
    g_infiniteAmmo = {};
    g_bottomless = {};
    g_omnipotent = {};
    g_jetpack = {};
    for (auto& r : g_speed) r = {};
    for (auto& r : g_jump) r = {};
    g_vehicleSpeed.Forget();
    g_vehicleGod.Forget();
    g_gravity = {};
    g_gameSpeed = {};
    g_aiGrenades = {};
    g_hud = {};
    for (auto& r : g_deaf) r = {};
    for (auto& a : g_aimbot) a = {};
    for (auto& r : g_ammo) r = {};
    for (auto& r : g_firingShake) r = {};
    g_listsScenario = nullptr;
    g_weapons.clear();
    g_projectiles.clear();
    g_characters.clear();
    g_vehicles.clear();
    g_objects.clear();
    ResetMainThread();
    SetMainThreadModule(nullptr);
    g_module = nullptr;
}

bool Halo1Cheats::Apply(const Games::Settings& s, Games::Status& status) {
    if (!g_module) return false;
    g_noEject = s.vehicleNoEject;
    g_noFriendlyFire = s.noFriendlyFire;
    g_noBetrayal = s.noBetrayal || s.playerTeam == 1 || s.playerTeam == 3;  // allied to the Covenant: hitting them mustn't break it
    Array players;
    Objects objects;
    Players p;
    if (!ReadState(players, objects, p)) return false;

    char* scnr = nullptr;
    char* matg = nullptr;
    Guard("Halo CE tags", [&] {
        scnr = Halo1Tags::Scenario();
        matg = Halo1Tags::First(HALO1_TAG_MATG);
        RefreshLists(scnr);
    });
    status.weapons = &g_weapons;
    status.projectiles = &g_projectiles;
    status.spawnAi = &g_characters;
    status.squadsAvailable[0] = status.squadsAvailable[1] = 0;
    for (int i = 0; i < Games::kSquadCount; ++i)
        if (Games::kSquads[i].ce && !Games::SquadTags(Games::kSquads[i], g_characters).empty())
            Games::SetSquadAvailable(status, i);
    status.spawnVehicles = &g_vehicles;
    status.spawnObjects = &g_objects;
    SpawnStatus(status);
    Fun::SetSettings(s);
    status.skullNames = kSkullNames;
    status.skullCount = (int)_countof(kSkullNames);
    Guard("Halo CE skulls", [&] {
        for (int i = 0; i < (int)_countof(kSkullNames); ++i) {
            uint8_t* b = i < HALO1_SKULL_BYTES ? Global<uint8_t>(OFFSET_HALO1_SKULLS + i)
                                               : Global<uint8_t>(OFFSET_HALO1_CHEAT_BUMP_POSSESSION + i - 32);
            if (*b) status.skullsActive |= 1ull << i;
        }
    });

    Guard("Halo CE built-in cheats", [&] {
        Switch(g_deathless, Global<uint8_t>(OFFSET_HALO1_CHEAT_DEATHLESS), s.godMode);
        Switch(g_infiniteAmmo, Global<uint8_t>(OFFSET_HALO1_CHEAT_INFINITE_AMMO), s.infiniteAmmo);
        Switch(g_bottomless, Global<uint8_t>(OFFSET_HALO1_CHEAT_BOTTOMLESS), s.bottomlessClip);
        Switch(g_omnipotent, Global<uint8_t>(OFFSET_HALO1_CHEAT_OMNIPOTENT), s.oneHitKill);
        Switch(g_jetpack, Global<uint8_t>(OFFSET_HALO1_CHEAT_JETPACK), s.fly);
    });
    Guard("Halo CE god/blind", [&] {
        UnitFlag(p, objects, HALO1_OBJECT_DAMAGE_FLAGS, HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE, s.godMode, g_godApplied, true);
        UnitFlag(p, objects, HALO1_UNIT_FLAGS, HALO1_UNIT_FLAG_AI_DISREGARD, s.aiBlind, g_blindApplied, false);
    });
    Guard("Halo CE god vitality", [&] { if (s.godMode) KeepVitality(p); });
    Guard("Halo CE vehicle god mode", [&] { VehicleGod(p, objects, s.vehicleGodMode); });
    Guard("Halo CE ammo", [&] { if (s.infiniteAmmo) TopUpReserve(p, objects); });
    Guard("Halo CE grenades", [&] { if (s.infiniteGrenades) TopUpGrenades(p); });
    Guard("Halo CE third-person zoom", [&] { if (s.thirdPerson) NoZoom(p); });
    Guard("Halo CE speed", [&] { if (matg) Speed(matg, s.speed); });
    Guard("Halo CE jump", [&] { Jump(p, s.jump); });
    Guard("Halo CE vehicle speed", [&] { VehicleSpeed(p, objects, s.vehicleSpeed); });
    Guard("Halo CE gravity", [&] {
        if (float* gravity = *Global<float*>(OFFSET_HALO1_GRAVITY)) Scale(g_gravity, gravity, s.gravity);
    });
    Guard("Halo CE game speed", [&] { Scale(g_gameSpeed, Global<float>(OFFSET_HALO1_GAME_SPEED), s.gameSpeed); });
    Guard("Halo CE aimbot", [&] { Aimbot(p, objects, s.aimbot); });
    Guard("Halo CE ammo type", [&] { AmmoType(p, objects, s.ammoType); });
    Guard("Halo CE AI", [&] {
        AiDeaf(s.aiDeaf);
        AiGrenades(s.aiCantShoot);
    });
    Guard("Halo CE HUD", [&] {
        uint8_t* show = *Global<uint8_t*>(OFFSET_HALO1_SHOW_HUD);
        Switch(g_hud, show, s.noHud && show, 0);
    });
    g_aiCantFire.Apply(s.aiCantShoot);
    // "Can't see you" is only the player's own ai_disregard flag: the global vision patch
    // blinded every AI to every other one too (allies and enemies alike).
    g_aiInvisible.Apply(false);
    g_grenades.Apply(s.infiniteGrenades);
    g_fullAuto.Apply(s.fullAuto);
    g_noSpread.Apply(s.noSpread);
    g_noOverheat.Apply(s.noOverheat);
    g_noRecoil.Apply(s.noRecoil);
    Guard("Halo CE firing shake", [&] { NoFiringShake(p, objects, s.noRecoil); });
    g_thirdPerson.Apply(s.thirdPerson);

    status.playerCount = p.count;
    Guard("Halo CE status", [&] {
        if (p.count == 0) return;
        status.health = Field<float>(p.units[0], HALO1_OBJECT_HEALTH);
        status.shield = Field<float>(p.units[0], HALO1_OBJECT_SHIELD);
    });
    return true;
}
