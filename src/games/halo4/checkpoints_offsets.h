#pragma once

// halo4.dll offsets for checkpoints.cpp (Lock checkpoints, checkpoint history).
// MCC 1.3528.0.0, found 2026-10-12 by static disassembly (tools/re/h4, by
// matching Reach's checkpoint code at 0x16504/0x16658) and read from the running
// game (m60_rescue). "live" = the data was read from the running game; nothing
// here has been CALLED or WRITTEN by the research.
//
// How Halo 4 checkpoints work (Reach's double-buffered design):
//  1. game_save* -> OFFSET_HALO4_GAME_SAVE (0x194DBC, offsets.h) only raises a pending-save
//     priority in the TLS state; the real save happens later.
//  2. The snapshot function 0x48DC4 (callers 0x49543, 0x60A16 = main loop, 0x9F89B)
//     advances the current-slot index ((cur + 1) % count), clears the new slot's
//     valid byte, memcpy's the live game state (global pointer 0x2AE2AB8, 0xAE6000 bytes)
//     into the slot, then sets the valid byte.
//  3. Revert: the word at OFFSET_HALO4_GAME_REVERT_FLAG = 0x0C01 -> the load function
//     0x49014: copies the current slot's 0x2D0C0-byte header out, validates it against
//     the live state's header (0x497C4 / 0x49954, below), memcpy's the whole slot back
//     over the live state (0x491CA) and re-validates. A failed check sets 0x293DEA8/AA
//     (the game's "load failed" path).

// Slot entries: 16 bytes each. Entry +0 = pointer to the snapshot (heap, 0xAE6000 bytes
// used, 0xAF0000 apart), entry +8 = valid byte (cleared while the snapshot is copied).
// live: slot 0 at 0x1388A790000, slot 1 at 0x1388B280000, both valid.
#define OFFSET_HALO4_CHECKPOINT_SLOTS           0x2AE2AD0
#define HALO4_CHECKPOINT_SLOT_STRIDE            16
#define HALO4_CHECKPOINT_SLOT_VALID             8
// Number of slots (int32, = 2; written by 0x489B1 at init). live.
#define OFFSET_HALO4_CHECKPOINT_COUNT           0x27C6074
// Current slot index (int32, 0/1): the slot the last save wrote and the one a revert
// loads (0x49069/0x49092/0x49180 read it; the snapshot stores (cur + 1) % count before
// the copy, 0x48E11-0x48E2B). live: reads 1 with both slots valid (static for "newest").
#define OFFSET_HALO4_CHECKPOINT_CURRENT         0x27C607C
// Snapshot size (int32, = 0xAE6000, the memcpy length at 0x491B4/0x48EC4; set by 0x3CA8C).
// live.
#define OFFSET_HALO4_CHECKPOINT_SIZE            0x2AE2AC4
// Pointer to the live game state the snapshot copies from and the revert copies into
// (also what the revert's header check compares against). live: 0x1387FFD0000.
#define OFFSET_HALO4_CHECKPOINT_LIVE            0x2AE2AB8

// Inside a snapshot: the map path ("environments\solo\m60_rescue\m60_rescue"), at +8. live.
#define HALO4_CHECKPOINT_MAP_NAME               8
// The header: the first 0x2D0C0 bytes of a snapshot (the revert copies exactly this
// much out at 0x490B4). live: the two slots differ inside it at +0x2D068..+0x2D0B8
// (zone/area descriptors) and everywhere after it.
#define HALO4_CHECKPOINT_HEADER_SIZE            0x2D0C0
// The revert's header check (0x49954, slot header vs live state header) needs the map path
// (strcmp at +8), bytes [0x128, 0x138) and [0x2D068, 0x2D088) to be equal, so a kept
// checkpoint from a different area set would be refused by the game (its load-failed path).
// static (read from 0x49954); live: both slots' [0, 0x138) matched the live state's, and
// the 0x2D068 block matched when the slot was taken in the same area as the live state.
#define HALO4_CHECKPOINT_CHECK_A_BEGIN          0x128
#define HALO4_CHECKPOINT_CHECK_A_END            0x138
#define HALO4_CHECKPOINT_CHECK_B_BEGIN          0x2D068
#define HALO4_CHECKPOINT_CHECK_B_END            0x2D088

// The snapshot function, void(void). Callers: 0x49543 and 0x9F89B (map start / level start,
// must run) and 0x60A16 (the main loops save step, which Lock checkpoints skips); the return
// address of that call is 0x60A1B. static. The save request 0x194DBC and the processor
// 0x194C0C (sets the byte at 0x293DEAF the main loop acts on) are left untouched: game_saving,
// game_save_cinematic_skip (priority 4) and the scripts waiting on them depend on them.
#define OFFSET_HALO4_CHECKPOINT_SNAPSHOT        0x48DC4
#define OFFSET_HALO4_CHECKPOINT_SNAPSHOT_RETURN 0x60A1B

// Engine query bool(void): a cinematic is playing (TLS state +0xC8, byte +5). static; live: the
// struct read 01 at +5 while the Rally Point Bravo cutscene played. +6 (0x12EF94, skip) also read 1.
#define OFFSET_HALO4_CINEMATIC_PLAYING          0x12EF68
