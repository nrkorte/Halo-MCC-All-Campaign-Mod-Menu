#pragma once

#include <cstdint>
#include <vector>

#include "game.h"

// The Fun, Chaos and Skulls pages (recoil push, teleport key, chaos gun, rain,
// skulls) for every title but Reach, which has its own older copy in reach/.
// This code decides what happens when; each title supplies a Fun::Title that
// does the engine work for its game.
namespace Fun {
    // Engine work a title supplies. Called on its game thread, where engine
    // functions are safe to call.
    class Title {
    public:
        virtual ~Title() = default;
        virtual const char* Name() = 0;  // for the log

        virtual uint32_t Player() = 0;         // player 1's unit, or -1
        virtual uint32_t PlayerVehicle() = 0;  // the top vehicle player 1 is in, or -1
        // Player 1's eye and look direction (a unit vector).
        virtual bool View(float eye[3], float dir[3]) = 0;
        // The engine's ray from `from` along `vec` (its length is the range),
        // ignoring player 1 and their vehicle if `ignorePlayer`. Writes the
        // hit point.
        virtual bool Ray(const float* from, const float* vec, bool ignorePlayer, float* hit) = 0;
        virtual bool Position(uint32_t object, float* out) = 0;  // false if it's gone
        virtual float Radius(uint32_t object) = 0;                // bounding radius, 0 if unknown
        // Moves an object and stops it.
        virtual void Teleport(uint32_t object, const float* position) = 0;
        virtual void AddVelocity(uint32_t object, const float* add) = 0;
        // Makes a spawn selection; returns the object made (an AI's unit), or -1.
        virtual uint32_t Create(uint32_t selection, int team, const float* position, float yaw) = 0;
        // After a Create failed: get what it needs loaded for next time (a
        // designer zone). Optional.
        virtual void PrepareFor(uint32_t) {}
        virtual bool InUseByPlayer(uint32_t object) = 0;  // held or ridden by a player
        virtual void Delete(uint32_t object) = 0;
        virtual void Message(const char* text) = 0;  // a toast (the spawn result)
        // Skulls: the active mask (bit = the title's skull number). False if
        // this title's skulls aren't known.
        virtual bool ReadSkulls(uint64_t& mask) = 0;
        virtual void WriteSkulls(uint64_t mask) = 0;
        virtual uint64_t SkullMask() = 0;  // the bits that are skulls
    };

    // Game thread, once per tick: skulls, the teleport key, rain, and the
    // recoil pushes queued by shots.
    void Tick(Title& title, const Games::Settings& s, float dt);
    void Reset();  // unloading: forget what was dropped and the pools
    void ResetState();  // a new map: forget what was dropped (the render thread has already set this map's pools)

    // Render thread: this map's spawn lists (rain picks from them, the
    // chaos gun from the projectiles) and the settings the shot hook reads.
    // `canFall` leaves objects out of rain (scenery and machines have no
    // physics and would hang in the air). Call when a map's lists change.
    void SetPools(const std::vector<Games::TagChoice>* vehicles, const std::vector<Games::TagChoice>* objects,
                  const std::vector<Games::TagChoice>* ai, const std::vector<Games::TagChoice>* weapons,
                  const std::vector<Games::TagChoice>* projectiles, bool (*canFall)(uint32_t tag) = nullptr);
    void SetSettings(const Games::Settings& s);

    // A title's per-shot hook, once it has found that the shot is a player's:
    // `projectiles` are the projectile tag reference datums the shot may read
    // (the barrel's own, its secondary, ...; all get the same random pick),
    // `pushTarget` what recoil push moves (the shooter, or the vehicle they're
    // in; -1 for none), `dir` their aim.
    struct Shot {
        static constexpr int kMax = 6;
        uint32_t* fields[kMax] = {};  // put back after the shot
        uint32_t saved[kMax] = {};
        int count = 0;
    };
    bool Active();  // chaos gun or recoil push on (skip the work otherwise)
    void BeforeShot(uint32_t* const* projectiles, int count, uint32_t pushTarget, const float* dir, Shot& shot);
    void AfterShot(const Shot& shot);
    // For a title whose projectile comes from a lookup instead (Halo 4):
    // a random projectile while the chaos gun is on, else -1.
    uint32_t ChaosProjectile();

    constexpr float kMetresPerUnit = 3.048f;  // a world unit is 10 feet
}
