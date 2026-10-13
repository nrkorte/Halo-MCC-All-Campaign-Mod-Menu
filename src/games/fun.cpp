#include "fun.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/log.h"

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kTeleportRange = 300.0f;  // world units
    constexpr int kMaxRain = 64;
    constexpr float kRainMinFraction = 0.3f;  // of the radius: nothing lands right on the player
    constexpr float kRainHeight = 15.0f;      // world units above the spot, or just under a ceiling

    // --- Render thread -> game thread -------------------------------------
    struct Pools {
        std::vector<uint32_t> vehicles, objects, ai, weapons, projectiles;
    };
    std::mutex g_poolsLock;
    Pools g_pools;
    uint32_t g_random = 0x9E3779B9u;  // xorshift, under g_poolsLock

    std::atomic<bool> g_chaosGun{false};
    std::atomic<float> g_push{0.0f};  // world units / s per shot, 0 = off

    uint32_t Next() {
        g_random ^= g_random << 13;
        g_random ^= g_random >> 17;
        g_random ^= g_random << 5;
        return g_random;
    }
    float Random01() {
        std::lock_guard<std::mutex> lock(g_poolsLock);
        return (Next() >> 8) / 16777216.0f;
    }
    uint32_t PickFrom(const std::vector<uint32_t>& list) {
        return list.empty() ? 0xFFFFFFFF : list[Next() % list.size()];
    }

    // A random entry of a rain kind (Games::Settings::rainKind).
    uint32_t RainPick(int kind, uint32_t selection) {
        if (kind == 5) return selection;
        std::lock_guard<std::mutex> lock(g_poolsLock);
        const Pools& p = g_pools;
        switch (kind) {
            case 1: return PickFrom(p.vehicles);
            case 2: return PickFrom(p.objects);
            case 3: return PickFrom(p.ai);
            case 4: return PickFrom(p.weapons);
            default: {
                // A kind first, so the long object lists don't drown out the rest.
                const std::vector<uint32_t>* kinds[] = {&p.vehicles, &p.objects, &p.ai, &p.weapons};
                const std::vector<uint32_t>* filled[4];
                int count = 0;
                for (auto* k : kinds)
                    if (!k->empty()) filled[count++] = k;
                return count ? PickFrom(*filled[Next() % count]) : 0xFFFFFFFF;
            }
        }
    }

    // --- Recoil pushes (shot hook -> game tick) ---------------------------
    struct Push {
        uint32_t object;
        float velocity[3];
    };
    std::mutex g_pushLock;
    Push g_pushes[8];
    int g_pushCount = 0;

    void QueuePush(uint32_t object, const float* dir, float strength) {
        std::lock_guard<std::mutex> lock(g_pushLock);
        Push* push = nullptr;
        for (int i = 0; i < g_pushCount && !push; ++i)
            if (g_pushes[i].object == object) push = &g_pushes[i];
        if (!push && g_pushCount < (int)_countof(g_pushes)) {
            push = &g_pushes[g_pushCount++];
            *push = {object, {}};
        }
        if (push)
            for (int k = 0; k < 3; ++k) push->velocity[k] -= dir[k] * strength;
    }

    int TakePushes(Push* out, int max) {
        std::lock_guard<std::mutex> lock(g_pushLock);
        int count = g_pushCount < max ? g_pushCount : max;
        memcpy(out, g_pushes, count * sizeof(Push));
        g_pushCount = 0;
        return count;
    }

    // --- Game thread state ------------------------------------------------
    uint64_t g_skullsForced = 0;  // skulls the mod turned on (they were off)
    bool g_haveTeleportSerial = false;
    uint32_t g_teleportSerial = 0;
    uint32_t g_rain[kMaxRain];  // dropped objects, oldest first
    int g_rainCount = 0;
    float g_rainTimer = 0.0f;

    // Skulls the user ticked are kept on. Ones the mod turned on go off
    // again when unticked; ones MCC set are left alone.
    void Skulls(Fun::Title& t, uint64_t wanted) {
        uint64_t mask;
        if (!t.ReadSkulls(mask)) return;
        wanted &= t.SkullMask();
        uint64_t add = wanted & ~mask;
        uint64_t drop = g_skullsForced & ~wanted;
        uint64_t next = (mask | add) & ~drop;
        g_skullsForced = (g_skullsForced | add) & ~drop;
        if (next == mask) return;
        LOG("%s: skulls %016llX -> %016llX", t.Name(), (unsigned long long)mask, (unsigned long long)next);
        t.WriteSkulls(next);
    }

    // Player 1 (or the vehicle they're in) to where they aim, backed off
    // from the surface by its size.
    void Teleport(Fun::Title& t) {
        uint32_t player = t.Player();
        float eye[3], dir[3], at[3];
        if (player == 0xFFFFFFFF || !t.View(eye, dir)) return;
        float ray[3] = {dir[0] * kTeleportRange, dir[1] * kTeleportRange, dir[2] * kTeleportRange};
        if (!t.Ray(eye, ray, true, at)) {
            t.Message("Nothing in range to teleport to");
            return;
        }
        uint32_t vehicle = t.PlayerVehicle();
        uint32_t target = vehicle != 0xFFFFFFFF ? vehicle : player;
        float backOff = 0.6f, lift = 0.3f;
        if (vehicle != 0xFFFFFFFF) {
            float radius = t.Radius(vehicle);
            backOff = radius > 0.0f && radius < 100.0f ? radius : 2.0f;
            lift = 0.5f;
        }
        float position[3];
        for (int k = 0; k < 3; ++k) position[k] = at[k] - dir[k] * backOff;
        position[2] = (position[2] > at[2] ? position[2] : at[2]) + lift;
        t.Teleport(target, position);
        LOG("%s: teleported %08X to %.2f %.2f %.2f", t.Name(), target, position[0], position[1], position[2]);
    }

    // Every few seconds something from the spawn lists is made under the
    // sky (or a ceiling) somewhere around player 1. The oldest drop goes once
    // there are too many, unless a player is using it.
    void Rain(Fun::Title& t, const Games::Settings& s, float dt) {
        int max = (int)(s.rainMax + 0.5f);
        max = max < 1 ? 1 : (max > kMaxRain ? kMaxRain : max);
        float scratch[3];
        while (g_rainCount > 0 && (g_rainCount > max || !t.Position(g_rain[0], scratch))) {
            uint32_t oldest = g_rain[0];
            if (t.Position(oldest, scratch) && !t.InUseByPlayer(oldest)) t.Delete(oldest);
            memmove(g_rain, g_rain + 1, (--g_rainCount) * sizeof(uint32_t));
        }
        uint32_t player = t.Player();
        if (!s.rain || player == 0xFFFFFFFF) {
            g_rainTimer = 0.0f;
            return;
        }
        g_rainTimer += dt;
        if (g_rainTimer < s.rainInterval) return;
        g_rainTimer = 0.0f;

        uint32_t pick = RainPick(s.rainKind, s.spawnTag);
        if (pick == 0xFFFFFFFF) return;

        // A random spot around the player, short of any wall in between.
        uint32_t vehicle = t.PlayerVehicle();
        float at[3];
        if (!t.Position(vehicle != 0xFFFFFFFF ? vehicle : player, at)) return;
        float radius = s.rainRadius / Fun::kMetresPerUnit;
        float angle = Random01() * 2.0f * kPi;
        float distance = radius * (kRainMinFraction + Random01() * (1.0f - kRainMinFraction));
        float from[3] = {at[0], at[1], at[2] + 0.5f};
        float out[3] = {cosf(angle) * distance, sinf(angle) * distance, 0.0f};
        float spot[3];
        if (t.Ray(from, out, true, spot)) {
            for (int k = 0; k < 2; ++k) spot[k] -= out[k] / distance * 1.5f;
        } else {
            for (int k = 0; k < 3; ++k) spot[k] = from[k] + out[k];
        }
        // Then up to the sky, or just under a ceiling.
        float up[3] = {0.0f, 0.0f, kRainHeight};
        float ceiling[3];
        float drop[3] = {spot[0], spot[1], spot[2] + kRainHeight};
        if (t.Ray(spot, up, false, ceiling)) drop[2] = ceiling[2] - 1.5f > spot[2] ? ceiling[2] - 1.5f : spot[2];

        uint32_t made = t.Create(pick, s.spawnTeam, drop, Random01() * 2.0f * kPi);
        if (made == 0xFFFFFFFF) {
            t.PrepareFor(pick);  // this drop is skipped
            return;
        }
        // New objects can start with their physics asleep and hang in the
        // air; a small push wakes them so they fall.
        const float down[3] = {0.0f, 0.0f, -1.0f};
        t.AddVelocity(made, down);
        if (g_rainCount < kMaxRain) g_rain[g_rainCount++] = made;
        static ULONGLONG s_lastLog = 0;
        if (GetTickCount64() - s_lastLog > 5000) {
            s_lastLog = GetTickCount64();
            LOG("%s: rain: %08X at %.1f %.1f %.1f (%d dropped)", t.Name(), pick, drop[0], drop[1], drop[2], g_rainCount);
        }
    }

    // Recoil push: the shoves queued by the last tick's shots.
    void ApplyPushes(Fun::Title& t) {
        Push pushes[8];
        int count = TakePushes(pushes, _countof(pushes));
        for (int i = 0; i < count; ++i) t.AddVelocity(pushes[i].object, pushes[i].velocity);
    }

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }
}

