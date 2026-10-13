// Halo 4: Lock checkpoints and checkpoint history (Game State page). The
// structures and how they were found are in checkpoints_offsets.h; the design
// follows Reach's (reach/main_thread.cpp) and Halo 3's (halo3/checkpoints.cpp):
//  - Lock: a hook on the snapshot function 0x48DC4 skips only the main loop's
//    save step (the caller at 0x60A16). The save request (0x194DBC) and the
//    save processor still run, so game_saving, the cinematic-skip save and
//    everything else that waits on a save completes normally; only the copy
//    into the slot is dropped. (Vetoing the request itself broke the cutscene
//    skip in Rally Point Bravo.) The mod's own Save button opens a window that
//    lets one snapshot through. While locked, the state the snapshot would have
//    copied is kept in the history instead, so history still records.
//  - History: every ~30 ticks the current slot is hashed; a new, stable
//    snapshot is copied out. Going back writes it into the current slot and
//    requests a revert; the header fields the game compares are first copied
//    into the live state, because the revert checks the slot against it.
#include "internal.h"

#include <windows.h>
#include <intrin.h>

#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "checkpoints_offsets.h"
#include "core/log.h"
#include "offsets.h"

namespace {
    constexpr size_t kMaxCheckpoints = 8;
    constexpr size_t kHashHeaderBytes = HALO4_CHECKPOINT_HEADER_SIZE;  // the header differs between saves
    constexpr int kPollTicks = 30;                                     // twice a second
    constexpr ULONGLONG kOwnSaveWindowMs = 30000;                      // a paused game holds the save until it ticks again

    char* g_module = nullptr;

    // Read by the hook (game thread), written by Tick/Apply and the Save button.
    volatile bool g_lock = false;
    volatile ULONGLONG g_ownSaveUntil = 0;  // the Save button's window: a snapshot may pass until this tick count (ms)
    volatile int g_vetoLogged = 0;
    volatile bool g_armed = false;  // the map's first snapshot has been taken: only then can the lock veto

    volatile ULONGLONG g_cinematicSaveUntil = 0;  // a cinematic-skip save (priority 4) was requested: its snapshot must pass
    void* g_snapshotHookTarget = nullptr;
    void* g_requestHookTarget = nullptr;

    bool OwnWindowOpen() {
        ULONGLONG until = g_ownSaveUntil;
        return until != 0 && GetTickCount64() < until;
    }

    // --- History ------------------------------------------------------------

    struct Stored {
        uint32_t id = 0;
        int64_t when = 0;
        uint64_t hash = 0;
        std::string map;
        std::vector<uint8_t> data;  // the snapshot
    };
    std::vector<Stored> g_checkpoints;
    std::mutex g_lockMutex;  // g_checkpoints is read by Apply (render thread)
    uint32_t g_nextId = 1;
    uint64_t g_lastHash = 0;
    uint64_t g_pendingHash = 0;
    int g_stable = 0;
    int g_poll = 0;
    uint32_t g_restoreSerial = 0;
    bool g_serialsInit = false;
    std::vector<Games::CheckpointEntry> g_status;  // render thread only

