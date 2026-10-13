// Halo 4: Disable friendly fire, Time stop and "Invulnerable AI can die" (offsets and
// how they were found in extras_offsets.h). Ported from Reach (reach/cheats.cpp
// DamageAllowed, reach/main_thread.cpp TimeStop and AiStayDead), with Halo 3's
// projectile-pass veto (halo3/extras.cpp). Runs on the game thread inside SEH; the
// hooks are installed in Load.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MinHook.h"
#include "core/log.h"
#include "extras_offsets.h"
#include "games/blam.h"
#include "internal.h"
#include "tags.h"

using namespace Halo4Cheats;

namespace {
    char* g_module = nullptr;
    int32_t g_tlsIndex = -1;
    void** g_state = nullptr;  // the game thread's TLS block (captured in Tick)

    // Read by the hooks (game thread), written by Tick.
    volatile bool g_noFriendlyFire = false;
    volatile bool g_timeStopOn = false;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);

    struct Hook {
        void* target = nullptr;
        bool Install(int rva, void* detour, void** original, const char* what) {
            void* at = g_module + rva;
            if (MH_CreateHook(at, detour, original) == MH_OK && MH_EnableHook(at) == MH_OK) {
                target = at;
                return true;
            }
            MH_RemoveHook(at);
            LOG("Halo 4: failed to hook %s at %p", what, at);
            return false;
        }
        void Remove() {
            if (!target) return;
            MH_DisableHook(target);
            MH_RemoveHook(target);
            target = nullptr;
        }
    };
    Hook g_damageHook, g_updateHook, g_wrapperHook, g_animHook, g_barrelsHook;

    // The calling thread's game state if it is the game thread, else nullptr.
    void** GameState() {
        if (g_tlsIndex < 0 || !g_state) return nullptr;
        return Blam::ThreadTlsBlock(g_tlsIndex) == g_state ? g_state : nullptr;
    }

    // Player 1's team, -1 with no player.
    int PlayerTeam(const Players& p) {
        return p.count > 0 ? Field<int8_t>(p.units[0], HALO4_UNIT_TEAM) : -1;
    }

    // An AI unit (biped or vehicle with an actor, not a player) whose team is friends with `mine`.
    bool IsAlly(void** state, const Players& p, char* object, int type, int mine) {
        if (mine < 0 || p.Contains(object)) return false;
        if (type != HALO4_OBJECT_TYPE_BIPED && type != HALO4_OBJECT_TYPE_VEHICLE) return false;
        if (Field<uint32_t>(object, HALO4_UNIT_ACTOR) == 0xFFFFFFFF) return false;
        return Halo4Internal::TeamsFriends(state, Field<int8_t>(object, HALO4_UNIT_TEAM), mine);
    }

    // --- Disable friendly fire ---------------------------------------------------
    // Every hit ends in the cause-damage function (extras_offsets.h). A hit from the player
    // on one of their allied AI, or from one of those AI on the player, is dropped (not
    // ally against ally). "Allied" = the AI's team is friends with player 1's in the
    // allegiance table, so it follows "Your team" changes.

    using CauseDamageFn = void(__fastcall*)(void* data, float scale, uint32_t object, uint16_t region, uint64_t a5,
                                            uint64_t a6, uint64_t a7);
    CauseDamageFn g_origDamage = nullptr;

    // What an object is as a damage victim: a player's unit (or a vehicle one sits in) and an
    // allied AI. Walks what's attached (seated units), three levels deep.
    void Classify(void** state, const Array& objects, const Players& p, char* object, int type, uint32_t datum, int mine,
                  int depth, bool& player, bool& ally) {
        if (p.Contains(object)) player = true;
        else if (IsAlly(state, p, object, type, mine)) ally = true;
        if (depth >= 3) return;
        uint32_t child = Field<uint32_t>(object, HALO4_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 16; ++n) {
            int childType = -1;
            char* c = ObjectFromDatum(objects, child, &childType);
            if (!c) break;
            if (childType == HALO4_OBJECT_TYPE_BIPED || childType == HALO4_OBJECT_TYPE_VEHICLE)
                Classify(state, objects, p, c, childType, child, mine, depth + 1, player, ally);
            child = Field<uint32_t>(c, HALO4_OBJECT_NEXT_SIBLING);
        }
    }

    volatile int g_blockedLogged = 0;
    uint64_t g_seenHits[32];  // signatures of hits on allies already logged (effect, flags, outcome)
    volatile int g_seenHitCount = 0;

    // True if this hit is between player(s)' side and allied AI, either way. `why`: 1 player ->
    // ally, 2 ally -> player.
    bool DropHit(void* damage, uint32_t victimDatum, int* why) {
        __try {
            void** state = GameState();
            if (!state) return false;
            Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
            Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
            if (!players.Valid() || !objects.Valid()) return false;
            Players p = FindPlayers(players, objects);
            if (p.count == 0) return false;
            int mine = PlayerTeam(p);

            char* d = (char*)damage;
            uint32_t ownerPlayer = Field<uint32_t>(d, HALO4_DAMAGE_OWNER_PLAYER);
            uint32_t ownerDatum = Field<uint32_t>(d, HALO4_DAMAGE_OWNER_UNIT);
            int ownerType = -1;
            char* owner = ObjectFromDatum(objects, ownerDatum, &ownerType);
            if (!owner && ownerPlayer != 0xFFFFFFFF) {  // only the player is recorded: use their unit
                if (char* entry = players.Get(ownerPlayer)) {
                    ownerDatum = Field<uint32_t>(entry, HALO4_PLAYER_UNIT_DATUM);
                    owner = ObjectFromDatum(objects, ownerDatum, &ownerType);
                }
            }

            int victimType = -1;
            char* victim = ObjectFromDatum(objects, victimDatum, &victimType);
            if (!victim) return false;
            bool vPlayer = false, vAlly = false;
            Classify(state, objects, p, victim, victimType, victimDatum, mine, 0, vPlayer, vAlly);

            bool blocked = false;
            if (owner) {
                bool ownerIsPlayer = p.Contains(owner);
                bool ownerIsAlly = !ownerIsPlayer && IsAlly(state, p, owner, ownerType, mine);
                if (ownerIsPlayer && vAlly && !vPlayer) { *why = 1; blocked = true; }
                else if (ownerIsAlly && vPlayer) { *why = 2; blocked = true; }
            }

            // Diagnostics: each distinct kind of hit on an ally (effect, flags, outcome) is logged once,
            // so a missing or wrong owner layout shows up.
            if (vAlly && !vPlayer && g_seenHitCount < 32) {
                uint32_t effect = Field<uint32_t>(d, 0), flags = Field<uint32_t>(d, 4);
                uint64_t sig = ((uint64_t)effect << 32) ^ ((uint64_t)flags << 1) ^ (blocked ? 1u : 0u) ^
                               (owner ? 2u : 0u) ^ (owner && p.Contains(owner) ? 4u : 0u);
                bool known = false;
                for (int i = 0; i < g_seenHitCount; ++i) known |= g_seenHits[i] == sig;
                if (!known) {
                    g_seenHits[g_seenHitCount] = sig;
                    g_seenHitCount = g_seenHitCount + 1;
                    LOG("Halo 4: hit on ally %08X: effect %08X flags %08X owner player %08X unit %08X%s -> %s", victimDatum,
                        effect, flags, ownerPlayer, ownerDatum, owner ? "" : " (not an object)",
                        blocked ? "blocked" : "let through");
                }
            }
            return blocked;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return false;
    }

    void __fastcall CauseDamageDetour(void* data, float scale, uint32_t object, uint16_t region, uint64_t a5, uint64_t a6,
                                      uint64_t a7) {
        int why = 0;
        if (g_noFriendlyFire && data && DropHit(data, object, &why)) {
            if (g_blockedLogged < 12) {
                g_blockedLogged = g_blockedLogged + 1;
                LOG("Halo 4: friendly fire blocked (%s) on object %08X, owner player %08X unit %08X",
                    why == 1 ? "player -> ally" : "ally -> player", object,
                    *(uint32_t*)((char*)data + HALO4_DAMAGE_OWNER_PLAYER),
                    *(uint32_t*)((char*)data + HALO4_DAMAGE_OWNER_UNIT));
            }
            return;
        }
        g_origDamage(data, scale, object, region, a5, a6, a7);
    }

    // --- Time stop -----------------------------------------------------------------
    // Every actor but the players' is switched off (actor_set_active false) and its unit, what
    // it holds and the vehicle it rides are skipped by the per-object update hooks, so nothing
    // about them updates: no animation, physics, melee or weapon. Frozen weapons' marked shots
    // are dropped in the projectile pass. Release: the skipped objects' stuck update flag is
    // cleared and only the actors this switched off are woken. Projectiles in flight keep moving.

    constexpr int kMaxFrozen = 16384;  // object table indices
    uint32_t g_frozen[kMaxFrozen];     // datum frozen at each table index, or -1; game thread only
    std::vector<uint32_t> g_frozenList;
    struct FrozenActor { uint32_t actor, unit; };
    std::vector<FrozenActor> g_frozenActors;
    bool g_frozenLogged = false;

    // Where each frozen free-standing unit/vehicle was when it froze: whatever still moves it (physics,
    // anything outside the hooked updates) is undone every tick so nothing teleports on release.
    float g_pinPos[kMaxFrozen][3];
    uint32_t g_pinDatum[kMaxFrozen];
    int g_pinLogged = 0;
    int g_wokeLogged = 0;

    void ClearFrozen() {
        for (auto& d : g_frozen) d = 0xFFFFFFFF;
        for (auto& d : g_pinDatum) d = 0xFFFFFFFF;
        g_frozenList.clear();
    }
    bool Frozen(uint32_t datum) {
        uint32_t index = datum & 0xFFFF;
        return index < kMaxFrozen && g_frozen[index] == datum;
    }

    using ObjectUpdateFn = void(__fastcall*)(uint32_t datum);
    ObjectUpdateFn g_origUpdate = nullptr;
    ObjectUpdateFn g_origWrapper = nullptr;
    using ObjectAnimFn = uint8_t(__fastcall*)(uint32_t datum);
    ObjectAnimFn g_origAnim = nullptr;
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward, const float* up,
                                            int64_t, bool);
    using FireBarrelsFn = void(__fastcall*)(uint32_t weapon, uint64_t b, uint64_t c, uint64_t d);
    FireBarrelsFn g_origFireBarrels = nullptr;

    // A frozen object isn't updated at all, so it keeps its pose.
    void __fastcall ObjectUpdateDetour(uint32_t datum) {
        if (g_timeStopOn && Frozen(datum)) return;
        g_origUpdate(datum);
    }
    void __fastcall WrapperDetour(uint32_t datum) {
        if (g_timeStopOn && Frozen(datum)) return;
        g_origWrapper(datum);
    }

    // The animation / pre-update pass (root motion, footstep events): returns "changed", 0 when skipped.
    uint8_t __fastcall AnimDetour(uint32_t datum) {
        if (g_timeStopOn && Frozen(datum)) return 0;
        return g_origAnim(datum);
    }

    // A frozen weapon's marked shot is dropped here (marks cleared as the engine would after firing).
    void __fastcall FireBarrelsDetour(uint32_t weapon, uint64_t b, uint64_t c, uint64_t d) {
        if (g_timeStopOn && Frozen(weapon)) {
            __try {
                void** state = GameState();
                char* object = nullptr;
                if (state) {
                    Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
                    if (objects.Valid()) object = ObjectFromDatum(objects, weapon);
                }
                if (object) {
                    for (int i = 0; i < HALO4_WEAPON_BARREL_COUNT; ++i)
                        Field<uint16_t>(object, HALO4_WEAPON_BARRELS + i * HALO4_WEAPON_BARREL_SIZE +
                                                    HALO4_WEAPON_BARREL_FLAGS) &= (uint16_t)~HALO4_WEAPON_MARKED_BARREL_FLAG;
                    Field<uint16_t>(object, HALO4_WEAPON_FIRE_FLAGS) &= (uint16_t)~1;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return;  // a frozen weapon never shoots, even if its object could not be read
        }
        g_origFireBarrels(weapon, b, c, d);
    }

    // Freezes an object by datum (once).
    void Mark(uint32_t datum) {
        uint32_t index = datum & 0xFFFF;
        if (datum == 0xFFFFFFFF || index >= kMaxFrozen || g_frozen[index] == datum) return;
        g_frozen[index] = datum;
        g_frozenList.push_back(datum);
    }

    // A unit's weapons and equipment (a melee blow is dealt by the weapon's own update).
    void MarkHeld(char* unit) {
        for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w) Mark(((uint32_t*)(unit + HALO4_UNIT_WEAPONS))[w]);
        Mark(Field<uint32_t>(unit, HALO4_UNIT_EQUIPMENT));
    }

    // True if a player is the object or anything seated in it.
    bool ContainsPlayer(const Array& objects, const Players& p, char* object, int depth) {
        if (p.Contains(object)) return true;
        if (depth >= 4) return false;
        uint32_t child = Field<uint32_t>(object, HALO4_OBJECT_FIRST_CHILD);
        for (int n = 0; child != 0xFFFFFFFF && n < 32; ++n) {
            char* c = ObjectFromDatum(objects, child);
            if (!c) break;
            if (ContainsPlayer(objects, p, c, depth + 1)) return true;
            child = Field<uint32_t>(c, HALO4_OBJECT_NEXT_SIBLING);
        }
        return false;
    }

    // Holds a frozen object that isn't attached to anything at the spot it froze at.
    void Pin(uint32_t datum, char* object) {
        uint32_t index = datum & 0xFFFF;
        if (index >= kMaxFrozen || Field<uint32_t>(object, HALO4_OBJECT_PARENT) != 0xFFFFFFFF) return;
        float* at = &Field<float>(object, HALO4_OBJECT_POSITION);
        if (g_pinDatum[index] != datum) {
            g_pinDatum[index] = datum;
            memcpy(g_pinPos[index], at, sizeof(float) * 3);
            return;
        }
        float dx = at[0] - g_pinPos[index][0], dy = at[1] - g_pinPos[index][1], dz = at[2] - g_pinPos[index][2];
        float drift2 = dx * dx + dy * dy + dz * dz;
        if (drift2 < 0.0005f * 0.0005f) return;
        if (g_pinLogged < 8) {
            ++g_pinLogged;
            LOG("Halo 4: time stop: frozen object %08X moved %.4f, put back", datum, sqrtf(drift2));
        }
        Engine<SetPositionFn>(OFFSET_HALO4_OBJECT_SET_POSITION)(datum, g_pinPos[index], nullptr, nullptr, 0, false);
        memset(&Field<float>(object, HALO4_OBJECT_VELOCITY), 0, 3 * sizeof(float));
    }

    // Wakes the actors time stop switched off (those still alive, same unit, still inactive).
    void WakeActors(const Array& actors) {
        int woken = 0;
        for (auto& f : g_frozenActors) {
            char* actor = actors.Get(f.actor);
            if (!actor || Field<uint32_t>(actor, HALO4_ACTOR_UNIT) != f.unit) continue;
            if (Field<uint8_t>(actor, HALO4_ACTOR_ACTIVE_FLAGS) & HALO4_ACTOR_ACTIVE_BIT) continue;
            Engine<ActorSetActiveFn>(OFFSET_HALO4_ACTOR_SET_ACTIVE)(f.actor, true);
            ++woken;
        }
        LOG("Halo 4: time resumed (%d of %d actors woken, %d objects released)", woken, (int)g_frozenActors.size(),
            (int)g_frozenList.size());
        g_frozenActors.clear();
    }

    void ReleaseTime(void** state, const Array& objects) {
        g_timeStopOn = false;
        for (uint32_t datum : g_frozenList) {  // the skipped objects' stuck update flag
            char* entry = objects.Entry((int)(datum & 0xFFFF));
            if (entry && *(uint16_t*)entry == (uint16_t)(datum >> 16))
                Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_SKIPPED_FLAGS) &= (uint8_t)~HALO4_OBJECT_ENTRY_SKIPPED_BIT;
        }
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (actors.Valid()) WakeActors(actors);
        else g_frozenActors.clear();
        ClearFrozen();
        g_frozenLogged = false;
    }

    // Rebuilds the frozen set every tick (AI that appear later are frozen on the next tick).
    void FreezeTick(void** state, const Players& p, const Array& objects) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return;
        for (uint32_t datum : g_frozenList) g_frozen[datum & 0xFFFF] = 0xFFFFFFFF;
        g_frozenList.clear();
        g_timeStopOn = true;

        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            uint16_t salt = *(uint16_t*)actor;
            if (!salt) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            uint32_t unitDatum = Field<uint32_t>(actor, HALO4_ACTOR_UNIT);
            char* unit = ObjectFromDatum(objects, unitDatum);
            if (!unit || p.Contains(unit)) continue;
            if (Field<uint8_t>(actor, HALO4_ACTOR_ACTIVE_FLAGS) & HALO4_ACTOR_ACTIVE_BIT) {
                for (auto& f : g_frozenActors)
                    if (f.actor == datum && g_wokeLogged < 6) {
                        ++g_wokeLogged;
                        LOG("Halo 4: time stop: actor %08X was switched back on by the game, off again", datum);
                    }
                Engine<ActorSetActiveFn>(OFFSET_HALO4_ACTOR_SET_ACTIVE)(datum, false);
                if (!(Field<uint8_t>(actor, HALO4_ACTOR_ACTIVE_FLAGS) & HALO4_ACTOR_ACTIVE_BIT)) {
                    bool known = false;
                    for (auto& f : g_frozenActors)
                        if (f.actor == datum) { f.unit = unitDatum; known = true; }
                    if (!known) g_frozenActors.push_back({datum, unitDatum});
                }
            }
            // Frozen whether or not the actor was awake: a dormant or scripted AI is still
            // animated by its unit's update.
            Mark(unitDatum);
            Pin(unitDatum, unit);
            MarkHeld(unit);
            memset(&Field<float>(unit, HALO4_OBJECT_VELOCITY), 0, 3 * sizeof(float));
            // The vehicle it rides, unless a player is in it too.
            uint32_t parent = Field<uint32_t>(unit, HALO4_OBJECT_PARENT);
            for (int up = 0; up < 4 && parent != 0xFFFFFFFF; ++up) {
                char* vehicle = ObjectFromDatum(objects, parent);
                if (!vehicle || ContainsPlayer(objects, p, vehicle, 0)) break;
                Mark(parent);
                Pin(parent, vehicle);
                MarkHeld(vehicle);  // its own guns
                memset(&Field<float>(vehicle, HALO4_OBJECT_VELOCITY), 0, 3 * sizeof(float));
                parent = Field<uint32_t>(vehicle, HALO4_OBJECT_PARENT);
            }
        }
        if (!g_frozenLogged) {
            g_frozenLogged = true;
            LOG("Halo 4: time stopped (%d objects, %d actors switched off)", (int)g_frozenList.size(),
                (int)g_frozenActors.size());
        }
    }

    // --- Invulnerable AI can die ---------------------------------------------------
    // The mission's AI lose the scripts' cannot-die / cannot-take-damage flags (allied AI
    // bipeds only) and come off the AI revive list, so they die and stay dead. Undone when
    // turned off, for those still alive. AI the mod spawned are left to "Enable respawn". A
    // mission may place a new protected copy of a character that died (Reach's Kat): the
    // character of each AI that dies is remembered and a new protected unit of it is deleted.

    constexpr uint32_t kProtectFlags = HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE | HALO4_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE;

    struct RemovedRevive {
        uint32_t unit;
        char entry[HALO4_AI_REVIVE_SIZE];
    };
    RemovedRevive g_removedRevives[HALO4_AI_REVIVE_COUNT];
    int g_removedReviveCount = 0;
    struct Unprotected {
        uint32_t unit;
        uint32_t character;  // 'char' tag, -1 if its actor wasn't found
        uint32_t cleared;    // the flags this cleared on it
        bool dead;
    };
    constexpr int kMaxUnprotected = 64;
    Unprotected g_unprotected[kMaxUnprotected];
    int g_unprotectedCount = 0;
    constexpr int kMaxDeadCharacters = 32;
    uint32_t g_deadCharacters[kMaxDeadCharacters];
    int g_deadCharacterCount = 0;
    bool g_stayDeadApplied = false;
    int g_revivesSeen = 0;  // for the log: units found on the revive list

    void ClearRevive(char* entry) {
        static const uint8_t kEmpty[HALO4_AI_REVIVE_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                             0,    0,    0,    0,    0xFF, 0xFF, 0xFF, 0xFF,
                                                             0,    0,    0,    0,    0,    0,    0xFF, 0xFF};
        memcpy(entry, kEmpty, sizeof(kEmpty));
    }

    uint32_t CharacterOfUnit(void** state, uint32_t unit) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        if (!actors.Valid()) return 0xFFFFFFFF;
        for (int i = 0; i < actors.HighWater(); ++i) {
            char* actor = actors.Entry(i);
            if (*(uint16_t*)actor && Field<uint32_t>(actor, HALO4_ACTOR_UNIT) == unit)
                return Field<uint32_t>(actor, HALO4_ACTOR_CHARACTER);
        }
        return 0xFFFFFFFF;
    }

    Unprotected* FindUnprotected(uint32_t unit) {
        for (int i = 0; i < g_unprotectedCount; ++i)
            if (g_unprotected[i].unit == unit) return &g_unprotected[i];
        return nullptr;
    }

    std::string CharacterName(uint32_t character) {
        std::string name = character != 0xFFFFFFFF ? Halo4Tags::FullName(character) : "";
        return name.empty() ? "unknown character" : name;
    }

    // A protected AI unit: tracked, or deleted if its character already died while the toggle was
    // on. Returns the tracking entry, or nullptr if the unit was deleted.
    Unprotected* Unprotect(void** state, uint32_t unit) {
        if (Unprotected* known = FindUnprotected(unit)) return known;
        uint32_t character = CharacterOfUnit(state, unit);
        for (int i = 0; i < g_deadCharacterCount && character != 0xFFFFFFFF; ++i) {
            if (g_deadCharacters[i] != character) continue;
            LOG("Halo 4: AI stay dead: deleted %s (unit %08X), placed again after dying", CharacterName(character).c_str(),
                unit);
            Halo4Internal::DeleteObject(unit);
            return nullptr;
        }
        if (g_unprotectedCount >= kMaxUnprotected) return nullptr;
        g_unprotected[g_unprotectedCount] = {unit, character, 0, false};
        LOG("Halo 4: AI stay dead: %s (unit %08X) is protected, can die now", CharacterName(character).c_str(), unit);
        return &g_unprotected[g_unprotectedCount++];
    }

    char* LivingUnit(const Array& objects, uint32_t datum) {
        char* unit = ObjectFromDatum(objects, datum);
        return unit && !(Field<uint32_t>(unit, HALO4_OBJECT_DAMAGE_FLAGS) & HALO4_DAMAGE_FLAG_DEAD) ? unit : nullptr;
    }

    bool IsFollowed(const uint32_t* followed, int count, uint32_t unit) {
        for (int i = 0; i < count; ++i)
            if (followed[i] == unit) return true;
        return false;
    }

    void AiStayDead(void** state, const Players& p, const Array& objects, bool on) {
        char* ai = (char*)state[HALO4_TLS_SLOT_AI_GLOBALS];
        if (!ai) return;

        if (!on) {
            if (!g_stayDeadApplied) return;
            for (int i = 0; i < g_unprotectedCount; ++i)
                if (char* unit = LivingUnit(objects, g_unprotected[i].unit))
                    Field<uint32_t>(unit, HALO4_OBJECT_DAMAGE_FLAGS) |= g_unprotected[i].cleared;
            for (int r = 0; r < g_removedReviveCount; ++r) {
                char* unit = LivingUnit(objects, g_removedRevives[r].unit);
                if (!unit) continue;
                for (int i = 0; i < HALO4_AI_REVIVE_COUNT; ++i) {
                    char* entry = ai + HALO4_AI_REVIVES + i * HALO4_AI_REVIVE_SIZE;
                    if (Field<uint32_t>(entry, HALO4_AI_REVIVE_UNIT) != 0xFFFFFFFF) continue;
                    memcpy(entry, g_removedRevives[r].entry, HALO4_AI_REVIVE_SIZE);
                    Field<uint8_t>(unit, HALO4_OBJECT_REVIVABLE) |= 1;
                    break;
                }
            }
            LOG("Halo 4: AI stay dead off: restored %d AI, %d revive entries", g_unprotectedCount, g_removedReviveCount);
            g_unprotectedCount = g_removedReviveCount = g_deadCharacterCount = 0;
            g_stayDeadApplied = false;
            return;
        }
        g_stayDeadApplied = true;

        uint32_t followed[128];
        int followedCount = Halo4Internal::FollowedUnits(followed, (int)_countof(followed));

        // Deaths: remember the character so the mission can't bring it back.
        for (int i = 0; i < g_unprotectedCount; ++i) {
            Unprotected& u = g_unprotected[i];
            if (u.dead || LivingUnit(objects, u.unit)) continue;
            u.dead = true;
            bool known = u.character == 0xFFFFFFFF;
            for (int k = 0; k < g_deadCharacterCount && !known; ++k) known = g_deadCharacters[k] == u.character;
            if (!known && g_deadCharacterCount < kMaxDeadCharacters) g_deadCharacters[g_deadCharacterCount++] = u.character;
            LOG("Halo 4: AI stay dead: unit %08X died", u.unit);
        }

        // The revive list: take the mission's units off it.
        for (int i = 0; i < HALO4_AI_REVIVE_COUNT; ++i) {
            char* entry = ai + HALO4_AI_REVIVES + i * HALO4_AI_REVIVE_SIZE;
            uint32_t datum = Field<uint32_t>(entry, HALO4_AI_REVIVE_UNIT);
            if (datum == 0xFFFFFFFF || IsFollowed(followed, followedCount, datum)) continue;
            char* unit = ObjectFromDatum(objects, datum);
            if (unit && p.Contains(unit)) continue;
            char saved[HALO4_AI_REVIVE_SIZE];
            memcpy(saved, entry, sizeof(saved));
            ClearRevive(entry);
            if (unit) Field<uint8_t>(unit, HALO4_OBJECT_REVIVABLE) &= (uint8_t)~1;
            if (g_revivesSeen < 12) {
                ++g_revivesSeen;
                LOG("Halo 4: AI stay dead: took unit %08X off the revive list", datum);
            }
            if (Unprotect(state, datum) && g_removedReviveCount < HALO4_AI_REVIVE_COUNT) {
                g_removedRevives[g_removedReviveCount].unit = datum;
                memcpy(g_removedRevives[g_removedReviveCount++].entry, saved, sizeof(saved));
            }
        }

        // Script immortality on the player's allied AI bipeds (enemies, scripted vehicles and objects keep theirs).
        int mine = PlayerTeam(p);
        if (mine < 0) return;
        for (int i = 0; i < objects.HighWater(); ++i) {
            char* e = objects.Entry(i);
            uint16_t salt = *(uint16_t*)e;
            char* unit = Field<char*>(e, HALO4_OBJECT_ENTRY_OBJECT);
            if (!salt || !unit || Field<uint8_t>(e, HALO4_OBJECT_ENTRY_TYPE) != HALO4_OBJECT_TYPE_BIPED) continue;
            if (p.Contains(unit) || Field<uint32_t>(unit, HALO4_UNIT_PLAYER) != 0xFFFFFFFF) continue;
            auto& flags = Field<uint32_t>(unit, HALO4_OBJECT_DAMAGE_FLAGS);
            if (!(flags & kProtectFlags) || (flags & HALO4_DAMAGE_FLAG_DEAD)) continue;
            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            if (IsFollowed(followed, followedCount, datum)) continue;
            if (!Halo4Internal::TeamsFriends(state, Field<int8_t>(unit, HALO4_UNIT_TEAM), mine)) continue;
            uint32_t cleared = flags & kProtectFlags;
            flags &= ~kProtectFlags;
            if (Unprotected* u = Unprotect(state, datum)) u->cleared |= cleared;
        }
    }
}