void Fun::Tick(Title& t, const Games::Settings& s, float dt) {
    if (!g_haveTeleportSerial) {  // don't run a key press from before this session
        g_teleportSerial = s.teleportSerial;
        g_haveTeleportSerial = true;
    }
    Guard("skulls", [&] { Skulls(t, s.skulls); });
    if (s.teleportSerial != g_teleportSerial) {
        g_teleportSerial = s.teleportSerial;
        if (s.teleportEnabled) Guard("teleport", [&] { Teleport(t); });
    }
    Guard("rain", [&] { Rain(t, s, dt); });
    Guard("recoil push", [&] { ApplyPushes(t); });
}

void Fun::ResetState() {
    g_skullsForced = 0;
    g_rainCount = 0;
    g_rainTimer = 0.0f;
    std::lock_guard<std::mutex> lock(g_pushLock);
    g_pushCount = 0;
}

void Fun::Reset() {
    ResetState();
    std::lock_guard<std::mutex> lock(g_poolsLock);
    g_pools = {};
}

void Fun::SetPools(const std::vector<Games::TagChoice>* vehicles, const std::vector<Games::TagChoice>* objects,
                   const std::vector<Games::TagChoice>* ai, const std::vector<Games::TagChoice>* weapons,
                   const std::vector<Games::TagChoice>* projectiles, bool (*canFall)(uint32_t tag)) {
    auto datums = [](const std::vector<Games::TagChoice>* list, bool (*keep)(uint32_t)) {
        std::vector<uint32_t> out;
        if (list)
            for (const auto& c : *list)
                if (!keep || keep(c.datum)) out.push_back(c.datum);
        return out;
    };
    Pools next{datums(vehicles, nullptr), datums(objects, canFall), datums(ai, nullptr), datums(weapons, nullptr),
               datums(projectiles, nullptr)};
    std::lock_guard<std::mutex> lock(g_poolsLock);
    g_pools = std::move(next);
}

