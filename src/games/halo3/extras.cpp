// Halo 3: Disable friendly fire, Time stop and "Invulnerable AI can die" (offsets and
// how they were found in extras_offsets.h). Ported from Reach / Halo 2 (see NOTES.md).
#include "extras.h"

#include <windows.h>

#include "MinHook.h"
#include "core/log.h"
#include "extras_offsets.h"
#include "internal.h"

using namespace Halo3Cheats;

namespace {
    char* g_module = nullptr;
    int32_t g_tlsIndex = -1;
    void** g_state = nullptr;  // the game thread's TLS block (captured in Tick)

    // Read by the hooks (game thread), written by Tick.
    volatile bool g_noFriendlyFire = false;
    volatile bool g_timeStopActive = false;

    struct Hook {
        void* target = nullptr;
        bool Install(int rva, void* detour, void** original, const char* what) {
            void* at = g_module + rva;
            if (MH_CreateHook(at, detour, original) == MH_OK && MH_EnableHook(at) == MH_OK) {
                target = at;
                return true;
            }
            MH_RemoveHook(at);
            LOG("Halo 3: failed to hook %s at %p", what, at);
            return false;
        }
        void Remove() {
            if (!target) return;
            MH_DisableHook(target);
            MH_RemoveHook(target);
            target = nullptr;
        }
    };
    Hook g_damageHook, g_updateHook, g_postUpdateHook, g_barrelsHook;

    // The calling thread's game state if it is the game thread, else nullptr.
    void** GameState() {
        if (g_tlsIndex < 0 || !g_state) return nullptr;
        return Blam::ThreadTlsBlock(g_tlsIndex) == g_state ? g_state : nullptr;
    }


    // --- Revive table ---------------------------------------------------------------
    // Live (010_jungle): the AI globals (TLS slot 6) hold 7 entries of 0x10 bytes from +0x40C, {unit
    // datum, second dword (0x592feb90 for the Arbiter's), 0, 0}, free ones all -1. Killing the Arbiter
    // didn't touch his damage flags (0 / 0x8 / 0x208); about 4 s after his health hit 0 the same unit
    // had health 1.0 and a new actor, and his entry (with the words before it) had been reset to -1.
    // The mod takes AI units off the table while the toggle is on (Reach's revive list worked the
    // same way) and puts them back when it goes off.
    struct RemovedEntry { uint32_t unit, second; };
    RemovedEntry g_removed[16] = {};
    int g_removedCount = 0;

    void ReviveTableTick(void** state, const Players& p, Blam::DataArray* objects, bool on) {
        char* ai = (char*)state[HALO3_TLS_SLOT_AI_GLOBALS];
        if (!ai) return;
        for (int i = 0; i < HALO3_REVIVE_COUNT; ++i) {
            uint32_t* entry = (uint32_t*)(ai + HALO3_REVIVE_TABLE + i * HALO3_REVIVE_STRIDE);
            if (on) {
                if (entry[0] == 0xFFFFFFFF) continue;
                char* unit = Blam::ObjectFromDatum(objects, entry[0]);
                if (unit && p.Contains(unit)) continue;  // never a player
                if (g_removedCount < _countof(g_removed)) g_removed[g_removedCount++] = {entry[0], entry[1]};
                LOG("Halo 3: unit %08X taken off the revive table (second word %08X)", entry[0], entry[1]);
                entry[0] = 0xFFFFFFFF;
                entry[1] = 0xFFFFFFFF;
            }
        }
        if (!on && g_removedCount > 0) {  // switched off: put the living back into free entries
            for (int n = 0; n < g_removedCount; ++n) {
                if (!Blam::ObjectFromDatum(objects, g_removed[n].unit)) continue;
                for (int i = 0; i < HALO3_REVIVE_COUNT; ++i) {
                    uint32_t* entry = (uint32_t*)(ai + HALO3_REVIVE_TABLE + i * HALO3_REVIVE_STRIDE);
                    if (entry[0] != 0xFFFFFFFF) continue;
                    entry[0] = g_removed[n].unit;
                    entry[1] = g_removed[n].second;
                    break;
                }
            }
            g_removedCount = 0;
        }
    }

    // --- Disable friendly fire ---------------------------------------------------

    using DamageFn = void(__fastcall*)(void* damage, uint64_t victim, uint64_t region, uint64_t node, uint64_t a5, uint64_t a6);
    DamageFn g_origDamage = nullptr;

