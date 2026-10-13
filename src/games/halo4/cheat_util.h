#pragma once

// Helpers shared by the Halo 4 cheat files. Everything here touches raw
// game memory, so callers must be inside SEH.

#include <cstdint>

#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace Halo4Internal {
    bool TeamsFriends(void** state, int teamA, int teamB);  // spawn.cpp
}

namespace Halo4Cheats {
    constexpr int kMaxLocalPlayers = 4;

    // The game state the enemy test reads the allegiance table from (set each
    // tick); null = fall back to player + human being allies.
    inline void** g_friendState = nullptr;

    using Games::Field;
    using Games::Restorable;
    using Games::Scale;

    // Data arrays (Reach's layout): a fixed pool of entries, each starting with a
    // 16-bit salt (0 = free). A datum is (salt << 16) | index.
    struct Array {
        char* header = nullptr;

        int HighWater() const { return Field<int32_t>(header, HALO4_ARRAY_HIGH_WATER); }
        char* Entry(int index) const {
            return Field<char*>(header, HALO4_ARRAY_DATA) + Field<int64_t>(header, HALO4_ARRAY_ENTRY_SIZE) * index;
        }
        bool Valid() const { return header && Field<char*>(header, HALO4_ARRAY_DATA); }

        // The entry for a datum, or nullptr if stale/invalid.
        char* Get(uint32_t datum) const {
            if (datum == 0xFFFFFFFF) return nullptr;
            uint16_t index = datum & 0xFFFF;
            if (index >= HighWater()) return nullptr;
            char* entry = Entry(index);
            uint16_t salt = *(uint16_t*)entry;
            return salt != 0 && salt == (datum >> 16) ? entry : nullptr;
        }
    };