void Fun::SetSettings(const Games::Settings& s) {
    g_chaosGun = s.chaosGun;
    g_push = s.recoilPush ? s.recoilStrength : 0.0f;
}

bool Fun::Active() { return g_chaosGun || g_push > 0.0f; }

uint32_t Fun::ChaosProjectile() {
    if (!g_chaosGun) return 0xFFFFFFFF;
    std::lock_guard<std::mutex> lock(g_poolsLock);
    return PickFrom(g_pools.projectiles);
}

void Fun::BeforeShot(uint32_t* const* projectiles, int count, uint32_t pushTarget, const float* dir, Shot& shot) {
    uint32_t pick = count > 0 ? ChaosProjectile() : 0xFFFFFFFF;
    for (int i = 0; pick != 0xFFFFFFFF && i < count && shot.count < Shot::kMax; ++i) {
        uint32_t* field = projectiles[i];
        if (!field || *field == 0xFFFFFFFF) continue;  // unused (no secondary, no crate projectile)
        shot.fields[shot.count] = field;
        shot.saved[shot.count++] = *field;
        *field = pick;
    }
    float push = g_push;
    if (push > 0.0f && pushTarget != 0xFFFFFFFF && dir) QueuePush(pushTarget, dir, push);
}

void Fun::AfterShot(const Shot& shot) {
    for (int i = shot.count - 1; i >= 0; --i) *shot.fields[i] = shot.saved[i];
}
