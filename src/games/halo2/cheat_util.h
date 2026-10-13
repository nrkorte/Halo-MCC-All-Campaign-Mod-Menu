#pragma once

// Helpers shared by the Halo 2 cheat files. Everything here touches raw game
// memory, so callers must be inside SEH.

#include <cstdint>
#include <string>

#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace Halo2Internal { bool TeamsFriends(int teamA, int teamB); }  // main_thread.cpp

namespace Halo2Cheats {
    constexpr int kMaxLocalPlayers = 4;

    // A Vehicles-list entry other than a plain tag: a model variant. Its id
    // is kVehicleChoiceId | index (no tag has salt 0xFFFF).
    constexpr uint32_t kVehicleChoiceId = 0xFFFF0000;
    struct VehicleChoice {
        uint32_t vehicle = 0xFFFFFFFF;
        uint32_t variant = 0;  // model variant name (string id), 0 = default
        std::string name;      // as listed
    };
    // cheats.cpp: the variant entry behind a list id (any thread).
    bool FindVehicleChoice(uint32_t id, VehicleChoice& out);

    using Games::Field;
    using Games::Restorable;
    using Games::Scale;

    // Halo 2's data arrays: entries start with a 16-bit salt (0 = free) and
    // live at an offset from the header.
    struct Array {
        char* header = nullptr;

        int HighWater() const { return Field<int32_t>(header, HALO2_ARRAY_HIGH_WATER); }
        char* Entry(int index) const {
            return header + Field<int64_t>(header, HALO2_ARRAY_DATA) +
                   (int64_t)Field<int32_t>(header, HALO2_ARRAY_ENTRY_SIZE) * index;
        }
        char* Get(uint32_t datum) const {
            if (datum == 0xFFFFFFFF) return nullptr;
            uint16_t index = datum & 0xFFFF;
            if (index >= HighWater()) return nullptr;
            char* entry = Entry(index);
            uint16_t salt = *(uint16_t*)entry;
            return salt != 0 && salt == (datum >> 16) ? entry : nullptr;
        }
    };

    // The object table plus the heap the object data lives in.
    struct Objects {
        Array table;
        char* heap = nullptr;  // already aligned

        bool Valid() const { return table.header && heap; }

        char* FromEntry(char* entry, int* type = nullptr) const {
            int32_t offset = Field<int32_t>(entry, HALO2_OBJECT_ENTRY_OFFSET);
            if (offset == -1) return nullptr;
            if (type) *type = Field<uint8_t>(entry, HALO2_OBJECT_ENTRY_TYPE);
            return heap + offset;
        }
        char* Get(uint32_t datum, int* type = nullptr) const {
            char* entry = table.Get(datum);
            return entry ? FromEntry(entry, type) : nullptr;
        }
        char* OfType(uint32_t datum, int wanted) const {
            int type = -1;
            char* object = Get(datum, &type);
            return object && type == wanted ? object : nullptr;
        }
    };

    struct Players {
        char* units[kMaxLocalPlayers] = {};
        uint32_t datums[kMaxLocalPlayers] = {};
        int count = 0;

        bool Contains(char* object) const {
            for (int i = 0; i < count; ++i)
                if (units[i] == object) return true;
            return false;
        }
    };

    inline Players FindPlayers(const Array& players, const Objects& objects) {
        Players out;
        int limit = players.HighWater() < 16 ? players.HighWater() : 16;
        for (int i = 0; i < limit && out.count < kMaxLocalPlayers; ++i) {
            char* player = players.Entry(i);
            if (*(uint16_t*)player == 0) continue;
            uint32_t datum = Field<uint32_t>(player, HALO2_PLAYER_UNIT);
            char* unit = objects.Get(datum);
            if (!unit) continue;
            out.units[out.count] = unit;
            out.datums[out.count++] = datum;
        }
        return out;
    }

