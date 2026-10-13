#pragma once

// Helpers shared by the Halo 3 cheat files. Everything here touches raw game
// memory, so callers must be inside SEH.

#include <cstdint>

#include "games/blam.h"
#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace Halo3Cheats {
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
            uint32_t datum = Field<uint32_t>(player, HALO3_PLAYER_UNIT_DATUM);
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
        p.allies = (1u << HALO3_TEAM_PLAYER) | (1u << HALO3_TEAM_HUMAN);
        if (p.count == 0) return;
        int16_t team = Field<int16_t>(p.units[0], HALO3_UNIT_TEAM);
        if (team < 0 || team >= HALO3_TEAM_COUNT) return;
        if (gameGlobals && allegiance && Field<uint8_t>(gameGlobals, HALO3_GAME_GLOBALS_MODE) == HALO3_GAME_MODE_CAMPAIGN)
            // Campaign: the matrix alone ("Your team" rewrites it, so marines stop counting as allies).
            p.allies = (1u << team) | Field<uint32_t>(allegiance, HALO3_ALLEGIANCE_FRIENDS + team * 4);
        else
            p.allies |= 1u << team;
    }

    // An AI-controlled enemy biped that the game lets die (not an ally, not
    // a story character kept alive by script).
    inline bool IsKillableEnemy(char* object, const Players& p) {
        if (Field<uint8_t>(object, HALO3_OBJECT_TYPE) != HALO3_OBJECT_TYPE_BIPED) return false;
        if (p.Contains(object)) return false;
        int16_t team = Field<int16_t>(object, HALO3_UNIT_TEAM);
        if (team >= 0 && team < HALO3_TEAM_COUNT && (p.allies & (1u << team))) return false;
        if (Field<int32_t>(object, HALO3_UNIT_ACTOR_INDEX) == -1) return false;
        uint32_t flags = Field<uint32_t>(object, HALO3_OBJECT_DAMAGE_FLAGS);
        return !(flags & (HALO3_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE));
    }

    // The vehicle (or turret) a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, Blam::DataArray* objects) {
        char* parent = Blam::ObjectFromDatum(objects, Field<uint32_t>(unit, HALO3_OBJECT_PARENT));
        return parent && Field<uint8_t>(parent, HALO3_OBJECT_TYPE) == HALO3_OBJECT_TYPE_VEHICLE ? parent : nullptr;
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
            uint32_t child = Field<uint32_t>(out[i], HALO3_OBJECT_FIRST_CHILD);
            for (int n = 0; child != 0xFFFFFFFF && n < 32 && count < max; ++n) {
                char* object = Blam::ObjectFromDatum(objects, child);
                if (!object) break;
                if (Field<uint8_t>(object, HALO3_OBJECT_TYPE) == HALO3_OBJECT_TYPE_VEHICLE) out[count++] = object;
                child = Field<uint32_t>(object, HALO3_OBJECT_NEXT_SIBLING);
            }
        }
        return count;
    }

    // The unit's currently held weapon, or nullptr.
    inline char* HeldWeapon(char* unit, Blam::DataArray* objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, HALO3_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= HALO3_UNIT_WEAPON_COUNT) return nullptr;
        uint32_t datum = Field<uint32_t>(unit, HALO3_UNIT_WEAPONS + index * 4);
        char* weapon = Blam::ObjectFromDatum(objects, datum);
        if (!weapon || Field<uint8_t>(weapon, HALO3_OBJECT_TYPE) != HALO3_OBJECT_TYPE_WEAPON) return nullptr;
        if (datumOut) *datumOut = datum;
        return weapon;
    }

    // True for weapons that only melee (energy sword, gravity hammer): the
    // aimbot leaves them alone. Takes the weapon object.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? Halo3Tags::Get(Field<uint32_t>(weapon, HALO3_OBJECT_TAG_DATUM)) : nullptr;
        return tag && (Field<uint32_t>(tag, HALO3_WEAP_FLAGS) & HALO3_WEAP_FLAG_MELEE_ONLY);
    }

    // Skulls page numbering: 0-8 the primary skulls, 9-13 the secondary, 14
    // Bandanna (MCC's mask, bit 23). Acrophobia is the Fly toggle.
    constexpr int kMenuSkullCount = 15;
    inline uint64_t MenuSkulls(char* gameGlobals) {
        uint32_t primary = Field<uint32_t>(gameGlobals, HALO3_GAME_GLOBALS_PRIMARY_SKULLS);
        uint32_t secondary = Field<uint32_t>(gameGlobals, HALO3_GAME_GLOBALS_SECONDARY_SKULLS);
        uint64_t mcc = Field<uint64_t>(gameGlobals, HALO3_GAME_GLOBALS_SKULLS);
        return (primary & 0x1FFull) | ((uint64_t)(secondary & 0x1F) << 9) |
               (((mcc >> HALO3_SKULL_BANDANNA) & 1) << 14);
    }
    inline void SetMenuSkulls(char* gameGlobals, uint64_t mask) {
        uint32_t& primary = Field<uint32_t>(gameGlobals, HALO3_GAME_GLOBALS_PRIMARY_SKULLS);
        uint32_t& secondary = Field<uint32_t>(gameGlobals, HALO3_GAME_GLOBALS_SECONDARY_SKULLS);
        uint64_t& mcc = Field<uint64_t>(gameGlobals, HALO3_GAME_GLOBALS_SKULLS);
        primary = (primary & ~0x1FFu) | (uint32_t)(mask & 0x1FF);
        secondary = (secondary & ~0x1Fu) | (uint32_t)((mask >> 9) & 0x1F);
        uint64_t bandanna = 1ull << HALO3_SKULL_BANDANNA;
        mcc = (mask >> 14) & 1 ? mcc | bandanna : mcc & ~bandanna;
    }
}