namespace Halo4Extras {
    void Load(char* module) {
        g_module = module;
        g_tlsIndex = Blam::ModuleTlsIndex((HMODULE)module);
        ClearFrozen();
        g_damageHook.Install(OFFSET_HALO4_OBJECT_CAUSE_DAMAGE, (void*)&CauseDamageDetour, (void**)&g_origDamage,
                             "cause damage (disable friendly fire unavailable)");
        g_updateHook.Install(OFFSET_HALO4_OBJECT_UPDATE, (void*)&ObjectUpdateDetour, (void**)&g_origUpdate,
                             "object update (time stop unavailable)");
        g_wrapperHook.Install(OFFSET_HALO4_OBJECT_UPDATE_WRAPPER, (void*)&WrapperDetour, (void**)&g_origWrapper,
                              "object update wrapper (time stop may not freeze everything)");
        g_animHook.Install(OFFSET_HALO4_OBJECT_ANIMATION, (void*)&AnimDetour, (void**)&g_origAnim,
                           "object animation pass (frozen AI may keep walking)");
        g_barrelsHook.Install(OFFSET_HALO4_FIRE_MARKED_BARRELS, (void*)&FireBarrelsDetour, (void**)&g_origFireBarrels,
                              "marked barrel fire (frozen AI may still shoot)");
    }

