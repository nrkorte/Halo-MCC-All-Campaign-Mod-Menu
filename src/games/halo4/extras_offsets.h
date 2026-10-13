#pragma once

// halo4.dll offsets for extras.cpp (Disable friendly fire, Time stop, "Invulnerable
// AI can die"), MCC 1.3528.0.0, found 2026-10-12 by porting Reach's versions
// (tools/re/reach show.py/xref.py against tools/re/h4 show.py/xref.py/fn.py, the
// HaloScript table in hs_funcs.txt) and checked against the running game
// (m60_rescue, read-only). Each value says whether it was verified live.

// --- Disable friendly fire --------------------------------------------------------
// Cause damage: void(damage_data* rcx, float scale xmm1, uint32 victim datum r8d,
// uint16 region r9w, 3 more stack args), RVA 0x646F94: the same function as Reach's
// 0x4DCB30 (same prologue, 4 callers, the same shape; both are what every hit ends in).
// Found from damage_object_effect (script, evaluator 0x286DAC): it builds a damage_data
// with damage_data_new (0x69C570) and calls the wrapper 0x646F20(data, victim, region,
// node, word, int), which checks the victim (0xCC9E4), computes the damage scale
// (0x646560) and calls 0x646F94. Its other callers are the projectile impact function
// (0x64899C, three calls: 0x648D0F, 0x648F20, 0x6491AD). Prologue (LIVE-checked): 40 53 56
// 57 41 54 41 55 41 56 41 57 48 81 EC C0 00 00 00 48 8B 05 ... (relocatable).
#define OFFSET_HALO4_OBJECT_CAUSE_DAMAGE 0x646F94
// damage_data: +0 effect tag datum, +4 flags, +8 instigating player datum, +0xC
// instigating unit datum (damage_data_new, RVA 0x69C570, fills +8/+0xC/+0x10 with -1 from
// a constant at 0x1049A30). The projectile path (0x603235..0x60327F, the weapon's hit
// data) copies a 0x14-byte owner block {player datum, unit datum, ...} to data +8, and
// looks the unit datum up as an object (0x1805DA400). Static only: the log's first 12
// "friendly fire blocked" lines (and the "hit on ally" lines) confirm it.
#define HALO4_DAMAGE_OWNER_PLAYER 0x8
#define HALO4_DAMAGE_OWNER_UNIT   0xC

// --- Time stop ----------------------------------------------------------------------
// The per-object update. Reach's wrapper 0x475DCC (void(datum), tail-jumps into the
// main update 0x475E3C, ends with `and word [entry+2], 0xFDFF`) exists twice in Halo 4:
//  - 0x5DDC64: the main update with the wrapper inlined; the tick's object loop (function
//    0x5D0440, loop at 0x5D04E0: entries of its update list whose entry byte +2 has
//    (flags & 0x14) == 4, then post steps 0x5DE3FC / 0x6435F4) calls it at 0x5D0512.
//    Ends with `and word [entry+2], 0xFDFF` at 0x5DE11A. It's the only caller, so a hook
//    here is the whole per-tick update. Prologue (LIVE-checked): 40 55 41 54 41 55 41 56
//    41 57 48 8D AC 24 50 78 FF FF B8 B0 88 00 00 E8 ... (relocatable, the call is a
//    __chkstk).
//  - 0x5DE150: the small wrapper (test byte [obj+0x100],1 ... call 0x5DF5E4, tail jmp to
//    the main update 0x5DE1C8..0x5DE3F9, which also ends with the 0xFDFF clear at 0x5DE3E1);
//    6 callers: 0x5CE2D2/0x5CE356 (small helpers), 0x5D0969, the child recursions
//    0x5DDEF6/0x5DE0BD/0x5DE38A. Nothing uses the return value of either. Prologue
//    (LIVE-checked): 40 53 48 83 EC 20 8B 15 BC 90 A7 00 65 48 8B 04 25 58 00 00 00 ...
// Skipping a frozen object leaves its object table entry's word +2 bit 9 (byte +3 bit
// 0x02) set, which only a real update clears (Reach: stuck AI after release, found live), so
// the release clears it. Live: no entry has it set when time runs normally (byte +3 is 0
// or 1); whether a skip sets it in Halo 4 is not verified (a clear of an unset bit is harmless).
#define OFFSET_HALO4_OBJECT_UPDATE         0x5DDC64
#define OFFSET_HALO4_OBJECT_UPDATE_WRAPPER 0x5DE150
#define HALO4_OBJECT_ENTRY_SKIPPED_FLAGS 3
#define HALO4_OBJECT_ENTRY_SKIPPED_BIT   0x02