    // Object table entries hold the type and a pointer to the object.
    inline char* ObjectFromDatum(const Array& objects, uint32_t datum, int* type = nullptr) {
        char* entry = objects.Get(datum);
        if (!entry) return nullptr;
        if (type) *type = Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE);
        return Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
    }

    inline char* ObjectOfType(const Array& objects, uint32_t datum, int wanted) {
        int type = -1;
        char* object = ObjectFromDatum(objects, datum, &type);
        return object && type == wanted ? object : nullptr;
    }

    // --- Vitality ---------------------------------------------------------
    // Damage sections: section 0 is health; the shield section comes from
    // the model tag (see offsets.h).
    inline int SectionCount(char* object) {
        return Field<int16_t>(object, HALO4_OBJECT_SECTIONS_SIZE) / HALO4_SECTION_SIZE;
    }

    inline float* SectionVitality(char* object, int section) {
        if (section < 0 || section >= SectionCount(object)) return nullptr;
        char* sections = object + Field<int16_t>(object, HALO4_OBJECT_SECTIONS_OFFSET);
        return &Field<float>(sections, section * HALO4_SECTION_SIZE + HALO4_SECTION_VITALITY);
    }

    inline float* Health(char* object) { return SectionVitality(object, 0); }

    // The shield section, or nullptr if the object has no shield (RVA 0x62B1B0).
    inline float* Shield(char* object) {
        char* definition = Halo4Tags::Get(Field<uint32_t>(object, HALO4_OBJECT_TAG_DATUM));
        char* model = definition ? Halo4Tags::Get(Field<uint32_t>(definition, HALO4_OBJE_MODEL + HALO4_TAGREF_DATUM))
                                 : nullptr;
        if (!model || Field<float>(model, HALO4_HLMT_MAX_SHIELD) <= 0.0f) return nullptr;
        int count = SectionCount(object);
        for (int s = 0; s < count; ++s) {
            char* def = Halo4Tags::BlockElement(model, HALO4_HLMT_SECTIONS, HALO4_HLMT_SECTION_SIZE, s);
            if (!def) break;
            int32_t marker = Field<int32_t>(def, HALO4_HLMT_SECTION_SHIELD);
            if (marker != 0 && marker != -1) return SectionVitality(object, s);
        }
        return nullptr;
    }

    // --- Players ----------------------------------------------------------
    // Local players' units (alive ones only).
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

    inline Players FindPlayers(const Array& players, const Array& objects) {
        Players out;
        int limit = players.HighWater() < 16 ? players.HighWater() : 16;
        for (int i = 0; i < limit && out.count < kMaxLocalPlayers; ++i) {
            char* player = players.Entry(i);
            if (*(uint16_t*)player == 0) continue;  // free slot
            uint32_t datum = Field<uint32_t>(player, HALO4_PLAYER_UNIT_DATUM);
            char* unit = ObjectFromDatum(objects, datum);
            if (!unit) continue;  // dead / respawning
            out.units[out.count] = unit;
            out.datums[out.count++] = datum;
        }
        return out;
    }

    // An AI-controlled enemy biped that the game lets die. As in Reach, the
    // campaign teams don't change sides, so the player and human teams are allies.
    inline bool IsKillableEnemy(char* object, int type, const Players& p) {
        if (type != HALO4_OBJECT_TYPE_BIPED || p.Contains(object)) return false;
        int8_t team = Field<int8_t>(object, HALO4_UNIT_TEAM);
        if (g_friendState && p.count > 0) {  // follows "Your team"
            if (Halo4Internal::TeamsFriends(g_friendState, Field<int8_t>(p.units[0], HALO4_UNIT_TEAM), team))
                return false;
        } else {
            if (team == HALO4_TEAM_PLAYER || team == HALO4_TEAM_HUMAN) return false;
            if (p.count > 0 && team == Field<int8_t>(p.units[0], HALO4_UNIT_TEAM)) return false;
        }
        if (Field<uint32_t>(object, HALO4_UNIT_ACTOR) == 0xFFFFFFFF) return false;
        uint32_t flags = Field<uint32_t>(object, HALO4_OBJECT_DAMAGE_FLAGS);
        return !(flags & (HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO4_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE));
    }

    // The vehicle (or turret) a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, const Array& objects) {
        return ObjectOfType(objects, Field<uint32_t>(unit, HALO4_OBJECT_PARENT), HALO4_OBJECT_TYPE_VEHICLE);
    }

    // Every vehicle that makes up the one a unit is seated in: up to the top
    // vehicle, then everything attached to it (a Scorpion's cannon and gun
    // turrets are separate vehicles holding their own weapons). Returns the
    // count written to `out`.
    inline int VehicleParts(char* unit, const Array& objects, char** out, int max) {
        char* top = VehicleOf(unit, objects);
        for (int depth = 0; top && depth < 4; ++depth) {
            char* up = VehicleOf(top, objects);
            if (!up) break;
            top = up;
        }
        if (!top || max <= 0) return 0;
        int count = 0;
        out[count++] = top;
        for (int i = 0; i < count; ++i) {  // breadth-first over the attachments
            uint32_t child = Field<uint32_t>(out[i], HALO4_OBJECT_FIRST_CHILD);
            for (int n = 0; child != 0xFFFFFFFF && n < 32 && count < max; ++n) {
                int type = -1;
                char* object = ObjectFromDatum(objects, child, &type);
                if (!object) break;
                if (type == HALO4_OBJECT_TYPE_VEHICLE) out[count++] = object;
                child = Field<uint32_t>(object, HALO4_OBJECT_NEXT_SIBLING);
            }
        }
        return count;
    }

    // The unit's weapon in a slot, or nullptr.
    inline char* WeaponInSlot(char* unit, const Array& objects, int slot, uint32_t* datumOut = nullptr) {
        uint32_t datum = Field<uint32_t>(unit, HALO4_UNIT_WEAPONS + slot * 4);
        char* weapon = ObjectOfType(objects, datum, HALO4_OBJECT_TYPE_WEAPON);
        if (weapon && datumOut) *datumOut = datum;
        return weapon;
    }

    // True for weapons that only melee (the energy sword and the like): the
    // aimbot leaves them alone.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
        return tag && (Field<uint32_t>(tag, HALO4_WEAP_FLAGS) & HALO4_WEAP_FLAG_MELEE_ONLY);
    }

    // The unit's currently held weapon, or nullptr.
    inline char* HeldWeapon(char* unit, const Array& objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, HALO4_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= HALO4_UNIT_WEAPON_COUNT) return nullptr;
        return WeaponInSlot(unit, objects, index, datumOut);
    }
}
