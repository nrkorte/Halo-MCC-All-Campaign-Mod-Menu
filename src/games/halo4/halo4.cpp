#include "halo4.h"

#include <intrin.h>

#include <atomic>
#include <mutex>

#include "MinHook.h"

#include "cheats.h"
#include "core/log.h"
#include "games/blam.h"
#include "offsets.h"
#include "tags.h"

using EngineFn = int64_t(__fastcall*)(int64_t, int64_t, int64_t, int64_t);
using BarrelFireFn = void(__fastcall*)(uint32_t weapon, int64_t barrel, int64_t flag, int64_t d);
using CameraUpdateFn = int64_t(__fastcall*)(void* camera, int64_t player, float dt, char* command);
using UnitActionFn = uint64_t(__fastcall*)(uint32_t unit, int32_t action, uint64_t c, uint64_t d);

static EngineFn g_origEngine = nullptr;
static EngineFn g_origGameTick = nullptr;
static BarrelFireFn g_origBarrelFire = nullptr;
using ProjectileOfFn = uint32_t*(__fastcall*)(uint32_t* out, uint32_t weapon, int64_t barrel);
static ProjectileOfFn g_origProjectileOf = nullptr;
static CameraUpdateFn g_origCameraUpdate = nullptr;
static UnitActionFn g_origUnitAction = nullptr;
static uintptr_t g_base = 0;  // halo4.dll
static int32_t g_tlsIndex = -1;
static std::atomic<void**> g_gameState{nullptr};  // main thread's TLS block
static std::atomic<float> g_cameraDistance{0.0f};  // extra world units, 0 = game default

// Latest settings for the game thread: written by the render thread's Tick,
// copied out by the game-tick hook.
static std::mutex g_settingsLock;
static Games::Settings g_threadSettings;
static Games::FrameInput g_threadInput;
static bool g_haveThreadSettings = false;

// Halo 4's main loop: called on the game thread when a session starts. Only
// used to grab that thread's TLS block, where the game state lives.
static int64_t __fastcall EngineDetour(int64_t a, int64_t b, int64_t c, int64_t d) {
    void** state = Blam::ThreadTlsBlock(g_tlsIndex);
    if (g_gameState.exchange(state) != state) LOG("Halo 4: captured game state at %p", (void*)state);
    return g_origEngine(a, b, c, d);
}

// One game tick. Cheats that call engine functions run just before it,
// inside the frame, where the engine's per-thread context is valid.
static int64_t __fastcall GameTickDetour(int64_t a, int64_t b, int64_t c, int64_t d) {
    // The tick's thread owns the game state: capture it here too, in case the
    // main loop hook ran on another thread or before the mod loaded.
    void** tick = Blam::ThreadTlsBlock(g_tlsIndex);
    if (tick && g_gameState.exchange(tick) != tick) LOG("Halo 4: captured game state at %p (game tick)", (void*)tick);
    void** state = g_gameState;
    if (state && Blam::ThreadTlsBlock(g_tlsIndex) == state) {
        Games::Settings settings;
        Games::FrameInput input;
        bool have;
        {
            std::lock_guard<std::mutex> lock(g_settingsLock);
            have = g_haveThreadSettings;
            settings = g_threadSettings;
            input = g_threadInput;
        }
        if (have) Halo4Cheats::MainThread(state, settings, input);
    }
    return g_origGameTick(a, b, c, d);
}

// Barrel fire: skipped for AI-held weapons while "AI can't shoot" is on.
static void __fastcall BarrelFireDetour(uint32_t weapon, int64_t barrel, int64_t flag, int64_t d) {
    if (!Halo4Cheats::WeaponMayFire(g_gameState, weapon)) return;
    Fun::Shot shot;  // the chaos gun and recoil push act around the players' shots
    Halo4Cheats::BeforeShot(g_gameState, weapon, (int16_t)barrel, shot);
    g_origBarrelFire(weapon, barrel, flag, d);
    Fun::AfterShot(shot);
}

// A barrel's projectile tag, looked up when the projectile is made: the
// chaos gun's pick for the players' weapons.
static uint32_t* __fastcall ProjectileOfDetour(uint32_t* out, uint32_t weapon, int64_t barrel) {
    uint32_t* result = g_origProjectileOf(out, weapon, barrel);
    if (Fun::ChaosProjectile() == 0xFFFFFFFF || !out) return result;  // chaos gun off
    uint32_t pick = Halo4Cheats::ChaosProjectileFor(g_gameState, weapon);
    if (pick != 0xFFFFFFFF) *out = pick;
    return result;
}

// The following (third-person) camera's update. Adds to the distance it
// hands the observer, which backs the camera off from the player.
static int64_t __fastcall CameraUpdateDetour(void* camera, int64_t player, float dt, char* command) {
    int64_t result = g_origCameraUpdate(camera, player, dt, command);
    float extra = g_cameraDistance;
    if (command && extra > 0.0f) *(float*)(command + HALO4_OBSERVER_COMMAND_DISTANCE) += extra;
    return result;
}