    void Unload() {
        g_noFriendlyFire = false;
        g_timeStopOn = false;
        g_damageHook.Remove();
        g_updateHook.Remove();
        g_wrapperHook.Remove();
        g_animHook.Remove();
        g_barrelsHook.Remove();
        ClearFrozen();
        g_frozenActors.clear();
        g_unprotectedCount = g_removedReviveCount = g_deadCharacterCount = 0;
        g_stayDeadApplied = false;
        g_state = nullptr;
        g_module = nullptr;
    }

    // Map change: the datums belong to the old map's objects and actors (nothing to restore on them).
    void Reset() {
        g_timeStopOn = false;
        ClearFrozen();
        g_frozenActors.clear();
        g_frozenLogged = false;
        g_pinLogged = g_wokeLogged = 0;
        g_unprotectedCount = g_removedReviveCount = g_deadCharacterCount = 0;
        g_stayDeadApplied = false;
        g_revivesSeen = 0;
        g_blockedLogged = 0;
        g_seenHitCount = 0;
    }

    void Tick(void** state, const Games::Settings& s, const Halo4Cheats::Players& p, const Halo4Cheats::Array& objects) {
        g_state = state;
        g_noFriendlyFire = s.noFriendlyFire;
        if (!state || !objects.Valid()) return;

        if (s.timeStop && g_updateHook.target) {
            __try {
                FreezeTick(state, p, objects);
            } __except (Log::Exception(GetExceptionInformation(), "Halo 4 time stop")) {
                g_timeStopOn = false;
                ClearFrozen();
            }
        } else if (g_timeStopOn) {
            __try {
                ReleaseTime(state, objects);
            } __except (Log::Exception(GetExceptionInformation(), "Halo 4 time stop release")) {
                g_timeStopOn = false;
                ClearFrozen();
                g_frozenActors.clear();
            }
        }

        if (s.aiStayDead || g_stayDeadApplied) {
            __try {
                AiStayDead(state, p, objects, s.aiStayDead);
            } __except (Log::Exception(GetExceptionInformation(), "Halo 4 AI stay dead")) {
            }
        }
    }
}
