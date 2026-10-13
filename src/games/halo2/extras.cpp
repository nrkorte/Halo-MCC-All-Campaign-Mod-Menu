// Halo 2: Disable friendly fire, Lock checkpoints and Time stop (the "extras"
// work item; offsets and how they were found in extras_offsets.h). "AI can't
// see you only affects you" needs no code here: cheats.cpp keeps just the
// player's ai_disregard flag (see NOTES / notes_h2_extras).
#include "internal.h"

#include <windows.h>

#include "MinHook.h"
#include "core/log.h"
#include "extras_offsets.h"

using namespace Halo2Cheats;

namespace {
    char* g_module = nullptr;

    // Read by the hooks (game thread) and written by Tick/Apply.
    volatile bool g_noFriendlyFire = false;
    volatile bool g_lockCheckpoints = false;
    volatile bool g_timeStopActive = false;
    // The mod's own Save button: until this tick count (ms) a snapshot is let through, once.
    volatile ULONGLONG g_ownSaveUntil = 0;
    volatile bool g_ownSaveDonePending = false;  // the own snapshot went through: its "done" message may show
    constexpr ULONGLONG kOwnSaveWindowMs = 30000;  // a paused game holds the save until it ticks again

    struct Hook {
        void* target = nullptr;
        bool Install(int rva, void* detour, void** original, const char* what) {
            void* at = g_module + rva;
            if (MH_CreateHook(at, detour, original) == MH_OK && MH_EnableHook(at) == MH_OK) {
                target = at;
                return true;
            }
            MH_RemoveHook(at);
            LOG("Halo 2: failed to hook %s at %p", what, at);
            return false;
        }
        void Remove() {
            if (!target) return;
            MH_DisableHook(target);
            MH_RemoveHook(target);
            target = nullptr;
        }
    };
    Hook g_damageHook, g_snapshotHook, g_updateHook, g_messageHook;

    // --- Disable friendly fire ---------------------------------------------------

    using DamageFn = void(__fastcall*)(void* damage, uint64_t object, uint64_t region, uint64_t node, uint64_t a5, uint64_t a6);
    DamageFn g_origDamage = nullptr;
    using TeamsAreEnemiesFn = bool(__fastcall*)(uint16_t a, uint16_t b);

    // Friends in the engine's own friends matrix (the damage code's friendly-fire flag uses it).
    bool Friends(int16_t a, int16_t b) {
        if (a == -1 || b == -1) return false;
        if (a == b) return true;
        return !((TeamsAreEnemiesFn)(g_module + OFFSET_HALO2_TEAMS_ARE_ENEMIES))((uint16_t)a, (uint16_t)b);
    }

    // What an object is as a damage victim: the players' own units (or a vehicle one sits in)
    // and the allied AI's. Walks the object and what's attached (seated units).
    void Classify(const Objects& objects, const Players& p, char* object, int type, int16_t myTeam, int depth,
                  bool& player, bool& ally) {
        if (type == HALO2_OBJECT_TYPE_BIPED || type == HALO2_OBJECT_TYPE_VEHICLE) {
            if (p.Contains(object)) player = true;
            else if (Field<uint32_t>(object, HALO2_UNIT_ACTOR) != 0xFFFFFFFF &&
                     Friends(Field<int16_t>(object, HALO2_UNIT_TEAM), myTeam))
                ally = true;
        }
        if (depth >= 3) return;
        uint32_t child = Field<uint32_t>(object, HALO2_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 16; ++n) {
            int childType = -1;
            char* c = objects.Get(child, &childType);
            if (!c) break;
            if (childType == HALO2_OBJECT_TYPE_BIPED || childType == HALO2_OBJECT_TYPE_VEHICLE)
                Classify(objects, p, c, childType, myTeam, depth + 1, player, ally);
            child = Field<uint32_t>(c, HALO2_OBJECT_NEXT_SIBLING);
        }
    }

    volatile int g_blockedLogged = 0;

