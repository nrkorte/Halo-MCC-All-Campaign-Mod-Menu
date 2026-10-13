#pragma once

// halo3odst.dll offsets for checkpoints.cpp (Lock checkpoints, checkpoint history).
// MCC 1.3528.0.0, found 2026-10-11 by static disassembly (tools/re/odst: od.py,
// port.py) and checked against the running game (sc120): "static" = read from the
// disassembly only; "live" = the data was also read from the running game.
// Nothing here has been CALLED or WRITTEN by the research; no save was made.
//
// Same double-buffered design as Halo 3 (the layouts differ slightly):
//  1. game_save* -> 0x1FDFE4(priority) (OFFSET_ODST_GAME_SAVE in offsets.h) raises a pending save.
//  2. The save processor (0x1FDE80, game tick) shows the "Checkpoint..." message
//     (0x2FF168(1) from 0x1FDFBB) and sets the byte at RVA 0x20FF6BC.
//  3. The main loop (0xBF924) sees that byte at 0xBFACB, shows the "done" message
//     (0x2FF168(0) at 0xBFAD6), then calls the snapshot 0xAABA4 at 0xBFAE4 and clears the byte.
//  4. The snapshot advances the current-slot index ((cur + 1) % count), clears the new slot's
//     valid byte, memcpy's the live game state (global pointer 0x220EE08, size 0x220EE14) into the
//     slot, sets the valid byte, then copies the 0xFBE8-byte save header (from the pointer at
//     0x2094D68, = the same live state base) into the matching blob.
//  5. Revert: the word at RVA 0x20FF6BA = 0x0C01 (offsets.h) -> the main loop's load path, which
//     restores the current slot over the game state.

// The snapshot function (void, no arguments). Callers: 0xBFAE4 (the main loop's save step, what
// we veto), 0xAB0F9 and 0x10E155 (map start / level start). Only the 0xBFAE4 call may be
// vetoed. Prologue (live-image bytes 48 89 5C 24 10 / 48 89 74 24 18 / 57) is relocatable. static.
#define OFFSET_ODST_CHECKPOINT_SNAPSHOT        0xAABA4
// Return address of the 0xBFAE4 call. static.
#define OFFSET_ODST_CHECKPOINT_SNAPSHOT_RETURN 0xBFAE9

// The checkpoint HUD message, void(uint8 started): 1 = "Checkpoint..." (from the save processor
// at 0x1FDFB6), 0 = "Checkpoint...done" (from the main loop at 0xBFAD6, BEFORE the snapshot).
// Only those two callers exist. (port.py's "match" 0x2FF0FC is a sibling with other string ids
// and other callers; not this one.) Prologue 48 89 5C 24 08 / 57 / 48 83 EC 20. static.
#define OFFSET_ODST_CHECKPOINT_MESSAGE         0x2FF168

// Slot entries: 16 bytes each from 0x220EE20 (the snapshot code addresses them as base 0x220EE00
// + cur*16 + 0x20), count at 0x2075564. Entry +0 = pointer to the snapshot (heap, 0x970000 bytes,
// back to back), entry +8 = valid byte. live: slot 0 at 0x7FF449C50000 (never written yet, valid 0),
// slot 1 at 0x7FF44A5C0000 (valid 1).
#define OFFSET_ODST_CHECKPOINT_SLOTS           0x220EE20
#define ODST_CHECKPOINT_SLOT_STRIDE            16
#define ODST_CHECKPOINT_SLOT_VALID             8
// Number of slots (int32, = 2). live.
#define OFFSET_ODST_CHECKPOINT_COUNT           0x2075564
// Current slot index (int32): the slot the last save wrote; the snapshot stores (cur+1)%count
// before the copy. live: read 1 with slot 1 the valid one. static for "newest".
#define OFFSET_ODST_CHECKPOINT_CURRENT         0x2075568
// Snapshot size (int32, = 0x970000; set at init by 0x90A2) and storage mode (int32, 1 = plain
// copy; the snapshot's else-branch is another/compressed path the history doesn't handle). live.
#define OFFSET_ODST_CHECKPOINT_SIZE            0x220EE14
#define OFFSET_ODST_CHECKPOINT_MODE            0x220EE1C
#define ODST_CHECKPOINT_MODE_COPY              1
// The save header blobs, one per slot: 0xFBE8 bytes at 0x2075570 + slot * 0xFBE8. live: blob 1
// equals the first 0xFBE8 bytes of slot 1's snapshot except 40 bytes at +0xFBBC (a signature),
// as in Halo 3. History keeps and restores it with the snapshot.
#define OFFSET_ODST_CHECKPOINT_BLOBS           0x2075570
#define ODST_CHECKPOINT_BLOB_SIZE              0xFBE8
// Inside a snapshot: the map path ("levels\atlas\sc120\sc120"), at +8 (Halo 3 has it at +0x10). live.
#define ODST_CHECKPOINT_MAP_NAME               0x8