    bool IsAllyTeam(const Players& p, int team) {
        return team >= 0 && team < HALO3_TEAM_COUNT && (p.allies & (1u << team)) != 0;
    }

    // What an object is as a damage victim: the players' own units (or a vehicle one sits
    // in) and the allied AI's. Walks the object and what's attached (seated units).
    void Classify(Blam::DataArray* objects, const Players& p, char* object, int depth, bool& player, bool& ally) {
        uint8_t type = Field<uint8_t>(object, HALO3_OBJECT_TYPE);
        if (type == HALO3_OBJECT_TYPE_BIPED || type == HALO3_OBJECT_TYPE_VEHICLE) {
            if (p.Contains(object)) player = true;
            else if (Field<uint32_t>(object, HALO3_UNIT_ACTOR_INDEX) != 0xFFFFFFFF &&
                     IsAllyTeam(p, Field<int16_t>(object, HALO3_UNIT_TEAM)))
                ally = true;
        }
        if (depth >= 3) return;
        uint32_t child = Field<uint32_t>(object, HALO3_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 16; ++n) {
            char* c = Blam::ObjectFromDatum(objects, child);
            if (!c) break;
            uint8_t childType = Field<uint8_t>(c, HALO3_OBJECT_TYPE);
            if (childType == HALO3_OBJECT_TYPE_BIPED || childType == HALO3_OBJECT_TYPE_VEHICLE)
                Classify(objects, p, c, depth + 1, player, ally);
            child = Field<uint32_t>(c, HALO3_OBJECT_NEXT_SIBLING);
        }
    }

    volatile int g_blockedLogged = 0;