    // True if this hit is between player 1's side and its allied AI, either way
    // (not ally against ally).
    bool DropHit(void* damage, uint32_t victim, int* why) {
        __try {
            Array players;
            Objects objects;
            if (!ReadTables(g_module, players, objects)) return false;
            Players p = FindPlayers(players, objects);
            if (p.count == 0) return false;
            int16_t myTeam = Field<int16_t>(p.units[0], HALO2_UNIT_TEAM);

            char* data = (char*)damage;
            uint32_t ownerPlayer = Field<uint32_t>(data, HALO2_DAMAGE_OWNER_PLAYER);
            uint32_t ownerUnit = Field<uint32_t>(data, HALO2_DAMAGE_OWNER_UNIT);
            int16_t ownerTeam = Field<int16_t>(data, HALO2_DAMAGE_OWNER_TEAM);
            bool ownerIsPlayer = ownerPlayer != 0xFFFFFFFF;
            for (int i = 0; i < p.count; ++i) ownerIsPlayer |= ownerUnit == p.datums[i];
            // An allied AI's shot still counts after the shooter died (its unit datum is gone, the team was recorded).
            bool ownerIsAlly = !ownerIsPlayer && ownerUnit != 0xFFFFFFFF && Friends(ownerTeam, myTeam);
            if (!ownerIsPlayer && !ownerIsAlly) return false;

            int type = -1;
            char* object = objects.Get(victim, &type);
            if (!object) return false;
            bool vPlayer = false, vAlly = false;
            Classify(objects, p, object, type, myTeam, 0, vPlayer, vAlly);
            if (ownerIsPlayer && vAlly && !vPlayer) { *why = 1; return true; }
            if (ownerIsAlly && vPlayer) { *why = 2; return true; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    void __fastcall DamageDetour(void* damage, uint64_t object, uint64_t region, uint64_t node, uint64_t a5, uint64_t a6) {
        int why = 0;
        if (g_noFriendlyFire && damage && DropHit(damage, (uint32_t)object, &why)) {
            if (g_blockedLogged < 12) {
                g_blockedLogged = g_blockedLogged + 1;
                LOG("Halo 2: friendly fire blocked (%s) on object %08X", why == 1 ? "player -> ally" : "ally -> player",
                    (uint32_t)object);
            }
            return;
        }
        g_origDamage(damage, object, region, node, a5, a6);
    }

    // --- Lock checkpoints ----------------------------------------------------------

    using SnapshotFn = void(__fastcall*)(uint64_t copyToSecond, uint64_t writeFile);
    SnapshotFn g_origSnapshot = nullptr;
    volatile int g_snapshotLogged = 0;

    void __fastcall SnapshotDetour(uint64_t a, uint64_t b) {
        if (g_lockCheckpoints) {
            ULONGLONG until = g_ownSaveUntil;
            if (until != 0 && GetTickCount64() < until) {
                g_ownSaveUntil = 0;  // the Save button's own checkpoint: once
                g_ownSaveDonePending = true;
            } else {
                if (g_snapshotLogged < 5) {
                    g_snapshotLogged = g_snapshotLogged + 1;
                    LOG("Halo 2: checkpoint save skipped (locked)");
                }
                return;
            }
        }
        g_origSnapshot(a, b);
    }

    // The "Checkpoint..." / "Checkpoint...done" HUD message: 0x8220D0(bool started) posts string
    // 0x120006AC (started) or 0x100006AD (done) to every local player's HUD. Called with 1 when the save
    // is requested (0x67A999) and with 0 by the main loop after the snapshot step (0x679EBB). While
    // locked both are dropped, except the mod's own save (window open for "started", snapshot let
    // through for "done").
    using CheckpointMessageFn = void(__fastcall*)(uint8_t started);
    CheckpointMessageFn g_origMessage = nullptr;

    void __fastcall CheckpointMessageDetour(uint8_t started) {
        if (g_lockCheckpoints) {
            bool own;
            if (started) {
                ULONGLONG until = g_ownSaveUntil;
                own = until != 0 && GetTickCount64() < until;
            } else {
                own = g_ownSaveDonePending;
                g_ownSaveDonePending = false;
            }
            if (!own) return;
        } else if (!started) {
            g_ownSaveDonePending = false;
        }
        g_origMessage(started);
    }

    // --- Time stop -----------------------------------------------------------------

    constexpr int kMaxFrozen = 4096;  // object table indices (Halo 2 allows 2048 objects)
    uint32_t g_frozen[kMaxFrozen];    // datum frozen at each table index, or -1; game thread only
    using ObjectUpdateFn = void(__fastcall*)(uint32_t datum);
    ObjectUpdateFn g_origUpdate = nullptr;

    void ClearFrozen() {
        for (auto& d : g_frozen) d = 0xFFFFFFFF;
    }

    // Actors this feature switched off (they were active), to wake exactly those on release.
    constexpr int kMaxActors = 1024;
    uint32_t g_frozenActors[kMaxActors];
    int g_frozenActorCount = 0;
    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);

    // Wakes the actors time stop froze (those still alive and still inactive).
    void ReleaseActors(bool callEngine) {
        int n = g_frozenActorCount;
        g_frozenActorCount = 0;
        if (!callEngine || !g_module) return;
        __try {
            Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
            if (!actors.header) return;
            for (int i = 0; i < n; ++i) {
                char* actor = actors.Get(g_frozenActors[i]);
                if (actor && Field<uint8_t>(actor, HALO2_ACTOR_ACTIVE) == 0)
                    ((ActorSetActiveFn)(g_module + OFFSET_HALO2_ACTOR_SET_ACTIVE))(g_frozenActors[i], true);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // Switches off every active actor that isn't a player's (also ones created since the last tick).
    void FreezeActors(const Players& p, const Objects& objects) {
        Array actors{*(char**)(g_module + OFFSET_HALO2_ACTORS)};
        if (!actors.header) return;
        int count = actors.HighWater();
        for (int i = 0; i < count; ++i) {
            char* entry = actors.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            if (!salt || Field<uint8_t>(entry, HALO2_ACTOR_ACTIVE) != 1) continue;
            char* unit = objects.Get(Field<uint32_t>(entry, HALO2_ACTOR_UNIT));
            if (unit && p.Contains(unit)) continue;
            if (g_frozenActorCount >= kMaxActors) break;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            ((ActorSetActiveFn)(g_module + OFFSET_HALO2_ACTOR_SET_ACTIVE))(datum, false);
            if (Field<uint8_t>(entry, HALO2_ACTOR_ACTIVE) == 0) g_frozenActors[g_frozenActorCount++] = datum;
        }
    }

    void __fastcall ObjectUpdateDetour(uint32_t datum) {
        if (g_timeStopActive) {
            uint32_t index = datum & 0xFFFF;
            if (index < kMaxFrozen && g_frozen[index] == datum) return;  // frozen: no animation, physics or weapon update
        }
        g_origUpdate(datum);
    }

    // Freezes an object and everything attached to it (held weapons, equipment, seated units).
    void Mark(const Objects& objects, uint32_t datum, int depth) {
        char* object = objects.Get(datum);
        if (!object) return;
        if ((datum & 0xFFFF) < kMaxFrozen) g_frozen[datum & 0xFFFF] = datum;
        if (depth >= 4) return;
        uint32_t child = Field<uint32_t>(object, HALO2_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
            char* c = objects.Get(child);
            if (!c) break;
            Mark(objects, child, depth + 1);
            child = Field<uint32_t>(c, HALO2_OBJECT_NEXT_SIBLING);
        }
    }

    bool ContainsPlayer(const Objects& objects, const Players& p, char* object, int depth) {
        if (p.Contains(object)) return true;
        if (depth >= 4) return false;
        uint32_t child = Field<uint32_t>(object, HALO2_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
            char* c = objects.Get(child);
            if (!c) break;
            if (ContainsPlayer(objects, p, c, depth + 1)) return true;
            child = Field<uint32_t>(c, HALO2_OBJECT_NEXT_SIBLING);
        }
        return false;
    }

    // Rebuilds the frozen set: every AI unit (actor set, not a player) with what it
    // holds, and the vehicles it rides unless a player is in them too.
    void TimeStopTick(const Players& p, const Objects& objects) {
        ClearFrozen();
        int count = objects.table.HighWater();
        if (count > kMaxFrozen) count = kMaxFrozen;
        for (int i = 0; i < count; ++i) {
            char* entry = objects.table.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            if (!salt) continue;
            int type = -1;
            char* object = objects.FromEntry(entry, &type);
            if (!object || (type != HALO2_OBJECT_TYPE_BIPED && type != HALO2_OBJECT_TYPE_VEHICLE)) continue;
            if (p.Contains(object) || Field<uint32_t>(object, HALO2_UNIT_ACTOR) == 0xFFFFFFFF) continue;
            Mark(objects, ((uint32_t)salt << 16) | (uint32_t)i, 0);
            uint32_t parent = Field<uint32_t>(object, HALO2_OBJECT_PARENT);
            for (int up = 0; up < 4 && parent != 0xFFFFFFFF; ++up) {
                char* vehicle = objects.Get(parent);
                if (!vehicle || ContainsPlayer(objects, p, vehicle, 0)) break;
                Mark(objects, parent, 0);
                parent = Field<uint32_t>(vehicle, HALO2_OBJECT_PARENT);
            }
        }
    }
}

namespace Halo2Extras {
    // Projectile-creation hook (halo2.cpp): true if time stop is on and this weapon belongs to a
    // frozen AI unit or vehicle (the table is rebuilt every tick on the game thread, the hook runs there too).
    bool IsFrozenObject(uint32_t datum) {
        if (!g_timeStopActive) return false;
        uint32_t index = datum & 0xFFFF;
        return index < kMaxFrozen && g_frozen[index] == datum;
    }

    void Load(char* module) {
        g_module = module;
        ClearFrozen();
        g_damageHook.Install(OFFSET_HALO2_DAMAGE_OBJECT, (void*)&DamageDetour, (void**)&g_origDamage,
                             "object damage (disable friendly fire unavailable)");
        g_snapshotHook.Install(OFFSET_HALO2_SNAPSHOT_COPY, (void*)&SnapshotDetour, (void**)&g_origSnapshot,
                               "checkpoint snapshot (lock checkpoints unavailable)");
        g_updateHook.Install(OFFSET_HALO2_OBJECT_UPDATE, (void*)&ObjectUpdateDetour, (void**)&g_origUpdate,
                             "object update (time stop unavailable)");
        g_messageHook.Install(OFFSET_HALO2_CHECKPOINT_MESSAGE, (void*)&CheckpointMessageDetour, (void**)&g_origMessage,
                              "checkpoint message (it will still show while locked)");
    }

    void Unload() {
        g_noFriendlyFire = false;
        g_lockCheckpoints = false;
        g_timeStopActive = false;
        g_damageHook.Remove();
        g_snapshotHook.Remove();
        g_updateHook.Remove();
        g_messageHook.Remove();
        ClearFrozen();
        g_ownSaveUntil = 0;
        g_ownSaveDonePending = false;
        g_module = nullptr;
    }

    // Map change: the frozen datums belong to the old map's objects.
    void Reset() {
        g_timeStopActive = false;
        ClearFrozen();
        g_frozenActorCount = 0;
    }

    void Tick(const Games::Settings& s, const Games::FrameInput&, const Halo2Cheats::Players& p,
              const Halo2Cheats::Objects& objects) {
        g_noFriendlyFire = s.noFriendlyFire;
        g_lockCheckpoints = s.lockCheckpoints;
        if (s.timeStop && g_updateHook.target) {
            TimeStopTick(p, objects);
            FreezeActors(p, objects);
            g_timeStopActive = true;
        } else if (g_timeStopActive) {
            g_timeStopActive = false;  // released: the objects just update again (nothing was flagged stuck)
            ClearFrozen();
            ReleaseActors(true);
        }
    }

    void Apply(const Games::Settings& s, const Halo2Cheats::Players&, const Halo2Cheats::Objects&, Games::Status&) {
        g_noFriendlyFire = s.noFriendlyFire;
        g_lockCheckpoints = s.lockCheckpoints;
    }

    // The Save button's request is only processed a tick or two later (the state machine at 0x7008A0,
    // then the main loop), so the window stays open until the snapshot comes through (or 30 s).
    void BeginOwnSave() { g_ownSaveUntil = GetTickCount64() + kOwnSaveWindowMs; }
    void EndOwnSave() {}
}
