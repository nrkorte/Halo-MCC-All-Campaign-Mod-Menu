#pragma once

// Helpers shared by the Halo: Reach cheat files. Everything here touches raw
// game memory, so callers must be inside SEH.

#include <cstdint>
#include <string>

#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace ReachCheats {
    constexpr int kMaxLocalPlayers = 4;

    // A Vehicles list entry other than a vehicle's default: one of its
    // model's variants, or a turret swapped onto its turretless variant
    // (a map's Warthog may lack the variant for a turret the mission
    // attaches itself, like Tip of the Spear's Gauss). Its list id is
    // kVehicleChoiceId | index (no tag has salt 0xFFFF).
    constexpr uint32_t kVehicleChoiceId = 0xFFFF0000;
    struct VehicleChoice {
        uint32_t vehicle = 0xFFFFFFFF;
        uint32_t variant = 0;          // model variant name (string id), 0 = default
        uint32_t turret = 0xFFFFFFFF;  // vehicle to attach, or -1
        uint32_t parentMarker = 0;     // where (string ids)
        uint32_t childMarker = 0;
        std::string name;              // as listed
    };
    // cheats.cpp: the entry for a list id; false if it isn't one.
    bool FindVehicleChoice(uint32_t id, VehicleChoice& out);
    // cheats.cpp: whether only AI may drive this vehicle tag (Pelican, Phantom).
    bool IsAiOnlyVehicle(uint32_t vehicle);
    // cheats.cpp: lets this unit be ejected despite "Cannot be ejected" (the
    // mod's own seat changes eject; -1 = none). Game thread only.
    void AllowEject(uint32_t unit);

    using Games::Field;
    using Games::Restorable;
    using Games::Scale;

    // Reach's data arrays: a fixed pool of entries, each starting with a
    // 16-bit salt (0 = free). A datum is (salt << 16) | index.
    struct Array {
        char* header = nullptr;

        int HighWater() const { return Field<int32_t>(header, REACH_ARRAY_HIGH_WATER); }
        char* Entry(int index) const {
            return Field<char*>(header, REACH_ARRAY_DATA) + Field<int64_t>(header, REACH_ARRAY_ENTRY_SIZE) * index;
        }
        bool Valid() const { return header && Field<char*>(header, REACH_ARRAY_DATA); }

        // The entry for a datum, or nullptr if stale/invalid (RVA 0x137C4).
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
        if (type) *type = Field<uint8_t>(entry, REACH_OBJECT_ENTRY_TYPE);
        return Field<char*>(entry, REACH_OBJECT_ENTRY_OBJECT);
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
        return Field<int16_t>(object, REACH_OBJECT_SECTIONS_SIZE) / REACH_SECTION_SIZE;
    }

    inline float* SectionVitality(char* object, int section) {
        if (section < 0 || section >= SectionCount(object)) return nullptr;
        char* sections = object + Field<int16_t>(object, REACH_OBJECT_SECTIONS_OFFSET);
        return &Field<float>(sections, section * REACH_SECTION_SIZE + REACH_SECTION_VITALITY);
    }

    inline float* Health(char* object) { return SectionVitality(object, 0); }

    // The shield section, or nullptr if the object has no shield (RVA 0x49DDEC).
    inline float* Shield(char* object) {
        char* definition = ReachTags::Get(Field<uint32_t>(object, REACH_OBJECT_TAG_DATUM));
        char* model = definition ? ReachTags::Get(Field<uint32_t>(definition, REACH_OBJE_MODEL + REACH_TAGREF_DATUM))
                                 : nullptr;
        if (!model || Field<float>(model, REACH_HLMT_MAX_SHIELD) <= 0.0f) return nullptr;
        int count = SectionCount(object);
        for (int s = 0; s < count; ++s) {
            char* def = ReachTags::BlockElement(model, REACH_HLMT_SECTIONS, REACH_HLMT_SECTION_SIZE, s);
            if (!def) break;
            int32_t marker = Field<int32_t>(def, REACH_HLMT_SECTION_SHIELD);
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
            uint32_t datum = Field<uint32_t>(player, REACH_PLAYER_UNIT_DATUM);
            char* unit = ObjectFromDatum(objects, datum);
            if (!unit) continue;  // dead / respawning
            out.units[out.count] = unit;
            out.datums[out.count++] = datum;
        }
        return out;
    }

    // Reach's allegiances never change: player and human are allies, every
    // other team is friends only with itself (see offsets.h). So whether two
    // teams are friends follows from the teams alone.
    inline bool HumanSide(int team) { return team == REACH_TEAM_PLAYER || team == REACH_TEAM_HUMAN; }
    inline bool Friends(int a, int b) { return a == b || (HumanSide(a) && HumanSide(b)); }

    // Player 1's team ("Your team" can change it), player if unknown.
    inline int PlayerTeam(const Players& p) {
        return p.count > 0 ? Field<int8_t>(p.units[0], REACH_UNIT_TEAM) : REACH_TEAM_PLAYER;
    }

    // An AI-controlled enemy biped that the game lets die: not on the
    // player's side (humans normally, the Covenant if they switched).
    inline bool IsKillableEnemy(char* object, int type, const Players& p) {
        if (type != REACH_OBJECT_TYPE_BIPED || p.Contains(object)) return false;
        if (Friends(Field<int8_t>(object, REACH_UNIT_TEAM), PlayerTeam(p))) return false;
        if (Field<uint32_t>(object, REACH_UNIT_ACTOR) == 0xFFFFFFFF) return false;
        uint32_t flags = Field<uint32_t>(object, REACH_OBJECT_DAMAGE_FLAGS);
        return !(flags & (REACH_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | REACH_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE));
    }

    // The vehicle (or turret) a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, const Array& objects) {
        return ObjectOfType(objects, Field<uint32_t>(unit, REACH_OBJECT_PARENT), REACH_OBJECT_TYPE_VEHICLE);
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
            uint32_t child = Field<uint32_t>(out[i], REACH_OBJECT_FIRST_CHILD);
            for (int n = 0; child != 0xFFFFFFFF && n < 32 && count < max; ++n) {
                int type = -1;
                char* object = ObjectFromDatum(objects, child, &type);
                if (!object) break;
                if (type == REACH_OBJECT_TYPE_VEHICLE) out[count++] = object;
                child = Field<uint32_t>(object, REACH_OBJECT_NEXT_SIBLING);
            }
        }
        return count;
    }

    // The unit's weapon in a slot, or nullptr.
    inline char* WeaponInSlot(char* unit, const Array& objects, int slot, uint32_t* datumOut = nullptr) {
        uint32_t datum = Field<uint32_t>(unit, REACH_UNIT_WEAPONS + slot * 4);
        char* weapon = ObjectOfType(objects, datum, REACH_OBJECT_TYPE_WEAPON);
        if (weapon && datumOut) *datumOut = datum;
        return weapon;
    }

    // True for weapons that only melee (energy sword, gravity hammer): the
    // aimbot leaves them alone.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? ReachTags::Get(Field<uint32_t>(weapon, REACH_OBJECT_TAG_DATUM)) : nullptr;
        return tag && (Field<uint32_t>(tag, REACH_WEAP_FLAGS) & REACH_WEAP_FLAG_MELEE_ONLY);
    }

    // The unit's currently held weapon, or nullptr.
    inline char* HeldWeapon(char* unit, const Array& objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, REACH_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= REACH_UNIT_WEAPON_COUNT) return nullptr;
        return WeaponInSlot(unit, objects, index, datumOut);
    }
}