    // The current slot's snapshot and size, or false. Plain memory reads in SEH.
    bool CurrentSlot(uint8_t** snapshot, size_t* size) {
        __try {
            if (!g_module) return false;
            if (*(int32_t*)(g_module + OFFSET_HALO4_CHECKPOINT_COUNT) != 2) return false;
            int32_t current = *(int32_t*)(g_module + OFFSET_HALO4_CHECKPOINT_CURRENT);
            if (current < 0 || current > 1) return false;
            int32_t length = *(int32_t*)(g_module + OFFSET_HALO4_CHECKPOINT_SIZE);
            if (length < 0x100000 || length > 0x4000000 || (size_t)length < kHashHeaderBytes) return false;
            char* slots = g_module + OFFSET_HALO4_CHECKPOINT_SLOTS;
            char* mine = slots + current * HALO4_CHECKPOINT_SLOT_STRIDE;
            char* other = slots + (1 - current) * HALO4_CHECKPOINT_SLOT_STRIDE;
            if (!*(uint8_t*)(mine + HALO4_CHECKPOINT_SLOT_VALID)) return false;
            uint8_t* mineData = *(uint8_t**)mine;
            uint8_t* otherData = *(uint8_t**)other;
            if (!mineData || !otherData) return false;
            // The two snapshots are the same size and sit one after the other.
            size_t span = (size_t)(mineData > otherData ? mineData - otherData : otherData - mineData);
            if (span < (size_t)length) return false;
            if (*(uint32_t*)mineData == 0) return false;  // never written
            *snapshot = mineData;
            *size = (size_t)length;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // memcpy that survives the game freeing the buffer under it.
    bool SafeCopy(void* to, const void* from, size_t size) {
        __try {
            memcpy(to, from, size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // FNV-1a over the header plus a sample every 64 KB, so a change anywhere shows.
    bool Hash(const uint8_t* snapshot, size_t size, uint64_t* out) {
        __try {
            uint64_t hash = 1469598103934665603ull;
            for (size_t i = 0; i + 8 <= kHashHeaderBytes; i += 8) {
                hash ^= *(const uint64_t*)(snapshot + i);
                hash *= 1099511628211ull;
            }
            for (size_t at = kHashHeaderBytes; at + 64 <= size; at += 0x10000)
                for (size_t i = 0; i < 64; i += 8) {
                    hash ^= *(const uint64_t*)(snapshot + at + i);
                    hash *= 1099511628211ull;
                }
            *out = hash;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    std::string MapOf(const uint8_t* snapshot) {
        char name[96] = {};
        SafeCopy(name, snapshot + HALO4_CHECKPOINT_MAP_NAME, sizeof(name) - 1);
        return name;
    }

    // The game's revert (0x49954) compares the slot's header with the live state's: map path,
    // [0x128, 0x138) and [0x2D068, 0x2D088). The live header holds the LAST save's values, so an
    // older checkpoint would be refused; copy its values over (the revert then replaces the
    // whole live state with the slot anyway). False if the live state can't be reached.
    bool SyncLiveHeader(const uint8_t* data) {
        __try {
            uint8_t* live = *(uint8_t**)(g_module + OFFSET_HALO4_CHECKPOINT_LIVE);
            if (!live) return false;
            if (strncmp((const char*)live + HALO4_CHECKPOINT_MAP_NAME, (const char*)data + HALO4_CHECKPOINT_MAP_NAME, 0x100) != 0) return false;
            memcpy(live + HALO4_CHECKPOINT_CHECK_A_BEGIN, data + HALO4_CHECKPOINT_CHECK_A_BEGIN,
                   HALO4_CHECKPOINT_CHECK_A_END - HALO4_CHECKPOINT_CHECK_A_BEGIN);
            memcpy(live + HALO4_CHECKPOINT_CHECK_B_BEGIN, data + HALO4_CHECKPOINT_CHECK_B_BEGIN,
                   HALO4_CHECKPOINT_CHECK_B_END - HALO4_CHECKPOINT_CHECK_B_BEGIN);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Adds a copy to the history.
    void Keep(Stored&& saved) {
        std::lock_guard<std::mutex> lock(g_lockMutex);
        if (!g_checkpoints.empty() && g_checkpoints.back().map != saved.map) {
            LOG("Halo 4: checkpoint history cleared (new mission %s)", saved.map.c_str());
            g_checkpoints.clear();
        }
        saved.id = g_nextId++;
        LOG("Halo 4: checkpoint %u kept (%s, %zu in history)", saved.id, saved.map.c_str(), g_checkpoints.size() + 1);
        g_checkpoints.push_back(std::move(saved));
        if (g_checkpoints.size() > kMaxCheckpoints) g_checkpoints.erase(g_checkpoints.begin());
    }

    // A vetoed save (lock on): the live state is what the snapshot would have copied.
    bool LiveState(uint8_t** live, size_t* size) {
        __try {
            *live = *(uint8_t**)(g_module + OFFSET_HALO4_CHECKPOINT_LIVE);
            int32_t length = *(int32_t*)(g_module + OFFSET_HALO4_CHECKPOINT_SIZE);
            if (!*live || length < 0x100000 || length > 0x4000000 || (size_t)length < kHashHeaderBytes) return false;
            *size = (size_t)length;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void KeepLive() {
        uint8_t* live = nullptr;
        size_t size = 0;
        if (!LiveState(&live, &size)) return;
        Stored saved;
        saved.when = (int64_t)time(nullptr);
        saved.map = MapOf(live);
        if (saved.map.empty()) return;
        saved.data.resize(size);
        if (!SafeCopy(saved.data.data(), live, size)) return;
        Hash(saved.data.data(), size, &saved.hash);
        Keep(std::move(saved));
    }

    // --- Lock ---------------------------------------------------------------

    // The save request 0x194DBC(priority), always passed on; it only notes the cinematic-skip
    // save (game_save_cinematic_skip = priority 4), whose snapshot the lock must let through:
    // skipping a cutscene saves and then reloads that checkpoint, so a dropped snapshot sends
    // the game back to the old one (the start of the mission, with the cutscene).
    using RequestFn = void(__fastcall*)(int32_t);
    RequestFn g_origRequest = nullptr;

    void __fastcall RequestDetour(int32_t priority) {
        if (priority >= 4) g_cinematicSaveUntil = GetTickCount64() + 15000;
        g_origRequest(priority);
    }

    // Engine query 0x12EF68: a cinematic is playing. While one is, the save processor only
    // accepts the skip save and drops every other request.
    // Re-evaluated on every call; remembers when one was last seen so the save that ends or skips
    // it (which can land a tick after the cinematic flag drops) still counts.
    volatile ULONGLONG g_cinematicSeen = 0;
    bool CinematicPlaying() {
        __try {
            bool playing = g_module && ((bool(__fastcall*)())(g_module + OFFSET_HALO4_CINEMATIC_PLAYING))();
            if (playing) g_cinematicSeen = GetTickCount64();
            ULONGLONG seen = g_cinematicSeen;
            return playing || (seen != 0 && GetTickCount64() < seen + 8000);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    using SnapshotFn = void(__fastcall*)();
    SnapshotFn g_origSnapshot = nullptr;

    void __fastcall SnapshotDetour() {
        // Only the main loop's save step is the game's own checkpoint save; the other callers
        // (map start, 0x49543, 0x9F89B) must go through or a new map has no first checkpoint.
        if (g_module && (char*)_ReturnAddress() == g_module + OFFSET_HALO4_CHECKPOINT_SNAPSHOT_RETURN) {
            ULONGLONG cinema = g_cinematicSaveUntil;
            bool cinematicSave = (cinema != 0 && GetTickCount64() < cinema) || CinematicPlaying();
            if (!g_armed) {
                // The first main-loop save of a map is never vetoed (and a cutscene mission
                // can still be in its opening); the lock arms after it.
                g_armed = true;
                LOG("Halo 4: checkpoint lock armed (first snapshot of the map)");
            } else if (g_lock && !OwnWindowOpen() && !cinematicSave) {
                if (g_vetoLogged < 5) {
                    g_vetoLogged = g_vetoLogged + 1;
                    LOG("Halo 4: checkpoint save skipped (locked); the state is kept in the history instead");
                }
                KeepLive();
                return;
            }
            g_ownSaveUntil = 0;  // the Save button's own checkpoint (or not locked): the window is used up
            if (g_lock && cinematicSave && g_vetoLogged < 10) {
                g_vetoLogged = 10;
                LOG("Halo 4: checkpoint save during a cinematic skip let through (locked)");
            }
            g_cinematicSaveUntil = 0;
        }
        g_origSnapshot();
    }

    // A new snapshot in the current slot that has stayed the same for two looks (a save
    // in progress isn't copied half done) is kept.
    void PollCheckpoints() {
        if (++g_poll < kPollTicks) return;
        g_poll = 0;
        uint8_t* snapshot = nullptr;
        size_t size = 0;
        if (!CurrentSlot(&snapshot, &size)) return;
        uint64_t hash = 0;
        if (!Hash(snapshot, size, &hash)) return;
        if (hash == g_lastHash) { g_stable = 0; return; }
        if (hash != g_pendingHash) {
            g_pendingHash = hash;
            g_stable = 1;
            return;
        }
        if (++g_stable < 2) return;

        Stored saved;
        saved.hash = hash;
        saved.when = (int64_t)time(nullptr);
        saved.map = MapOf(snapshot);
        if (saved.map.empty()) return;
        saved.data.resize(size);
        if (!SafeCopy(saved.data.data(), snapshot, size)) {
            LOG("Halo 4: checkpoint history: reading the snapshot faulted");
            return;
        }
        g_lastHash = hash;
        g_stable = 0;
        Keep(std::move(saved));
    }

    // Goes back to a kept checkpoint: its snapshot becomes the current slot's, then the
    // game is asked to revert.
    void RestoreCheckpoint(uint32_t id) {
        uint8_t* snapshot = nullptr;
        size_t size = 0;
        bool have = CurrentSlot(&snapshot, &size);
        std::lock_guard<std::mutex> lock(g_lockMutex);
        for (const Stored& saved : g_checkpoints) {
            if (saved.id != id) continue;
            if (!have || saved.data.size() != size) {
                LOG("Halo 4: restore checkpoint %u: no matching slot (size %zu vs %zu)", id, saved.data.size(), have ? size : 0);
                return;
            }
            // A different map's snapshot would crash the game: the slot must still be this mission's.
            if (MapOf(snapshot) != saved.map) {
                LOG("Halo 4: restore checkpoint %u: the current slot is another map (%s vs %s)", id, MapOf(snapshot).c_str(), saved.map.c_str());
                return;
            }
            if (!SyncLiveHeader(saved.data.data())) {
                LOG("Halo 4: restore checkpoint %u: the live state's header couldn't be matched", id);
                return;
            }
            if (!SafeCopy(snapshot, saved.data.data(), size)) {
                LOG("Halo 4: restore checkpoint %u: writing the slot faulted", id);
                return;
            }
            g_lastHash = saved.hash;  // not a new save
            g_pendingHash = saved.hash;
            g_stable = 0;
            *(uint16_t*)(g_module + OFFSET_HALO4_GAME_REVERT_FLAG) = HALO4_GAME_REVERT_REQUEST;
            LOG("Halo 4: going back to checkpoint %u", id);
            return;
        }
        LOG("Halo 4: restore checkpoint %u: not in the history", id);
    }

    void DropHistory() {
        std::lock_guard<std::mutex> lock(g_lockMutex);
        g_checkpoints.clear();
        g_checkpoints.shrink_to_fit();
    }

    void TickInner(const Games::Settings& s) {
        g_lock = s.lockCheckpoints;
        if (g_lock) CinematicPlaying();  // keeps the "a cinematic just ran" stamp current
        if (!g_serialsInit) {
            g_serialsInit = true;
            g_restoreSerial = s.restoreCheckpointSerial;
        }
        PollCheckpoints();
        if (s.restoreCheckpointSerial != g_restoreSerial) {
            g_restoreSerial = s.restoreCheckpointSerial;
            RestoreCheckpoint(s.restoreCheckpoint);
        }
    }
}

namespace Halo4Checkpoints {
    void Load(char* module) {
        g_module = module;
        __try {
            void* at = module + OFFSET_HALO4_CHECKPOINT_SNAPSHOT;
            if (MH_CreateHook(at, (void*)&SnapshotDetour, (void**)&g_origSnapshot) == MH_OK && MH_EnableHook(at) == MH_OK) {
                g_snapshotHookTarget = at;
            } else {
                MH_RemoveHook(at);
                LOG("Halo 4: failed to hook the checkpoint snapshot at %p (lock checkpoints unavailable)", at);
            }
            void* req = module + OFFSET_HALO4_GAME_SAVE;
            if (MH_CreateHook(req, (void*)&RequestDetour, (void**)&g_origRequest) == MH_OK && MH_EnableHook(req) == MH_OK) {
                g_requestHookTarget = req;
            } else {
                MH_RemoveHook(req);
                LOG("Halo 4: failed to hook the save request at %p (cutscene skips may not save while locked)", req);
            }
        } __except (Log::Exception(GetExceptionInformation(), "Halo 4 checkpoint hook")) {
        }
    }

    void Unload() {
        g_lock = false;
        __try {
            if (g_snapshotHookTarget) {
                MH_DisableHook(g_snapshotHookTarget);
                MH_RemoveHook(g_snapshotHookTarget);
                g_snapshotHookTarget = nullptr;
            }
            if (g_requestHookTarget) {
                MH_DisableHook(g_requestHookTarget);
                MH_RemoveHook(g_requestHookTarget);
                g_requestHookTarget = nullptr;
            }
        } __except (Log::Exception(GetExceptionInformation(), "Halo 4 checkpoint unhook")) {
        }
        g_ownSaveUntil = 0;
        g_module = nullptr;
        DropHistory();
    }

    // Map change: the history belongs to the old mission.
    void Reset() {
        g_armed = false;
        g_cinematicSeen = 0;
        g_cinematicSaveUntil = 0;
        std::lock_guard<std::mutex> lock(g_lockMutex);
        g_checkpoints.clear();
        g_lastHash = 0;
        g_pendingHash = 0;
        g_stable = 0;
        g_poll = 0;
    }

    void Tick(void**, const Games::Settings& s, const Halo4Cheats::Players&, const Halo4Cheats::Array&) {
        __try {
            TickInner(s);
        } __except (Log::Exception(GetExceptionInformation(), "Halo 4 checkpoint history")) {
        }
    }

    void Apply(const Games::Settings& s, Games::Status& status) {
        g_lock = s.lockCheckpoints;
        g_status.clear();
        {
            std::lock_guard<std::mutex> lock(g_lockMutex);
            for (const Stored& saved : g_checkpoints) {
                Games::CheckpointEntry entry;
                entry.id = saved.id;
                entry.when = saved.when;
                time_t t = (time_t)saved.when;
                tm local{};
                localtime_s(&local, &t);
                char text[16];
                strftime(text, sizeof(text), "%H:%M:%S", &local);
                entry.label = text;
                g_status.push_back(std::move(entry));
            }
        }
        status.checkpoints = &g_status;
    }

    // The Save button's request is only processed a tick or two later (the save processor, then
    // the main loop), so the window stays open until the snapshot comes through (or 30 s).
    void BeginOwnSave() { g_ownSaveUntil = GetTickCount64() + kOwnSaveWindowMs; }
    void EndOwnSave() {}
}
