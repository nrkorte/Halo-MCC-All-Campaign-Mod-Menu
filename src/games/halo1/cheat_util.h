#pragma once

// Helpers shared by the Halo: CE cheat files. Everything here touches raw
// game memory, so callers must be inside SEH.

#include <cstdint>

#include "games/memory.h"
#include "offsets.h"
#include "tags.h"

namespace Halo1Cheats {
    constexpr int kMaxLocalPlayers = 4;

    using Games::Field;
    using Games::Restorable;
    using Games::Scale;

    // CE's data arrays: entries start with a 16-bit salt (0 = free); the
    // entries live at a relative offset from the header.
    struct Array {
        char* header = nullptr;

        int Count() const { return Field<int16_t>(header, HALO1_ARRAY_COUNT); }
        char* Entry(int index) const {
            return header + Field<int32_t>(header, HALO1_ARRAY_DATA_OFFSET) +
                   (int64_t)Field<uint16_t>(header, HALO1_ARRAY_ENTRY_SIZE) * index;
        }
        char* Get(uint32_t datum) const {
            if (datum == 0xFFFFFFFF) return nullptr;
            int16_t index = (int16_t)(datum & 0xFFFF);
            if (index < 0 || index >= Count()) return nullptr;
            char* entry = Entry(index);
            uint16_t salt = *(uint16_t*)entry;
            return salt != 0 && salt == (datum >> 16) ? entry : nullptr;
        }
    };

    // The object table, plus the heap the object data lives in.
    struct Objects {
        Array table;
        char* heap = nullptr;

        bool Valid() const { return table.header && heap; }

        char* FromEntry(char* entry, int* type = nullptr) const {
            int32_t offset = Field<int32_t>(entry, HALO1_OBJECT_ENTRY_OFFSET);
            if (offset == -1) return nullptr;
            if (type) *type = Field<uint8_t>(entry, HALO1_OBJECT_ENTRY_TYPE);
            return heap + HALO1_OBJECT_HEAP_HEADER + offset;
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

    inline Players FindPlayers(const Array& players, const Objects& objects) {
        Players out;
        int limit = players.Count() < 16 ? players.Count() : 16;
        for (int i = 0; i < limit && out.count < kMaxLocalPlayers; ++i) {
            char* player = players.Entry(i);
            if (*(uint16_t*)player == 0) continue;  // free slot
            uint32_t datum = Field<uint32_t>(player, HALO1_PLAYER_UNIT);
            char* unit = objects.Get(datum);
            if (!unit) continue;  // dead / respawning
            out.units[out.count] = unit;
            out.datums[out.count++] = datum;
        }
        return out;
    }

    // Whether two teams are friends in the game's allegiance matrix (a team is
    // its own friend); falls back to "same team" if it can't be read.
    inline bool TeamsFriends(char* module, int a, int b) {
        if (a == b) return true;
        char* g = *(char**)(module + OFFSET_HALO1_ALLEGIANCE);
        if (!g || a < 0 || b < 0 || a > 9 || b > 9) return false;
        int idx = a * 10 + b;
        return (((uint32_t*)(g + HALO1_ALLEGIANCE_FRIENDLY))[idx >> 5] >> (idx & 31)) & 1;
    }

    // An AI-controlled biped: it has an actor (or a swarm actor, e.g. a Flood
    // infection form). Players and dead units count too; callers filter.
    inline bool IsAiUnit(char* object, int type) {
        return type == HALO1_OBJECT_TYPE_BIPED &&
               (Field<uint32_t>(object, HALO1_UNIT_ACTOR) != 0xFFFFFFFF ||
                Field<uint32_t>(object, HALO1_UNIT_SWARM_ACTOR) != 0xFFFFFFFF);
    }

    // halo1.dll's base, for reading the allegiance matrix from the helpers here.
    inline char* g_allegianceModule = nullptr;

    // On the player's side: friends of player 1's team in the game's allegiance
    // matrix (so "Your team" changes who counts), falling back to the player and
    // human teams.
    inline bool IsAlly(char* object, const Players& p) {
        int team = Field<int16_t>(object, HALO1_OBJECT_TEAM);
        int mine = p.count > 0 ? Field<int16_t>(p.units[0], HALO1_OBJECT_TEAM) : HALO1_TEAM_PLAYER;
        if (g_allegianceModule) return TeamsFriends(g_allegianceModule, team, mine);
        return team == HALO1_TEAM_PLAYER || team == HALO1_TEAM_HUMAN || team == mine;
    }

    // An AI-controlled enemy biped the game lets die.
    inline bool IsKillableEnemy(char* object, int type, const Players& p) {
        if (type != HALO1_OBJECT_TYPE_BIPED || p.Contains(object)) return false;
        if (IsAlly(object, p) || !IsAiUnit(object, type)) return false;
        return !(Field<uint16_t>(object, HALO1_OBJECT_DAMAGE_FLAGS) &
                 (HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO1_DAMAGE_FLAG_DEAD));
    }

    inline char* WeaponInSlot(char* unit, const Objects& objects, int slot, uint32_t* datumOut = nullptr) {
        uint32_t datum = Field<uint32_t>(unit, HALO1_UNIT_WEAPONS + slot * 4);
        char* weapon = objects.OfType(datum, HALO1_OBJECT_TYPE_WEAPON);
        if (weapon && datumOut) *datumOut = datum;
        return weapon;
    }

    // The vehicle a unit is seated in, or nullptr.
    inline char* VehicleOf(char* unit, const Objects& objects) {
        return objects.OfType(Field<uint32_t>(unit, HALO1_OBJECT_PARENT), HALO1_OBJECT_TYPE_VEHICLE);
    }

    inline char* HeldWeapon(char* unit, const Objects& objects, uint32_t* datumOut = nullptr) {
        int8_t index = Field<int8_t>(unit, HALO1_UNIT_CURRENT_WEAPON);
        if (index < 0 || index >= HALO1_UNIT_WEAPON_COUNT) return nullptr;
        return WeaponInSlot(unit, objects, index, datumOut);
    }

    // True for a weapon with nothing to shoot: no triggers, so only its melee
    // blow does anything. The aimbot leaves those alone. CE's campaign has no
    // energy sword or gravity hammer, so in practice this never matches; it
    // covers custom maps and keeps the check the same as the other titles'.
    inline bool IsMeleeOnly(char* weapon) {
        char* tag = weapon ? Halo1Tags::Get(Field<uint32_t>(weapon, HALO1_OBJECT_TAG_DATUM)) : nullptr;
        return tag && Halo1Tags::BlockCount(tag, HALO1_WEAP_TRIGGERS) == 0;
    }

    // Reads the object table and players from halo1.dll's globals.
    inline bool ReadTables(char* module, Array& players, Objects& objects) {
        players.header = *(char**)(module + OFFSET_HALO1_PLAYERS);
        objects.table.header = *(char**)(module + OFFSET_HALO1_OBJECTS);
        objects.heap = *(char**)(module + OFFSET_HALO1_OBJECT_HEAP);
        return players.header && objects.Valid();
    }
}