// The projectile pass: after the objects update, 0x607630 (called at 0x5D0887) walks the
// weapons whose +0x490 bit 0 is set and calls
// void fire_marked_barrels(uint32 weapon rcx, rdx 0, r8b 0, r9d -1) = RVA 0x60768C (also
// called from 0x5959E0); for each barrel (weapon +0x51C, 0x34 each) whose state word (+4)
// has bit 2 ("fired this tick", set by the weapon update's barrel fire 0x614FC0) it calls
// the projectile maker 0x6176B8 and clears the mark (and 0xFFFD), then clears weapon
// +0x490 bit 0. Same function as Halo 3's 0x35EC84. A frozen AI's shot that was already
// marked, or one made outside the object update, is dropped here (the veto clears the marks
// itself, so nothing is delayed). Prologue (LIVE-checked): 48 8B C4 48 89 58 08 48 89 68 18
// 48 89 70 20 48 89 50 10 57 41 54 41 55 ... (relocatable). A different address than
// halo4.cpp's barrel-fire hook (0x614FC0) and projectile-of hook (0x6826E0).
#define OFFSET_HALO4_FIRE_MARKED_BARRELS 0x60768C
#define HALO4_WEAPON_FIRE_FLAGS          0x490   // word: bit 0 = has marked barrels
#define HALO4_WEAPON_BARREL_FLAGS        0x4     // word in each barrel's state: bit 1 (0x2) = marked
#define HALO4_WEAPON_MARKED_BARREL_FLAG  0x2

// Actors: actor_set_active (OFFSET_HALO4_ACTOR_SET_ACTIVE, offsets.h) tests/sets bit 3
// of the byte at actor +0x89 (0x72EA0C: `shr al,3; and al,1` against the argument; it
// sets `or dil,8` at 0x72EA33). LIVE (m60_rescue): 28 of 31 actors read 0x09 (awake), the
// three dormant ones 0x00. The unit datum is HALO4_ACTOR_UNIT (+0xC, offsets.h).
#define HALO4_ACTOR_ACTIVE_FLAGS 0x89
#define HALO4_ACTOR_ACTIVE_BIT   0x08

// --- Invulnerable AI can die ---------------------------------------------------------
// The AI revive list exists in Halo 4 exactly as in Reach: AI globals (TLS slot 6), 8
// entries of 0x18 bytes from +0x43C, unit datum first, free = {-1,-1,0,-1,0,0,0xFFFF...}.
// LIVE (m60_rescue): all 8 entries are free and read exactly Reach's empty pattern (ff*8,
// 00*4, ff*4, 00*6, ff ff), so the layout matches; none was occupied (no mission AI is
// revived on this level), so "a protected unit's entry is emptied" is by analogy. The adder
// is 0x72D128(actor, flag) (Reach 0x6A669C; called from 0x72E402): it takes the first free
// entry, writes the actor's unit (actor +0xC) at +0, the flag at +4, then calls
// 0x5E54DC(unit, true), which sets bit 0 and bit 6 of the unit's byte at +0x17C (Reach set bit 0
// of +0x143 directly). LIVE: dead marines/Prometheans read 0x41/0x3E/0x2E/0x06 there, living AI 0.
#define HALO4_AI_REVIVES       0x43C
#define HALO4_AI_REVIVE_SIZE   0x18
#define HALO4_AI_REVIVE_COUNT  8
#define HALO4_AI_REVIVE_UNIT   0x0
#define HALO4_OBJECT_REVIVABLE 0x17C   // byte, bit 0 set while on the revive list

// The damage flags word (offsets.h HALO4_OBJECT_DAMAGE_FLAGS, 0x16C): bit 2 = dead (LIVE: every
// dead biped reads 0xC, living ones 0 or 0x80; ai_resurrect-style checks use it in Reach).
#define HALO4_DAMAGE_FLAG_DEAD (1u << 2)

// The actor's character: LIVE (m60_rescue) two 'char' tag datums sit at actor +0x2C and
// +0x48 (the pawns' read storm_pawn / storm_pawn_prime, marines storm_marine_m60 twice,
// the same pair Reach has at +0x4C / +0x6C where the second is the specific character, e.g.
// spartan_kat). +0x48 is used; which of the two is the "own" character was not told apart.
#define HALO4_ACTOR_CHARACTER 0x48

// --- Time stop, second part (2026-10-12, after a user test: "the AI still move") --------
// The object update hooks above only froze the pose. The animation / pre-update pass
// bool pass(uint32 datum) = RVA 0x5DD2E4 (returns "changed" in al, accumulated by one caller
// 0x5DD72B and ignored by the others) kept running for frozen AI: it is called by the tick's
// object loops (0x5D01A5, 0x5D027D, 0x5D094B in the second object pass 0x5D08CC) and runs the
// animation processing (0x5DC490, root motion, footstep/sound events) and the per-node
// updates, then clears bit 2 of entry word +2 (0x5DD3DB). Root motion moved the unit's
// physics under the frozen pose, and the sound events kept firing; on release the object
// caught up (the teleport). Static only (not toggleable from outside); the hook returns 0 for
// frozen objects. Prologue: 48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 ... (relocatable).
// The log's "frozen object ... moved" lines (halo4 extras Pin) show anything that still moves one.
#define OFFSET_HALO4_OBJECT_ANIMATION 0x5DD2E4
