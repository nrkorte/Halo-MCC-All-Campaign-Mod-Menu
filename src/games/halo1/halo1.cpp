#include "halo1.h"

#include <mutex>

#include "MinHook.h"

#include "cheats.h"
#include "core/log.h"
#include "offsets.h"
#include "tags.h"

using GameTickFn = int64_t(__fastcall*)(int64_t, int64_t, int64_t, int64_t);

static GameTickFn g_origGameTick = nullptr;

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
    if (have) Halo1Cheats::MainThread(settings, input);
    return g_origGameTick(a, b, c, d);
}

bool Games::Halo1::Load(HMODULE module) {
    void* target = (char*)module + OFFSET_HALO1_GAME_TICK;
    if (MH_CreateHook(target, (void*)&GameTickDetour, (void**)&g_origGameTick) == MH_OK &&
        MH_EnableHook(target) == MH_OK) {
        m_gameTick = target;
    } else {
        MH_RemoveHook(target);
        LOG("Halo CE: failed to hook the game tick at %p; noclip/damage/give weapon unavailable", target);
    }
    Halo1Tags::SetModule(module);
    Halo1Cheats::Load(module);
    return true;
}

void Games::Halo1::Unload() {
    if (m_gameTick) {
        MH_DisableHook(m_gameTick);
        MH_RemoveHook(m_gameTick);
        m_gameTick = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_haveThreadSettings = false;
    }
    Halo1Cheats::Unload();
    Halo1Tags::SetModule(nullptr);
}

uint64_t Games::Halo1::Features() const {
    // Not in CE: equipment, map boundaries (no kill triggers or soft ceilings).
    return kGodMode | kInfiniteAmmo | kBottomlessClip | kInfiniteGrenades | kOneHitKill | kSpeed | kJump |
           kGravity | kGameSpeed | kFly | kNoclip | kDamage | kGiveWeapon | kAmmoType | kAimbot | kAiBlind |
           kAiCantShoot | kFullAuto | kRapidFire | kNoSpread | kNoRecoil | kNoOverheat | kAiDeaf | kThirdPerson |
           kNoHud | kVehicleSpeed | kVehicleRapidFire | kVehicleGodMode | kVehicleNoEject | kCheckpoints | kSpawning |
           kNuke | kDespawnAi | kPoisonTouch | kRandomTeams | kNoFriendlyFire | kNoBetrayal | kPlayerTeam | kTimeStop | kSkulls | kChaosGun | kRain | kRecoilPush | kTeleport | kLaunchOnHit | kUpgradeAi | kSquadSpawning | kCheckpointLock;
}

uint64_t Games::Halo1::Features2() const {
    return kAiScale | kRandomSwap | kDoubleJump | kDot;
}

void Games::Halo1::Tick(const Settings& settings, const FrameInput& input, Status& status) {
    {
        std::lock_guard<std::mutex> lock(g_settingsLock);
        g_threadSettings = settings;
        g_threadInput = input;
        g_haveThreadSettings = true;
    }
    status.inGame = Halo1Cheats::Apply(settings, status);
}
