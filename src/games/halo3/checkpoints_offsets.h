#pragma once

// halo3.dll offsets for checkpoints.cpp (Lock checkpoints, checkpoint history).
// MCC 1.3528.0.0, found 2026-10-10 by static disassembly (tools/re) and
// checked against the running game (070_waste). "static" = read from the
// disassembly only; "live" = the data was also read from the running game.
// Nothing here has been CALLED or WRITTEN by the research; no save was made.
//
// How Halo 3 checkpoints work (the same double-buffered design as Reach):
//  1. game_save* -> 0x1C92B8(priority) only raises a pending-save priority.
//  2. The save processor 0x1C916C (game tick, when it is safe to save) clears the
//     pending priority, runs 0x1C94FC, shows the "Checkpoint..." message
//     (0x2D41F4(1)) and sets the byte at RVA 0x20B96AC.
//  3. The main loop (0xB2250) sees that byte at 0xB23F7: shows the "done" message
//     (0x2D41F4(0)), then calls the snapshot 0x9E04C at 0xB2410 (profile scope
//     "checkpoint save" at 0xB2427) and clears the byte.
//  4. The snapshot 0x9E04C advances the current-slot index ((cur + 1) % count),
//     clears the new slot's valid byte, memcpy's the live game state into the slot
//     (size 0x970000), sets the valid byte, then copies the 0xFCF0-byte save header
//     into the matching blob.
//  5. Revert: the word at RVA 0x20B96AA = 0x0C01 -> the main loop's load path (0xB35AC,
//     scope "checkpoint load") -> 0x9E278 -> 0x16FD6C(current slot) memcpy's the slot
//     back over the game state, after checking the blob.

// The snapshot function (void, no arguments). Called from three places:
// 0xB2410 (the save request, what we veto), 0x9E63D (map start) and 0xF29C5 (level
// start/continue). Only the 0xB2410 call may be vetoed, or a fresh map has no first
// checkpoint. static.
#define OFFSET_HALO3_CHECKPOINT_SNAPSHOT        0x9E04C
// Return address of the 0xB2410 call (the main loop's save step). static.
#define OFFSET_HALO3_CHECKPOINT_SNAPSHOT_RETURN 0xB2415

// The checkpoint HUD message, void(uint8 started): 1 = "Checkpoint..." (from the save
// processor at 0x1C9289), 0 = "Checkpoint...done" (from the main loop at 0xB2402, BEFORE
// the snapshot). Only those two callers exist. static.
#define OFFSET_HALO3_CHECKPOINT_MESSAGE         0x2D41F4

// Slot entries: 16 bytes each, count at 0x2030324. Entry +0 = pointer to the snapshot
// (heap, 0x970000 bytes, the two are back to back), entry +8 = valid byte (cleared
// while the snapshot is being copied, set once it is). live: slot 0 at 0x7FF3B5640000,
// slot 1 at 0x7FF3B5FB0000, both valid.
#define OFFSET_HALO3_CHECKPOINT_SLOTS           0x21BE4E0
#define HALO3_CHECKPOINT_SLOT_STRIDE            16
#define HALO3_CHECKPOINT_SLOT_VALID             8
// Number of slots (int32, = 2). live.
#define OFFSET_HALO3_CHECKPOINT_COUNT           0x2030324
// Current slot index (int32, 0/1): the slot the last save wrote and the one a revert
// loads (0x9E1A7 / 0x9E3B8 read it; the snapshot stores (cur + 1) % count before the
// copy). live: reads 0 / 1, flipping on saves. static for the "newest" meaning.
#define OFFSET_HALO3_CHECKPOINT_CURRENT         0x2030328
// Snapshot size (int32, = 0x970000) and storage mode (int32, 1 = plain copy; anything
// else is a compressed/other path the history doesn't handle). Written at init by
// 0x8DE2. live.
#define OFFSET_HALO3_CHECKPOINT_SIZE            0x21BE4D4
#define OFFSET_HALO3_CHECKPOINT_MODE            0x21BE4DC
#define HALO3_CHECKPOINT_MODE_COPY              1
// The save header blobs, one per slot: 0xFCF0 bytes at 0x2030330 + slot * 0xFCF0.
// live: equal to the first 0xFCF0 bytes of the same slot's snapshot except 40 bytes at
// +0xFCC4 (a signature). The revert reads the blob of the current slot first and
// refuses to load if it doesn't check out, so history keeps and restores it with the
// snapshot.
#define OFFSET_HALO3_CHECKPOINT_BLOBS           0x2030330
#define HALO3_CHECKPOINT_BLOB_SIZE              0xFCF0
// Inside a snapshot: the map path ("levels\solo\070_waste\070_waste"), at +0x10. live.
#define HALO3_CHECKPOINT_MAP_NAME               0x10
