// ODST: Lock checkpoints and checkpoint history (Game State page). The
// structures and how they were found are in checkpoints_offsets.h; the design
// follows Reach's (reach/main_thread.cpp) and Halo 2's lock (halo2/extras.cpp):
//  - Lock: a hook on the snapshot function 0xAABA4 skips only the main loop's
//    save step (the request state machine still completes, nothing waits on a
//    save that never happens), and a hook on the HUD message hides the
//    "Checkpoint..." text. The mod's own Save button opens a window that lets
//    one snapshot (and its messages) through.
//  - History: every ~30 ticks the current slot is hashed; a new, stable
//    snapshot is copied out (with its header blob). Going back writes both
//    into the current slot and requests a revert.
#include "internal.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
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
    constexpr size_t kHashHeaderBytes = 0x10000;   // the header and its signature differ between saves
    constexpr int kPollTicks = 30;                 // twice a second
    constexpr ULONGLONG kOwnSaveWindowMs = 30000;  // a paused game holds the save until it ticks again

    char* g_module = nullptr;

    // Read by the hooks (game thread), written by Tick/Apply.
    volatile bool g_lock = false;
    volatile ULONGLONG g_ownSaveUntil = 0;  // the Save button's window: a snapshot may pass until this tick count (ms)
    volatile int g_vetoLogged = 0;

    struct Hook {
        void* target = nullptr;
        bool Install(int rva, void* detour, void** original, const char* what) {
            void* at = g_module + rva;
            if (MH_CreateHook(at, detour, original) == MH_OK && MH_EnableHook(at) == MH_OK) {
                target = at;
                return true;
            }
            MH_RemoveHook(at);
            LOG("ODST: failed to hook %s at %p", what, at);
            return false;
        }
        void Remove() {
            if (!target) return;
            MH_DisableHook(target);
            MH_RemoveHook(target);
            target = nullptr;
        }
    };
    Hook g_snapshotHook, g_messageHook;

    bool OwnWindowOpen() {
        ULONGLONG until = g_ownSaveUntil;
        return until != 0 && GetTickCount64() < until;
    }

    // --- Lock ---------------------------------------------------------------

    using SnapshotFn = void(__fastcall*)();
    SnapshotFn g_origSnapshot = nullptr;

    void __fastcall SnapshotDetour() {
        // Only the main loop's save step (the 0xBFAE4 call) is the game's own checkpoint save;
        // the map-start callers must go through or a new map has no first checkpoint.
        if (g_lock && g_module && (char*)_ReturnAddress() == g_module + OFFSET_ODST_CHECKPOINT_SNAPSHOT_RETURN) {
            if (OwnWindowOpen()) {
                g_ownSaveUntil = 0;  // the Save button's own checkpoint: once
            } else {
                if (g_vetoLogged < 5) {
                    g_vetoLogged = g_vetoLogged + 1;
                    LOG("ODST: checkpoint save skipped (locked)");
                }
                return;
            }
        } else if (g_module && (char*)_ReturnAddress() == g_module + OFFSET_ODST_CHECKPOINT_SNAPSHOT_RETURN) {
            g_ownSaveUntil = 0;  // not locked: whatever saved, the window is used up
        }
        g_origSnapshot();
    }

    // The "Checkpoint..." (started) and "Checkpoint...done" HUD messages; both are posted while the
    // window is open for the mod's own save.
    using MessageFn = void(__fastcall*)(uint8_t started);
    MessageFn g_origMessage = nullptr;

    void __fastcall MessageDetour(uint8_t started) {
        if (g_lock && !OwnWindowOpen()) return;
        g_origMessage(started);
    }

    // --- History ------------------------------------------------------------

    struct Stored {
        uint32_t id = 0;
        int64_t when = 0;
        uint64_t hash = 0;
        std::string map;
        std::vector<uint8_t> data;  // the snapshot, then its header blob
        size_t snapshotSize = 0;
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

    struct Current {
        uint8_t* snapshot = nullptr;
        uint8_t* blob = nullptr;
        size_t size = 0;
    };

    // The current slot's snapshot, header blob and size, or false. Plain memory reads in SEH.
    bool CurrentSlot(Current* out) {
        __try {
            if (!g_module) return false;
            if (*(int32_t*)(g_module + OFFSET_ODST_CHECKPOINT_COUNT) != 2) return false;
            if (*(int32_t*)(g_module + OFFSET_ODST_CHECKPOINT_MODE) != ODST_CHECKPOINT_MODE_COPY) return false;
            int32_t current = *(int32_t*)(g_module + OFFSET_ODST_CHECKPOINT_CURRENT);
            if (current < 0 || current > 1) return false;
            int32_t size = *(int32_t*)(g_module + OFFSET_ODST_CHECKPOINT_SIZE);
            if (size < 0x100000 || size > 0x4000000 || (size_t)size < kHashHeaderBytes) return false;
            char* slots = g_module + OFFSET_ODST_CHECKPOINT_SLOTS;
            char* mine = slots + current * ODST_CHECKPOINT_SLOT_STRIDE;
            char* other = slots + (1 - current) * ODST_CHECKPOINT_SLOT_STRIDE;
            if (!*(uint8_t*)(mine + ODST_CHECKPOINT_SLOT_VALID)) return false;
            uint8_t* snapshot = *(uint8_t**)mine;
            uint8_t* neighbour = *(uint8_t**)other;
            if (!snapshot || !neighbour) return false;
            // The two snapshots are the same size and sit one after the other.
            size_t span = (size_t)(snapshot > neighbour ? snapshot - neighbour : neighbour - snapshot);
            if (span < (size_t)size) return false;
            if (*(uint32_t*)snapshot == 0) return false;  // never written
            out->snapshot = snapshot;
            out->blob = (uint8_t*)(g_module + OFFSET_ODST_CHECKPOINT_BLOBS) + (size_t)current * ODST_CHECKPOINT_BLOB_SIZE;
            out->size = (size_t)size;
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
        SafeCopy(name, snapshot + ODST_CHECKPOINT_MAP_NAME, sizeof(name) - 1);
        return name;
    }

    // A new snapshot in the current slot that has stayed the same for two looks (a save
    // in progress isn't copied half done) is kept.
    void PollCheckpoints() {
        if (++g_poll < kPollTicks) return;
        g_poll = 0;
        Current slot;
        if (!CurrentSlot(&slot)) return;
        uint64_t hash = 0;
        if (!Hash(slot.snapshot, slot.size, &hash)) return;
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
        saved.map = MapOf(slot.snapshot);
        if (saved.map.empty()) return;
        saved.snapshotSize = slot.size;
        saved.data.resize(slot.size + ODST_CHECKPOINT_BLOB_SIZE);
        if (!SafeCopy(saved.data.data(), slot.snapshot, slot.size) ||
            !SafeCopy(saved.data.data() + slot.size, slot.blob, ODST_CHECKPOINT_BLOB_SIZE)) {
            LOG("ODST: checkpoint history: reading the snapshot faulted");
            return;
        }
        g_lastHash = hash;
        g_stable = 0;

        std::lock_guard<std::mutex> lock(g_lockMutex);
        if (!g_checkpoints.empty() && g_checkpoints.back().map != saved.map) {
            LOG("ODST: checkpoint history cleared (new mission %s)", saved.map.c_str());
            g_checkpoints.clear();
        }
        saved.id = g_nextId++;
        LOG("ODST: checkpoint %u kept (%s, %zu in history)", saved.id, saved.map.c_str(), g_checkpoints.size() + 1);
        g_checkpoints.push_back(std::move(saved));
        if (g_checkpoints.size() > kMaxCheckpoints) g_checkpoints.erase(g_checkpoints.begin());
    }

    // Goes back to a kept checkpoint: its snapshot and header blob become the current
    // slot's, then the game is asked to revert.
    void RestoreCheckpoint(uint32_t id) {
        Current slot;
        bool have = CurrentSlot(&slot);
        std::lock_guard<std::mutex> lock(g_lockMutex);
        for (const Stored& saved : g_checkpoints) {
            if (saved.id != id) continue;
            if (!have || saved.snapshotSize != slot.size) {
                LOG("ODST: restore checkpoint %u: no matching slot (size %zu vs %zu)", id, saved.snapshotSize, have ? slot.size : 0);
                return;
            }
            // A different map's snapshot would crash the game: the slot must still be this mission's.
            if (MapOf(slot.snapshot) != saved.map) {
                LOG("ODST: restore checkpoint %u: the current slot is another map (%s vs %s)", id, MapOf(slot.snapshot).c_str(), saved.map.c_str());
                return;
            }
            if (!SafeCopy(slot.snapshot, saved.data.data(), slot.size) ||
                !SafeCopy(slot.blob, saved.data.data() + slot.size, ODST_CHECKPOINT_BLOB_SIZE)) {
                LOG("ODST: restore checkpoint %u: writing the slot faulted", id);
                return;
            }
            g_lastHash = saved.hash;  // not a new save
            g_pendingHash = saved.hash;
            g_stable = 0;
            *(uint16_t*)(g_module + OFFSET_ODST_GAME_REVERT_FLAG) = ODST_GAME_REVERT_REQUEST;
            LOG("ODST: going back to checkpoint %u", id);
            return;
        }
        LOG("ODST: restore checkpoint %u: not in the history", id);
    }

    void DropHistory() {
        std::lock_guard<std::mutex> lock(g_lockMutex);
        g_checkpoints.clear();
        g_checkpoints.shrink_to_fit();
    }

    void TickInner(const Games::Settings& s) {
        g_lock = s.lockCheckpoints;
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

namespace OdstCheckpoints {
    void Load(char* module) {
        g_module = module;
        __try {
            g_snapshotHook.Install(OFFSET_ODST_CHECKPOINT_SNAPSHOT, (void*)&SnapshotDetour, (void**)&g_origSnapshot,
                                   "checkpoint snapshot (lock checkpoints unavailable)");
            g_messageHook.Install(OFFSET_ODST_CHECKPOINT_MESSAGE, (void*)&MessageDetour, (void**)&g_origMessage,
                                  "checkpoint message (it will still show while locked)");
        } __except (Log::Exception(GetExceptionInformation(), "ODST checkpoint hooks")) {
        }
    }

    void Unload() {
        g_lock = false;
        __try {
            g_snapshotHook.Remove();
            g_messageHook.Remove();
        } __except (Log::Exception(GetExceptionInformation(), "ODST checkpoint unhook")) {
        }
        g_ownSaveUntil = 0;
        g_module = nullptr;
        DropHistory();
    }

    // Map change: the history belongs to the old mission.
    void Reset() {
        std::lock_guard<std::mutex> lock(g_lockMutex);
        g_checkpoints.clear();
        g_lastHash = 0;
        g_pendingHash = 0;
        g_stable = 0;
        g_poll = 0;
    }

    void Tick(void**, const Games::Settings& s, const OdstCheats::Players&, Blam::DataArray*) {
        __try {
            TickInner(s);
        } __except (Log::Exception(GetExceptionInformation(), "ODST checkpoint history")) {
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
