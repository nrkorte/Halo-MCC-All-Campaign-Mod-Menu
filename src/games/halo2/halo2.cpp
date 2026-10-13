#include "halo2.h"

#include <mutex>

#include "MinHook.h"

#include "cheats.h"
#include "core/log.h"
#include "offsets.h"
#include "tags.h"

using GameTickFn = int64_t(__fastcall*)(int64_t, int64_t, int64_t, int64_t);

static GameTickFn g_origGameTick = nullptr;

// Projectile creation: the chaos gun and recoil push act around the players' shots.
using CreateProjectilesFn = void(__fastcall*)(uint32_t weapon, int64_t barrel, int64_t c, int64_t d);
static CreateProjectilesFn g_origCreateProjectiles = nullptr;
namespace Halo2Extras { bool IsFrozenObject(uint32_t datum); }  // extras.cpp
static void __fastcall CreateProjectilesDetour(uint32_t weapon, int64_t barrel, int64_t c, int64_t d) {
    // Time stop: a frozen AI's (or its vehicle's) shot is dropped here. The caller (0x8EF960) clears the
    // barrel's "fired" mark itself after this call, so a dropped shot is gone, not delayed. The weapon's
    // ammo/heat were already spent by the trigger code. Players' weapons are never in the frozen table.
    if (Halo2Extras::IsFrozenObject(weapon)) return;
    Fun::Shot shot;
    Halo2Cheats::BeforeShot(weapon, (int16_t)barrel, shot);
    g_origCreateProjectiles(weapon, barrel, c, d);
    Fun::AfterShot(shot);
}

// Latest settings for the game thread: written by the render thread's Tick,
// copied out by the game-tick hook.
static std::mutex g_settingsLock;
static Games::Settings g_threadSettings;
static Games::FrameInput g_threadInput;
static bool g_haveThreadSettings = false;

// One game tick. Cheats that call engine functions run just before it.
static int64_t __fastcall GameTickDetour(int64_t a, int64_t b, int64_t c, int64_t d) {
    Games::Settings settings;
    Games::FrameInput input;
    bool have;
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        have = g_haveThreadSettings;
        settings = g_threadSettings;
        input = g_threadInput;
    }
    if (have) Halo2Cheats::MainThread(settings, input);
    return g_origGameTick(a, b, c, d);
}

bool Games::Halo2::Load(HMODULE module) {
    void* target = (char*)module + OFFSET_HALO2_GAME_TICK;
    if (MH_CreateHook(target, (void*)&GameTickDetour, (void**)&g_origGameTick) == MH_OK &&
        MH_EnableHook(target) == MH_OK) {
        m_gameTick = target;
    } else {
        MH_RemoveHook(target);
        LOG("Halo 2: failed to hook the game tick at %p; noclip/damage/give weapon unavailable", target);
    }
    void* create = (char*)module + OFFSET_HALO2_BARREL_CREATE_PROJECTILES;
    if (MH_CreateHook(create, (void*)&CreateProjectilesDetour, (void**)&g_origCreateProjectiles) == MH_OK &&
        MH_EnableHook(create) == MH_OK) {
        m_createProjectiles = create;
    } else {
        MH_RemoveHook(create);
        LOG("Halo 2: failed to hook projectile creation at %p; chaos gun and recoil push unavailable", create);
    }
    Halo2Tags::SetModule(module);
    Halo2Cheats::Load(module);
    return true;
}

void Games::Halo2::Unload() {
    if (m_gameTick) {
        MH_DisableHook(m_gameTick);
        MH_RemoveHook(m_gameTick);
        m_gameTick = nullptr;
    }
    if (m_createProjectiles) {
        MH_DisableHook(m_createProjectiles);
        MH_RemoveHook(m_createProjectiles);
        m_createProjectiles = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_haveThreadSettings = false;
    }
    Halo2Cheats::Unload();
    Halo2Tags::SetModule(nullptr);
}

uint64_t Games::Halo2::Features() const {
    // Not in Halo 2 yet: camera distance. Equipment
    // is active camo (Arbiter, or Envy). No infinite boost: boosting never
    // runs out in Halo 2.
    return kGodMode | kInfiniteAmmo | kBottomlessClip | kInfiniteGrenades | kOneHitKill | kSpeed | kJump | kGravity |
           kGameSpeed | kFly | kNoclip | kNoBoundaries | kDamage | kGiveWeapon | kAmmoType | kAimbot | kAiBlind |
           kAiCantShoot | kFullAuto | kRapidFire | kNoSpread | kNoRecoil | kNoOverheat | kInfiniteEquipment | kAiDeaf | kNoHud | kThirdPerson |
           kEnvySkull | kVehicleSpeed | kVehicleRapidFire | kVehicleGodMode | kVehicleNoHijack | kVehicleNoEject | kCheckpoints | kSpawning |
           kSkulls | kChaosGun | kRain | kRecoilPush | kTeleport |
           // Ported from Reach 2026-10-10 (each needs its Halo 2 implementation; see NOTES.md)
           kNuke | kDespawnAi | kPoisonTouch | kRandomTeams | kNoFriendlyFire | kNoBetrayal | kPlayerTeam |
           kTimeStop | kLaunchOnHit | kUpgradeAi | kSquadSpawning | kCheckpointLock;
}

uint64_t Games::Halo2::Features2() const {
    return kAiScale | kRandomSwap | kDoubleJump | kSwordFly | kDot | kFov | kGrenadeDrop | kExplosionSize;
}

void Games::Halo2::Tick(const Settings& settings, const FrameInput& input, Status& status) {
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_threadSettings = settings;
        g_threadInput = input;
        g_haveThreadSettings = true;
    }
    status.inGame = Halo2Cheats::Apply(settings, input, status);
}