// Unit actions: the flip check sends action 0x19 (eject) through this. It's
// dropped for players while "Cannot be ejected" is on, only from the flip
// check (the function has ~90 other callers).
static uint64_t __fastcall UnitActionDetour(uint32_t unit, int32_t action, uint64_t c, uint64_t d) {
    if (action == HALO4_UNIT_ACTION_FLIP_EJECT &&
        (uintptr_t)_ReturnAddress() - g_base == HALO4_FLIP_EJECT_RETURN &&
        !Halo4Cheats::FlipEjectAllowed(g_gameState, unit))
        return 0;  // the unit update ignores the result here
    return g_origUnitAction(unit, action, c, d);
}

static void* Hook(HMODULE module, int rva, void* detour, void** original, const char* what) {
    void* target = (char*)module + rva;
    if (MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK) return target;
    MH_RemoveHook(target);
    LOG("Halo 4: failed to hook %s at %p", what, target);
    return nullptr;
}

bool Games::Halo4::Load(HMODULE module) {
    g_base = (uintptr_t)module;
    g_tlsIndex = Blam::ModuleTlsIndex(module);
    if (g_tlsIndex < 0) {
        LOG("Halo 4: no TLS directory; not hooking");
        return false;
    }

    m_hooks[0] = Hook(module, OFFSET_HALO4_ENGINE, (void*)&EngineDetour, (void**)&g_origEngine, "main loop");
    if (!m_hooks[0]) return false;
    m_hooks[1] = Hook(module, OFFSET_HALO4_GAME_TICK, (void*)&GameTickDetour, (void**)&g_origGameTick,
                      "game tick (noclip/damage/give weapon/aimbot pull unavailable)");
    m_hooks[2] = Hook(module, OFFSET_HALO4_BARREL_FIRE, (void*)&BarrelFireDetour, (void**)&g_origBarrelFire,
                      "barrel fire (AI can't shoot unavailable)");
    m_hooks[3] = Hook(module, OFFSET_HALO4_FOLLOWING_CAMERA_UPDATE, (void*)&CameraUpdateDetour,
                      (void**)&g_origCameraUpdate, "following camera (camera distance unavailable)");
    m_hooks[4] = Hook(module, OFFSET_HALO4_UNIT_ACTION, (void*)&UnitActionDetour, (void**)&g_origUnitAction,
                      "unit action (cannot be ejected unavailable)");
    m_hooks[5] = Hook(module, OFFSET_HALO4_BARREL_PROJECTILE_OF, (void*)&ProjectileOfDetour,
                      (void**)&g_origProjectileOf, "projectile lookup (chaos gun unavailable)");

    Halo4Tags::SetModule(module);
    Halo4Cheats::Load(module);
    return true;
}

void Games::Halo4::Unload() {
    for (void*& target : m_hooks) {
        if (!target) continue;
        MH_DisableHook(target);
        MH_RemoveHook(target);
        target = nullptr;
    }
    g_gameState = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_haveThreadSettings = false;
    }
    g_cameraDistance = 0.0f;
    Halo4Cheats::Unload();
    Halo4Tags::SetModule(nullptr);
}

uint64_t Games::Halo4::Features() const {
    // Not in Halo 4: the Acrophobia skull (fly) and equipment that despawns
    // (armor abilities stay with you, as in Reach).
    return kGodMode | kInfiniteAmmo | kBottomlessClip | kInfiniteGrenades | kOneHitKill | kSpeed | kJump |
           kGravity | kGameSpeed | kNoclip | kNoBoundaries | kDamage | kGiveWeapon | kAmmoType | kAimbot |
           kAiBlind | kAiCantShoot | kFullAuto | kRapidFire | kNoSpread | kNoRecoil | kNoOverheat |
           kInfiniteEquipment | kAiDeaf | kThirdPerson | kCameraDistance | kNoHud | kVehicleSpeed |
           kVehicleRapidFire | kVehicleGodMode | kVehicleNoHijack | kVehicleNoEmp |
           kVehicleInfiniteBoost | kVehicleNoRecoil | kVehicleNoEject | kCheckpoints | kSpawning |
           kSkulls | kChaosGun | kRain | kRecoilPush | kTeleport | kUpgradeAi | kRandomTeams | kPlayerTeam |
           kSquadSpawning | kNuke | kDespawnAi | kNoFriendlyFire | kNoBetrayal | kGiveEquipment | kPoisonTouch |
           kPerfectCamo | kTimeStop | kLaunchOnHit | kCheckpointLock | kCheckpointHistory | kPilotAiVehicles |
           kUpgradeVehicle | kTakePilotSeat | kAiStayDead;
}

uint64_t Games::Halo4::Features2() const {
    return kDoubleJump | kAiScale | kRandomSwap | kDot | kInfiniteSprint;
}

void Games::Halo4::Tick(const Settings& settings, const FrameInput& input, Status& status) {
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_threadSettings = settings;
        g_threadInput = input;
        g_haveThreadSettings = true;
    }
    g_cameraDistance = settings.thirdPerson ? settings.cameraDistance : 0.0f;

    void** state = g_gameState;
    if (!state) return;
    status.inGame = Halo4Cheats::Apply(state, settings, status);
}
