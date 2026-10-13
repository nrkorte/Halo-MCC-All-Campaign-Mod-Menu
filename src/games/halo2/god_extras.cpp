// Halo 2: god mode that survives falling out of the world.
//
// Cause (static, see god_extras_offsets.h): scenario kill volumes (scnr +0x230)
// are tested every tick by 0x75F620 from the biped and vehicle updates, and a
// hit applies an instant-kill damage that skips cannot-take-damage. The fix
// hooks 0x75F620 and answers "no" for the players' units and the vehicle they
// sit in while god mode is on. A player who falls below the level just keeps
// falling (nothing teleports them), which is the least surprising behaviour.
#include <atomic>

#include <MinHook.h>

#include "core/log.h"
#include "god_extras.h"
#include "god_extras_offsets.h"

using namespace Halo2Cheats;

namespace {
    using KillVolumeFn = uint8_t(__fastcall*)(uint32_t);
    KillVolumeFn g_orig = nullptr;
    void* g_hook = nullptr;

    std::atomic<bool> g_god{false};
    // Datums to protect: each local player's unit and the vehicle it is in.
    std::atomic<uint32_t> g_safe[kMaxLocalPlayers * 2];
    std::atomic<bool> g_logged{false};

    uint8_t __fastcall KillVolumeDetour(uint32_t datum) {
        uint8_t hit = g_orig(datum);
        if (hit && g_god.load(std::memory_order_relaxed)) {
            for (auto& s : g_safe) {
                if (s.load(std::memory_order_relaxed) == datum) {
                    if (!g_logged.exchange(true)) LOG("Halo 2: god mode vetoed a kill volume (object %08X)", datum);
                    return 0;
                }
            }
        }
        return hit;
    }
}

void Halo2God::Load(char* module) {
    for (auto& s : g_safe) s = 0xFFFFFFFF;
    void* target = module + OFFSET_HALO2_IN_KILL_VOLUME;
    if (MH_CreateHook(target, (void*)&KillVolumeDetour, (void**)&g_orig) == MH_OK && MH_EnableHook(target) == MH_OK) {
        g_hook = target;
    } else {
        MH_RemoveHook(target);
        LOG("Halo 2: failed to hook the kill volume test at %p; god mode won't stop falling deaths", target);
    }
}

void Halo2God::Unload() {
    g_god = false;
    if (g_hook) {
        MH_DisableHook(g_hook);
        MH_RemoveHook(g_hook);
        g_hook = nullptr;
    }
}

void Halo2God::Tick(bool godMode, const Players& p, const Objects& objects) {
    for (int i = 0; i < kMaxLocalPlayers; ++i) {
        uint32_t unit = 0xFFFFFFFF, vehicle = 0xFFFFFFFF;
        if (godMode && i < p.count) {
            unit = p.datums[i];
            if (VehicleOf(p.units[i], objects)) vehicle = Field<uint32_t>(p.units[i], HALO2_OBJECT_PARENT);
        }
        g_safe[i * 2] = unit;
        g_safe[i * 2 + 1] = vehicle;
    }
    g_god = godMode;
}