    // True if this hit is between player(s)' side and allied AI, either way (not ally against ally).
    bool DropHit(void* damage, uint32_t victim, int* why) {
        __try {
            void** state = GameState();
            if (!state) return false;
            auto players = (Blam::DataArray*)state[HALO3_TLS_SLOT_PLAYERS];
            auto objects = (Blam::DataArray*)state[HALO3_TLS_SLOT_OBJECTS];
            if (!players || !objects || !players->data || !objects->data) return false;
            Players p = FindPlayers(players, objects);
            if (p.count == 0) return false;
            ReadAllies(p, (char*)state[HALO3_TLS_SLOT_GAME_GLOBALS], (char*)state[HALO3_TLS_SLOT_ALLEGIANCE]);

            char* data = (char*)damage;
            uint32_t ownerPlayer = Field<uint32_t>(data, HALO3_DAMAGE_OWNER_PLAYER);
            uint32_t ownerUnit = Field<uint32_t>(data, HALO3_DAMAGE_OWNER_UNIT);
            int ownerTeam = Field<int8_t>(data, HALO3_DAMAGE_OWNER_TEAM);
            bool ownerIsPlayer = ownerPlayer != 0xFFFFFFFF;
            for (int i = 0; i < p.count; ++i) ownerIsPlayer |= ownerUnit == p.datums[i];
            // An allied AI's shot still counts after the shooter died (its unit datum is gone, the team was recorded).
            bool ownerIsAlly = false;
            if (!ownerIsPlayer && ownerUnit != 0xFFFFFFFF) {
                if (ownerTeam < 0)
                    if (char* owner = Blam::ObjectFromDatum(objects, ownerUnit)) ownerTeam = Field<int16_t>(owner, HALO3_UNIT_TEAM);
                ownerIsAlly = IsAllyTeam(p, ownerTeam);
            }
            if (!ownerIsPlayer && !ownerIsAlly) return false;

            char* object = Blam::ObjectFromDatum(objects, victim);
            if (!object) return false;
            bool vPlayer = false, vAlly = false;
            Classify(objects, p, object, 0, vPlayer, vAlly);
            if (ownerIsPlayer && vAlly && !vPlayer) { *why = 1; return true; }
            if (ownerIsAlly && vPlayer) { *why = 2; return true; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    void __fastcall DamageDetour(void* damage, uint64_t victim, uint64_t region, uint64_t node, uint64_t a5, uint64_t a6) {
        int why = 0;
        if (g_noFriendlyFire && damage && DropHit(damage, (uint32_t)victim, &why)) {
            if (g_blockedLogged < 12) {
                g_blockedLogged = g_blockedLogged + 1;
                LOG("Halo 3: friendly fire blocked (%s) on object %08X, owner player %08X unit %08X team %d",
                    why == 1 ? "player -> ally" : "ally -> player", (uint32_t)victim,
                    *(uint32_t*)((char*)damage + HALO3_DAMAGE_OWNER_PLAYER),
                    *(uint32_t*)((char*)damage + HALO3_DAMAGE_OWNER_UNIT),
                    (int)*(int8_t*)((char*)damage + HALO3_DAMAGE_OWNER_TEAM));
            }
            return;
        }
        g_origDamage(damage, victim, region, node, a5, a6);
    }

    // --- Time stop -----------------------------------------------------------------

    constexpr int kMaxFrozen = 4096;  // object table indices
    uint32_t g_frozen[kMaxFrozen];    // datum frozen at each table index, or -1; game thread only

    void ClearFrozen() {
        for (auto& d : g_frozen) d = 0xFFFFFFFF;
    }
    bool Frozen(uint32_t datum) {
        uint32_t index = datum & 0xFFFF;
        return index < kMaxFrozen && g_frozen[index] == datum;
    }

    using ObjectUpdateFn = uint8_t(__fastcall*)(uint32_t datum);
    ObjectUpdateFn g_origUpdate = nullptr;
    using ObjectPostUpdateFn = void(__fastcall*)(uint32_t datum);
    ObjectPostUpdateFn g_origPostUpdate = nullptr;
    using FireBarrelsFn = void(__fastcall*)(uint32_t weapon, uint64_t b, uint64_t c, uint64_t d, uint64_t e);
    FireBarrelsFn g_origFireBarrels = nullptr;

    uint8_t __fastcall ObjectUpdateDetour(uint32_t datum) {
        if (g_timeStopActive && Frozen(datum)) return 1;  // frozen: no animation, physics or weapon update; stays "needs update"
        return g_origUpdate(datum);
    }

    void __fastcall ObjectPostUpdateDetour(uint32_t datum) {
        if (g_timeStopActive && Frozen(datum)) return;
        g_origPostUpdate(datum);
    }

    // A frozen weapon's marked shot is dropped here (marks cleared as the engine would after firing).
    void __fastcall FireBarrelsDetour(uint32_t weapon, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
        if (g_timeStopActive && Frozen(weapon)) {
            __try {
                void** state = GameState();
                auto objects = state ? (Blam::DataArray*)state[HALO3_TLS_SLOT_OBJECTS] : nullptr;
                char* object = objects ? Blam::ObjectFromDatum(objects, weapon) : nullptr;
                if (object) {
                    for (int i = 0; i < HALO3_WEAPON_BARREL_COUNT; ++i)
                        Field<uint16_t>(object, HALO3_WEAPON_BARRELS + i * HALO3_WEAPON_BARREL_SIZE + 4) &=
                            (uint16_t)~HALO3_WEAPON_MARKED_BARREL_FLAG;
                    Field<uint16_t>(object, HALO3_WEAPON_FLAGS) &= (uint16_t)~1;
                    return;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return;  // frozen weapon whose object could not be read: still no shot
        }
        g_origFireBarrels(weapon, b, c, d, e);
    }

    // Actors this feature switched off (they were active), to wake exactly those on release.
    constexpr int kMaxActors = 1024;
    uint32_t g_frozenActors[kMaxActors];
    int g_frozenActorCount = 0;
    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);

    // Wakes the actors time stop froze (those still alive and still inactive).
    void ReleaseActors(void** state, bool callEngine) {
        int n = g_frozenActorCount;
        g_frozenActorCount = 0;
        if (!callEngine || !g_module || !state) return;
        __try {
            auto actors = (Blam::DataArray*)state[HALO3_TLS_SLOT_ACTORS];
            if (!actors || !actors->data) return;
            for (int i = 0; i < n; ++i) {
                uint32_t datum = g_frozenActors[i];
                uint32_t index = datum & 0xFFFF;
                if ((int32_t)index >= actors->highWater) continue;
                char* actor = actors->Entry((int)index);
                if (*(uint16_t*)actor == (datum >> 16) && Field<uint8_t>(actor, HALO3_ACTOR_ACTIVE) == 0)
                    ((ActorSetActiveFn)(g_module + OFFSET_HALO3_ACTOR_SET_ACTIVE))(datum, true);
            }
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 time stop release")) {
        }
    }

    // Switches off every active actor that isn't a player's (also ones created since the last tick).
    void FreezeActors(void** state, const Players& p, Blam::DataArray* objects) {
        auto actors = (Blam::DataArray*)state[HALO3_TLS_SLOT_ACTORS];
        if (!actors || !actors->data) return;
        int count = actors->highWater;
        for (int i = 0; i < count; ++i) {
            char* entry = actors->Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            if (!salt || Field<uint8_t>(entry, HALO3_ACTOR_ACTIVE) != 1) continue;
            char* unit = Blam::ObjectFromDatum(objects, Field<uint32_t>(entry, HALO3_ACTOR_UNIT));
            if (unit && p.Contains(unit)) continue;
            if (g_frozenActorCount >= kMaxActors) break;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            ((ActorSetActiveFn)(g_module + OFFSET_HALO3_ACTOR_SET_ACTIVE))(datum, false);
            if (Field<uint8_t>(entry, HALO3_ACTOR_ACTIVE) == 0) g_frozenActors[g_frozenActorCount++] = datum;
        }
    }

    // Freezes an object and everything attached to it (held weapons, equipment, seated units).
    void Mark(Blam::DataArray* objects, uint32_t datum, int depth) {
        char* object = Blam::ObjectFromDatum(objects, datum);
        if (!object) return;
        if ((datum & 0xFFFF) < kMaxFrozen) g_frozen[datum & 0xFFFF] = datum;
        if (depth >= 4) return;
        uint32_t child = Field<uint32_t>(object, HALO3_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
            char* c = Blam::ObjectFromDatum(objects, child);
            if (!c) break;
            Mark(objects, child, depth + 1);
            child = Field<uint32_t>(c, HALO3_OBJECT_NEXT_SIBLING);
        }
    }

    bool ContainsPlayer(Blam::DataArray* objects, const Players& p, char* object, int depth) {
        if (p.Contains(object)) return true;
        if (depth >= 4) return false;
        uint32_t child = Field<uint32_t>(object, HALO3_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
            char* c = Blam::ObjectFromDatum(objects, child);
            if (!c) break;
            if (ContainsPlayer(objects, p, c, depth + 1)) return true;
            child = Field<uint32_t>(c, HALO3_OBJECT_NEXT_SIBLING);
        }
        return false;
    }

    // Rebuilds the frozen set: every AI unit (has an actor, not a player) with what it
    // holds, and the vehicles it rides unless a player is in them too.
    void TimeStopTick(const Players& p, Blam::DataArray* objects) {
        ClearFrozen();
        int count = objects->highWater;
        if (count > kMaxFrozen) count = kMaxFrozen;
        for (int i = 0; i < count; ++i) {
            auto entry = (const Blam::ObjectEntry*)objects->Entry(i);
            if (!entry->salt || !entry->object) continue;
            char* object = entry->object;
            uint8_t type = Field<uint8_t>(object, HALO3_OBJECT_TYPE);
            if (type != HALO3_OBJECT_TYPE_BIPED && type != HALO3_OBJECT_TYPE_VEHICLE) continue;
            if (p.Contains(object) || Field<uint32_t>(object, HALO3_UNIT_ACTOR_INDEX) == 0xFFFFFFFF) continue;
            Mark(objects, ((uint32_t)entry->salt << 16) | (uint32_t)i, 0);
            uint32_t parent = Field<uint32_t>(object, HALO3_OBJECT_PARENT);
            for (int up = 0; up < 4 && parent != 0xFFFFFFFF; ++up) {
                char* vehicle = Blam::ObjectFromDatum(objects, parent);
                if (!vehicle || ContainsPlayer(objects, p, vehicle, 0)) break;
                Mark(objects, parent, 0);
                parent = Field<uint32_t>(vehicle, HALO3_OBJECT_PARENT);
            }
        }
    }

    // --- Invulnerable AI can die ---------------------------------------------------

    constexpr int kMaxCleared = 256;
    uint32_t g_cleared[kMaxCleared];  // units whose cannot-die flag was cleared
    int g_clearedCount = 0;

    // Clears the cannot-die flag (what object_cannot_die / ai_cannot_die set) on every
    // living allied AI biped; restores it on those still alive when switched off.
    void AiStayDeadTick(const Players& p, Blam::DataArray* objects, bool on) {
        if (on) {
            if (p.count == 0) return;
            int count = objects->highWater;
            for (int i = 0; i < count; ++i) {
                auto entry = (const Blam::ObjectEntry*)objects->Entry(i);
                if (!entry->salt || !entry->object) continue;
                char* object = entry->object;
                if (Field<uint8_t>(object, HALO3_OBJECT_TYPE) != HALO3_OBJECT_TYPE_BIPED) continue;
                if (p.Contains(object) || Field<uint32_t>(object, HALO3_UNIT_ACTOR_INDEX) == 0xFFFFFFFF) continue;
                uint32_t& flags = Field<uint32_t>(object, HALO3_OBJECT_DAMAGE_FLAGS);
                if (!(flags & HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE) || (flags & HALO3_OBJECT_FLAG_DEAD)) continue;
                if (!IsAllyTeam(p, Field<int16_t>(object, HALO3_UNIT_TEAM))) continue;
                uint32_t datum = ((uint32_t)entry->salt << 16) | (uint32_t)i;
                flags &= ~HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE;
                bool known = false;
                for (int k = 0; k < g_clearedCount; ++k) known |= g_cleared[k] == datum;
                if (!known && g_clearedCount < kMaxCleared) {
                    g_cleared[g_clearedCount++] = datum;
                    LOG("Halo 3: AI stay dead: unit %08X can die now", datum);
                }
            }
        } else if (g_clearedCount > 0) {
            for (int k = 0; k < g_clearedCount; ++k) {
                char* object = Blam::ObjectFromDatum(objects, g_cleared[k]);
                if (!object) continue;
                uint32_t& flags = Field<uint32_t>(object, HALO3_OBJECT_DAMAGE_FLAGS);
                if (!(flags & HALO3_OBJECT_FLAG_DEAD)) flags |= HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE;
            }
            g_clearedCount = 0;
        }
    }
}

namespace Halo3Extras {
    bool IsFrozenObject(uint32_t datum) {
        return g_timeStopActive && Frozen(datum);
    }

    void Load(char* module) {
        g_module = module;
        g_tlsIndex = Blam::ModuleTlsIndex((HMODULE)module);
        ClearFrozen();
        g_damageHook.Install(OFFSET_HALO3_DAMAGE_OBJECT, (void*)&DamageDetour, (void**)&g_origDamage,
                             "object damage (disable friendly fire unavailable)");
        g_updateHook.Install(OFFSET_HALO3_OBJECT_UPDATE, (void*)&ObjectUpdateDetour, (void**)&g_origUpdate,
                             "object update (time stop unavailable)");
        g_postUpdateHook.Install(OFFSET_HALO3_OBJECT_POST_UPDATE, (void*)&ObjectPostUpdateDetour,
                                 (void**)&g_origPostUpdate, "object post-update (time stop may animate)");
        g_barrelsHook.Install(OFFSET_HALO3_FIRE_MARKED_BARRELS, (void*)&FireBarrelsDetour, (void**)&g_origFireBarrels,
                              "marked barrel fire (frozen AI may still shoot)");
    }

    void Unload() {
        g_noFriendlyFire = false;
        g_timeStopActive = false;
        g_damageHook.Remove();
        g_updateHook.Remove();
        g_postUpdateHook.Remove();
        g_barrelsHook.Remove();
        ClearFrozen();
        g_frozenActorCount = 0;
        g_clearedCount = 0;
        g_state = nullptr;
        g_module = nullptr;
    }

    // Map change: the datums belong to the old map's objects (the flags are on objects that are gone).
    void Reset() {
        g_timeStopActive = false;
        ClearFrozen();
        g_frozenActorCount = 0;
        g_clearedCount = 0;
        g_blockedLogged = 0;
    }

    void Tick(void** state, const Games::Settings& s, const Halo3Cheats::Players& p, Blam::DataArray* objects) {
        g_state = state;
        g_noFriendlyFire = s.noFriendlyFire;
        if (!objects || !objects->data) return;

        __try {
            if (s.timeStop && g_updateHook.target) {
                TimeStopTick(p, objects);
                FreezeActors(state, p, objects);
                g_timeStopActive = true;
            } else if (g_timeStopActive) {
                g_timeStopActive = false;  // released: the objects just update again (nothing was flagged stuck)
                ClearFrozen();
                ReleaseActors(state, true);
            }
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 time stop")) {
            g_timeStopActive = false;
            ClearFrozen();
        }

        __try {
            AiStayDeadTick(p, objects, s.aiStayDead);
            ReviveTableTick(state, p, objects, s.aiStayDead);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 3 AI stay dead")) {
        }
    }
}
