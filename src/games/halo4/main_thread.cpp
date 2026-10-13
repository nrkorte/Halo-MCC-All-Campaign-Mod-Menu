// Halo 4 cheats that call engine functions. Runs once per game tick on
// the game's main thread, before the tick (see halo4.cpp).

#include <windows.h>

#include <cmath>
#include <cstring>

#include "cheat_util.h"
#include "cheats.h"
#include "core/log.h"
#include "internal.h"
#include "spawn.h"
#include "tags.h"

using namespace Halo4Cheats;

namespace {
    constexpr float kNoclipSpeed = 6.0f;      // world units per second
    constexpr float kNoclipFastScale = 4.0f;
    constexpr int kMaxTracked = 4096;         // object table slots we track for damage
    constexpr float kPi = 3.14159265f;
    constexpr float kAimMinCone = 3.0f;       // degrees from the crosshair, at 1% strength
    constexpr float kAimMaxCone = 55.0f;      // at 100%: about the whole screen
    constexpr float kAimRange = 200.0f;       // world units

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using UnitAddWeaponFn = bool(__fastcall*)(uint32_t unit, uint32_t weapon, int mode);
    using UnitKillFn = void(__fastcall*)(uint32_t unit, uint32_t, bool, int, int);
    using HeadPositionFn = void(__fastcall*)(uint32_t unit, float* out);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up, int64_t, bool);

    char* g_module = nullptr;  // set by Halo4Cheats::Load via SetMainThreadModule

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    // Noclip
    bool g_noclipActive = false;
    uint32_t g_noclipUnit = 0xFFFFFFFF;
    float g_anchor[3] = {};
    LARGE_INTEGER g_lastTick{};

    // Damage multiplier: last seen vitality per object slot.
    struct Tracked {
        uint32_t datum;
        float health;
        float shield;
    };
    Tracked g_tracked[kMaxTracked];

    uint32_t g_giveSerial = 0;
    uint32_t g_saveSerial = 0;
    uint32_t g_loadSerial = 0;

    float TickSeconds(LARGE_INTEGER& last) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart : 0.0f;
        last = now;
        return dt > 0.1f ? 0.1f : dt;
    }

    // Holds player 1 in place, and flies them where they're looking while
    // the noclip button is held. The engine's teleport ignores collision.
    void Noclip(void** state, const Players& p, bool on, const Games::FrameInput& in, float dt) {
        // Paused while seated (vehicle, turret): re-anchor where they get out.
        bool seated = p.count > 0 && Field<uint32_t>(p.units[0], HALO4_OBJECT_PARENT) != 0xFFFFFFFF;
        bool sameUnit = p.count > 0 && p.datums[0] == g_noclipUnit && !seated;
        if (g_noclipActive && (!on || !sameUnit)) {
            g_noclipActive = false;
            LOG("Halo 4: noclip off");
        }
        if (!on || p.count == 0 || seated) return;

        char* unit = p.units[0];
        float* pos = &Field<float>(unit, HALO4_OBJECT_POSITION);
        if (!g_noclipActive) {
            memcpy(g_anchor, pos, sizeof(g_anchor));
            g_noclipUnit = p.datums[0];
            g_noclipActive = true;
            LOG("Halo 4: noclip on at %.2f %.2f %.2f", pos[0], pos[1], pos[2]);
        }

        char* control = (char*)state[HALO4_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        float yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
        if (in.noclipForward) {
            float step = kNoclipSpeed * (in.fast ? kNoclipFastScale : 1.0f) * dt;
            g_anchor[0] += cosf(yaw) * cosf(pitch) * step;
            g_anchor[1] += sinf(yaw) * cosf(pitch) * step;
            g_anchor[2] += sinf(pitch) * step;
        }

        // Not object_teleport: for a player it also runs a respawn-style move
        // that snaps the first-person view level every tick. Setting only the
        // position leaves the view alone. Velocity is cleared so gravity
        // doesn't build up between ticks.
        Engine<SetPositionFn>(OFFSET_HALO4_OBJECT_SET_POSITION)(p.datums[0], g_anchor, nullptr, nullptr, 0, false);
        memset(&Field<float>(unit, HALO4_OBJECT_VELOCITY), 0, 3 * sizeof(float));

        static ULONGLONG s_lastLog = 0;  // look angles, to diagnose noclip's view
        if (GetTickCount64() - s_lastLog > 2000) {
            s_lastLog = GetTickCount64();
            LOG("Halo 4: noclip view yaw %.2f pitch %.2f, anchor %.1f %.1f %.1f, unit at %.1f %.1f %.1f", yaw, pitch,
                g_anchor[0], g_anchor[1], g_anchor[2], pos[0], pos[1], pos[2]);
        }
    }

    // Scales the health and shield enemies lost since last tick. Kills them
    // properly if the scaled damage is fatal.
    void Damage(const Players& p, const Array& objects, float m) {
        if (m == 1.0f) {
            memset(g_tracked, 0, sizeof(g_tracked));
            return;
        }

        for (int i = 0; i < objects.HighWater() && i < kMaxTracked; ++i) {
            char* entry = objects.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE);
            char* object = Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
            Tracked& t = g_tracked[i];
            float* health = object ? Health(object) : nullptr;
            if (salt == 0 || !health || !IsKillableEnemy(object, type, p)) {
                t.datum = 0;
                continue;
            }

            uint32_t datum = ((uint32_t)salt << 16) | (uint32_t)i;
            float* shield = Shield(object);
            float shieldNow = shield ? *shield : 0.0f;
            if (t.datum != datum) {
                t = {datum, *health, shieldNow};
                continue;
            }
            if (*health <= 0.0f) {  // already dead
                t.datum = 0;
                continue;
            }

            if (shield && shieldNow < t.shield) {
                float s = t.shield - (t.shield - shieldNow) * m;
                *shield = s > 0.0f ? s : 0.0f;
            }
            if (*health < t.health) {
                float h = t.health - (t.health - *health) * m;
                if (h <= 0.0f) {
                    *health = 0.0001f;
                    Engine<UnitKillFn>(OFFSET_HALO4_UNIT_KILL)(datum, 0, true, 2, 0);
                    t.datum = 0;
                    continue;
                }
                *health = h;
            }
            t.health = *health;
            t.shield = shield ? *shield : 0.0f;
        }
    }

    // A give waiting for the game to free the held weapon's slot. Deleting
    // an object takes effect a tick or so later.
    struct PendingGive {
        bool active = false;
        uint32_t weapon = 0xFFFFFFFF;  // new weapon, already created
        uint32_t held = 0xFFFFFFFF;    // weapon being replaced (deleted)
        int frames = 0;
    };
    PendingGive g_give;

    int SlotOf(char* unit, uint32_t weapon) {
        for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w)
            if (Field<uint32_t>(unit, HALO4_UNIT_WEAPONS + w * 4) == weapon) return w;
        return -1;
    }

    // Adds the weapon into a free slot and switches to it. Deletes it if the
    // game won't take it.
    void AddAndSelect(char* unit, uint32_t unitDatum, uint32_t weapon) {
        bool added = Engine<UnitAddWeaponFn>(OFFSET_HALO4_UNIT_ADD_WEAPON)(unitDatum, weapon, HALO4_ADD_WEAPON_FREE_SLOT);
        int slot = added ? SlotOf(unit, weapon) : -1;
        if (slot >= 0) Field<int8_t>(unit, HALO4_UNIT_DESIRED_WEAPON) = (int8_t)slot;
        else Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(weapon);
        LOG("Halo 4: give weapon: %s (object %08X, slot %d)", slot >= 0 ? "added" : "add FAILED", weapon, slot);
    }

    // Replaces player 1's held weapon with a new one of the chosen type (or
    // fills a free slot). The new weapon is created first, so a type the
    // game can't create costs nothing.
    void GiveWeapon(const Players& p, const Array& objects, uint32_t weaponTag) {
        if (p.count == 0) { LOG("Halo 4: give weapon: no player unit"); return; }
        if (!Halo4Tags::Get(weaponTag)) { LOG("Halo 4: give weapon %08X: tag not loaded", weaponTag); return; }
        char* unit = p.units[0];
        uint32_t unitDatum = p.datums[0];

        if (g_give.active) {  // replaced by this newer request
            Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
        }

        static uint8_t placement[HALO4_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO4_OBJECT_PLACEMENT_NEW)(placement, weaponTag, unitDatum, 0);
        uint32_t weapon = Engine<ObjectNewFn>(OFFSET_HALO4_OBJECT_NEW)(placement);
        if (weapon == 0xFFFFFFFF) {
            LOG("Halo 4: give weapon %08X: the game couldn't create it here", weaponTag);
            return;
        }

        bool freeSlot = false;
        for (int w = 0; w < HALO4_PLAYER_WEAPON_SLOTS; ++w)
            freeSlot |= Field<uint32_t>(unit, HALO4_UNIT_WEAPONS + w * 4) == 0xFFFFFFFF;

        uint32_t held = 0xFFFFFFFF;
        if (freeSlot || !HeldWeapon(unit, objects, &held)) {
            AddAndSelect(unit, unitDatum, weapon);
            return;
        }

        Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(held);
        g_give = {true, weapon, held, 0};
        LOG("Halo 4: give weapon %08X: removing held %08X, adding once its slot frees", weaponTag, held);
    }

    // Finishes a pending give once the held weapon has left the inventory.
    void ContinueGive(const Players& p) {
        if (!g_give.active) return;
        if (p.count == 0) {  // died: drop the request
            Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(g_give.weapon);
            g_give = {};
            return;
        }
        char* unit = p.units[0];
        if (SlotOf(unit, g_give.held) < 0) {
            AddAndSelect(unit, p.datums[0], g_give.weapon);
            g_give = {};
        } else if (++g_give.frames > 120) {
            Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(g_give.weapon);
            LOG("Halo 4: give weapon: held weapon never left the inventory; gave up");
            g_give = {};
        }
    }

    // Aimbot strength: while firing, turns player 1's view toward the enemy
    // head nearest the crosshair (see the Halo 3 version).
    void AimPull(void** state, const Players& p, const Array& objects, float strength) {
        if (strength <= 0.0f || p.count == 0) return;
        if (IsMeleeOnly(HeldWeapon(p.units[0], objects))) return;  // sword: no pull
        float s = strength / 100.0f;
        auto head = Engine<HeadPositionFn>(OFFSET_HALO4_UNIT_HEAD_POSITION);

        char* control = (char*)state[HALO4_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        float& yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
        float& pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
        float forward[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        head(p.datums[0], eye);

        float minCos = cosf((kAimMinCone + (kAimMaxCone - kAimMinCone) * s) * kPi / 180.0f);
        float best[3] = {};
        float bestCos = minCos;
        bool found = false;
        for (int i = 0; i < objects.HighWater(); ++i) {
            char* entry = objects.Entry(i);
            uint16_t salt = *(uint16_t*)entry;
            int type = Field<uint8_t>(entry, HALO4_OBJECT_ENTRY_TYPE);
            char* object = Field<char*>(entry, HALO4_OBJECT_ENTRY_OBJECT);
            if (salt == 0 || !object || !IsKillableEnemy(object, type, p)) continue;
            float* health = Health(object);
            if (!health || *health <= 0.0f) continue;

            float pos[3];
            head(((uint32_t)salt << 16) | (uint32_t)i, pos);
            float d[3] = {pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]};
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 0.1f || len > kAimRange) continue;
            float c = (d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2]) / len;
            if (c <= bestCos) continue;
            bestCos = c;
            memcpy(best, d, sizeof(best));
            found = true;
        }
        if (!found) return;

        float targetYaw = atan2f(best[1], best[0]);
        float targetPitch = atan2f(best[2], sqrtf(best[0] * best[0] + best[1] * best[1]));
        float dyaw = remainderf(targetYaw - yaw, 2.0f * kPi);  // shortest way round
        float turn = s * s;                                      // fraction of the gap closed per tick
        yaw = remainderf(yaw + dyaw * turn, 2.0f * kPi);
        if (yaw < 0.0f) yaw += 2.0f * kPi;  // Halo 4 keeps yaw in 0..2pi (checked live)
        pitch += (targetPitch - pitch) * turn;
    }

    // Each cheat gets its own guard so one fault can't stop the others.
    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    bool ReadPlayers(void** state, Array& objects, Players& p) {
        __try {
            Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
            objects.header = (char*)state[HALO4_TLS_SLOT_OBJECTS];
            if (!players.Valid() || !objects.Valid()) return false;
            p = FindPlayers(players, objects);
            return true;
        } __except (Log::Exception(GetExceptionInformation(), "Halo 4 main-thread players")) {
            return false;
        }
    }

    void RunAll(void** state, const Games::Settings& s, const Games::FrameInput& in) {
        Array objects;
        Players p;
        if (!ReadPlayers(state, objects, p)) return;

        g_friendState = state;
        float dt = TickSeconds(g_lastTick);
        Guard("Halo 4 noclip", [&] { Noclip(state, p, s.noclip, in, dt); });
        Guard("Halo 4 damage multiplier", [&] { Damage(p, objects, s.damage); });
        Guard("Halo 4 aimbot strength", [&] {
            if (s.aimbot && in.firing) AimPull(state, p, objects, s.aimbotStrength);
        });
        if (s.saveCheckpointSerial != g_saveSerial) {
            g_saveSerial = s.saveCheckpointSerial;
            LOG("Halo 4: saving a checkpoint");
            Halo4Checkpoints::BeginOwnSave();
            Guard("Halo 4 save checkpoint", [&] {
                Engine<void(__fastcall*)(int32_t)>(OFFSET_HALO4_GAME_SAVE)(HALO4_GAME_SAVE_IMMEDIATE);
            });
            Halo4Checkpoints::EndOwnSave();
        }
        if (s.loadCheckpointSerial != g_loadSerial) {
            g_loadSerial = s.loadCheckpointSerial;
            LOG("Halo 4: loading the last checkpoint");
            Guard("Halo 4 load checkpoint", [&] {
                *(uint16_t*)(g_module + OFFSET_HALO4_GAME_REVERT_FLAG) = HALO4_GAME_REVERT_REQUEST;
            });
        }
        bool request = s.giveWeaponSerial != g_giveSerial;
        if (request) {
            g_giveSerial = s.giveWeaponSerial;
            LOG("Halo 4: give weapon request %08X (%d player units)", s.giveWeapon, p.count);
        }
        __try {
            if (request) GiveWeapon(p, objects, s.giveWeapon);
            ContinueGive(p);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 4 give weapon")) {
            g_give = {};
        }
        Halo4Spawn::Tick(state, p, objects, s);
        Halo4Spawn::FunTick(state, p, objects, s, dt);
        Guard("Halo 4 teams", [&] { Halo4Teams::Tick(state, s, p, objects); });
        Guard("Halo 4 extras", [&] { Halo4Extras::Tick(state, s, p, objects); });
        Guard("Halo 4 checkpoints", [&] { Halo4Checkpoints::Tick(state, s, p, objects); });
        Guard("Halo 4 fun extras", [&] { Halo4Fun::Tick(state, s, in, p, objects); });
        Guard("Halo 4 equipment", [&] { Halo4Equipment::Tick(state, s, p, objects); });
        Guard("Halo 4 upgrade AI", [&] { Halo4Upgrade::Tick(state, s, p, objects); });
        Guard("Halo 4 vehicles", [&] { Halo4Vehicles::Tick(state, s, in, p, objects); });
    }
}

namespace Halo4Cheats {
    void SetMainThreadModule(HMODULE halo4) { g_module = (char*)halo4; }

    void MoveNoclipAnchor(const float* position) {
        if (g_noclipActive) memcpy(g_anchor, position, sizeof(g_anchor));
    }

    void ResetMainThread() {
        g_noclipActive = false;
        g_noclipUnit = 0xFFFFFFFF;
        g_lastTick = {};
        g_give = {};  // its objects went with the map
        memset(g_tracked, 0, sizeof(g_tracked));
    }
}

void Halo4Cheats::MainThread(void** gameState, const Games::Settings& settings, const Games::FrameInput& input) {
    if (!g_module || !gameState) return;
    // Don't run any "Give" that was pressed before this session.
    static bool s_first = true;
    if (s_first) {
        g_giveSerial = settings.giveWeaponSerial;
        g_saveSerial = settings.saveCheckpointSerial;
        g_loadSerial = settings.loadCheckpointSerial;
        s_first = false;
    }
    RunAll(gameState, settings, input);
}
