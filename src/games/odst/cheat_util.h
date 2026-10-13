#pragma once

// Helpers shared by the ODST cheat files. Everything here touches raw game
// memory, so callers must be inside SEH.

#include <cstdint>

#include "games/blam.h"
#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace OdstCheats {
    constexpr int kMaxLocalPlayers = 4;

    using Games::Field;
    using Games::Restorable;
    using Games::Scale;

    // Local players' units (alive ones only).
    struct Players {
        char* units[kMaxLocalPlayers] = {};
        uint32_t datums[kMaxLocalPlayers] = {};
        int count = 0;
        uint32_t allies = 0;  // bit per team friendly to player 1 (see ReadAllies)

        bool Contains(char* object) const {
            for (int i = 0; i < count; ++i)
                if (units[i] == object) return true;
            return false;
        }
    };

    inline Players FindPlayers(Blam::DataArray* players, Blam::DataArray* objects) {
        Players out;
        int limit = players->highWater < 16 ? players->highWater : 16;
        for (int i = 0; i < limit && out.count < kMaxLocalPlayers; ++i) {
            char* player = players->Entry(i);
            if (*(uint16_t*)player == 0) continue;  // free slot
            uint32_t datum = Field<uint32_t>(player, ODST_PLAYER_UNIT_DATUM);
            char* unit = Blam::ObjectFromDatum(objects, datum);
            if (!unit) continue;  // dead / respawning
            out.units[out.count] = unit;
            out.datums[out.count++] = datum;
        }
        return out;
    }

    // Which teams player 1's team treats as friends, from the game's own
    // allegiance table (campaign scripts change it, e.g. when the Elites join).
    inline void ReadAllies(Players& p, char* gameGlobals, char* allegiance) {
        p.allies = (1u << ODST_TEAM_PLAYER) | (1u << ODST_TEAM_HUMAN);
        if (p.count == 0) return;
        int16_t team = Field<int16_t>(p.units[0], ODST_UNIT_TEAM);
        if (team < 0 || team >= ODST_TEAM_COUNT) return;
        if (gameGlobals && allegiance && Field<uint8_t>(gameGlobals, ODST_GAME_GLOBALS_MODE) == ODST_GAME_MODE_CAMPAIGN)
            // Campaign: the matrix alone ("Your team" rewrites it, so marines stop counting as allies).
            p.allies = (1u << team) | Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + team * 4);
        else
            p.allies |= 1u << team;
    }

    // An AI-controlled enemy biped that the game lets die (not an ally, not
    // a story character kept alive by script).
    inline bool IsKillableEnemy(char* object, const Players& p) {
        if (Field<uint8_t>(object, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_BIPED) return false;
        if (p.Contains(object)) return false;
        int16_t team = Field<int16_t>(object, ODST_UNIT_TEAM);
        if (team >= 0 && team < ODST_TEAM_COUNT && (p.allies & (1u << team))) return false;
        if (Field<int32_t>(object, ODST_UNIT_ACTOR_INDEX) == -1) return false;
        uint32_t flags = Field<uint32_t>(object, ODST_OBJECT_DAMAGE_FLAGS);
        return !(flags & (ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | ODST_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE));
    }

    // The vehicle (or turret) a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, Blam::DataArray* objects) {
        char* parent = Blam::ObjectFromDatum(objects, Field<uint32_t>(unit, ODST_OBJECT_PARENT));
        return parent && Field<uint8_t>(parent, ODST_OBJECT_TYPE) == ODST_OBJECT_TYPE_VEHICLE ? parent : nullptr;
    }

    // Every vehicle that makes up the one a unit is seated in: up to the top
    // vehicle, then everything attached to it (turrets are separate vehicles
    // holding their own weapons). Returns the count written to `out`.
    inline int VehicleParts(char* unit, Blam::DataArray* objects, char** out, int max) {
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
            uint32_t child = Field<uint32_t>(out[i], ODST_OBJECT_FIRST_CHILD);
            for (int n = 0; child != 0xFFFFFFFF && n < 32 && count < max; ++n) {
                char* object = Blam::ObjectFromDatum(objects, child);
                if (!object) break;
                if (Field<uint8_t>(object, ODST_OBJECT_TYPE) == ODST_OBJECT_TYPE_VEHICLE) out[count++] = object;
                child = Field<uint32_t>(object, ODST_OBJECT_NEXT_SIBLING);
            }
        }
        return count;
    }

    // True for weapons that only melee (energy sword, gravity hammer): the
    // aimbot leaves them alone.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM)) : nullptr;
        return tag && (Field<uint32_t>(tag, ODST_WEAP_FLAGS) & ODST_WEAP_FLAG_MELEE_ONLY);
    }

    // The unit's currently held weapon, or nullptr.
    inline char* HeldWeapon(char* unit, Blam::DataArray* objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, ODST_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= ODST_UNIT_WEAPON_COUNT) return nullptr;
        uint32_t datum = Field<uint32_t>(unit, ODST_UNIT_WEAPONS + index * 4);
        char* weapon = Blam::ObjectFromDatum(objects, datum);
        if (!weapon || Field<uint8_t>(weapon, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_WEAPON) return nullptr;
        if (datumOut) *datumOut = datum;
        return weapon;
    }

    // Skulls page numbering: 0-8 the primary skulls, 9-15 the secondary
    // (MCC's mask bits 0-15), 16 Bandanna (MCC's mask, bit 23). Acrophobia is
    // the Fly toggle. Written to both copies, as the script commands do.
    constexpr int kMenuSkullCount = 17;
    inline uint64_t MenuSkulls(char* gameGlobals) {
        uint64_t mcc = Field<uint64_t>(gameGlobals, ODST_GAME_GLOBALS_SKULLS);
        return (mcc & 0xFFFFull) | (((mcc >> ODST_SKULL_BANDANNA) & 1) << 16);
    }
    inline void SetMenuSkulls(char* gameGlobals, uint64_t mask) {
        uint32_t& primary = Field<uint32_t>(gameGlobals, ODST_GAME_GLOBALS_PRIMARY_SKULLS);
        uint32_t& secondary = Field<uint32_t>(gameGlobals, ODST_GAME_GLOBALS_SECONDARY_SKULLS);
        uint64_t& mcc = Field<uint64_t>(gameGlobals, ODST_GAME_GLOBALS_SKULLS);
        primary = (primary & ~0x1FFu) | (uint32_t)(mask & 0x1FF);
        secondary = (secondary & ~0x7Fu) | (uint32_t)((mask >> 9) & 0x7F);
        uint64_t bandanna = 1ull << ODST_SKULL_BANDANNA;
        mcc = (mcc & ~0xFFFFull) | (mask & 0xFFFF);
        mcc = (mask >> 16) & 1 ? mcc | bandanna : mcc & ~bandanna;
    }
}