    // An AI-controlled enemy biped the game lets die: not on the player's side
    // (the live allegiance table, so it follows "Your team" and Arbiter missions).
    inline bool IsKillableEnemy(char* object, int type, const Players& p) {
        if (type != HALO2_OBJECT_TYPE_BIPED || p.Contains(object)) return false;
        int16_t team = Field<int16_t>(object, HALO2_UNIT_TEAM);
        if (team == HALO2_TEAM_PLAYER || team == HALO2_TEAM_HUMAN) {
            if (p.count == 0 || Halo2Internal::TeamsFriends(Field<int16_t>(p.units[0], HALO2_UNIT_TEAM), team)) return false;
        } else if (p.count > 0 && Halo2Internal::TeamsFriends(Field<int16_t>(p.units[0], HALO2_UNIT_TEAM), team)) {
            return false;
        }
        if (Field<uint32_t>(object, HALO2_UNIT_ACTOR) == 0xFFFFFFFF) return false;
        return !(Field<uint16_t>(object, HALO2_OBJECT_DAMAGE_FLAGS) & HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE);
    }

    inline char* WeaponInSlot(char* unit, const Objects& objects, int slot, uint32_t* datumOut = nullptr) {
        uint32_t datum = Field<uint32_t>(unit, HALO2_UNIT_WEAPONS + slot * 4);
        char* weapon = objects.OfType(datum, HALO2_OBJECT_TYPE_WEAPON);
        if (weapon && datumOut) *datumOut = datum;
        return weapon;
    }

    // The vehicle (or turret) a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, const Objects& objects) {
        return objects.OfType(Field<uint32_t>(unit, HALO2_OBJECT_PARENT), HALO2_OBJECT_TYPE_VEHICLE);
    }

    // Units whose weapons rapid fire acts on: the players' own (rapid fire)
    // and the vehicles they're in (vehicle rapid fire). Returns how many.
    inline int Shooters(const Players& p, const Objects& objects, bool own, bool vehicles, char** out, int max,
                        int players = kMaxLocalPlayers) {
        int n = 0;
        for (int i = 0; i < p.count && i < players; ++i) {
            if (own && n < max) out[n++] = p.units[i];
            char* vehicle = vehicles ? VehicleOf(p.units[i], objects) : nullptr;
            if (vehicle && n < max) out[n++] = vehicle;
        }
        return n;
    }

    inline char* HeldWeapon(char* unit, const Objects& objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, HALO2_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= HALO2_UNIT_WEAPON_COUNT) return nullptr;
        return WeaponInSlot(unit, objects, index, datumOut);
    }

    // True for weapons that only melee (the energy sword): the aimbot leaves
    // them alone. Argument: a WEAPON OBJECT (what WeaponInSlot/HeldWeapon
    // return), not a tag. Halo 2's weap tags have no "melee only" flag; found
    // live (2026-10-10, 05b_deltatowers, all 27 weap tags): the energy blade
    // (and energy_blade_useless, the MP skull item) have no barrels (weap
    // +0x2D0 count 0) and no magazines, while every gun has at least one
    // barrel (turret weapons included). Needs SEH from the caller.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? Halo2Tags::Get(Field<uint32_t>(weapon, HALO2_OBJECT_TAG_DATUM)) : nullptr;
        return tag && Halo2Tags::BlockCount(tag, HALO2_WEAP_BARRELS) == 0;
    }

    // True while the unit holds a melee-only weapon (for the aimbot strength
    // pull in main_thread.cpp: `if (HoldingMeleeOnly(p.units[0], objects)) return;`).
    inline bool HoldingMeleeOnly(char* unit, const Objects& objects) {
        char* weapon = HeldWeapon(unit, objects);
        return weapon && IsMeleeOnly(weapon);
    }

    inline bool ReadTables(char* module, Array& players, Objects& objects) {
        players.header = *(char**)(module + OFFSET_HALO2_PLAYERS);
        objects.table.header = *(char**)(module + OFFSET_HALO2_OBJECTS);
        char* heap = *(char**)(module + OFFSET_HALO2_OBJECT_HEAP);
        objects.heap = heap ? (char*)(((uintptr_t)heap + 0x57) & ~(uintptr_t)0xF) : nullptr;
        return players.header && objects.Valid();
    }
}
