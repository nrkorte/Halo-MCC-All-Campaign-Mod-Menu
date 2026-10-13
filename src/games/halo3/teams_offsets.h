#pragma once

// halo3.dll offsets for teams.cpp (Nuke, Despawn custom AI, Random AI teams,
// Your team, Allies never turn against you). MCC 1.3528.0.0. All of them were
// found 2026-10-10 from the HaloScript function table (tools/re/hsfunc.py:
// the evaluator is the pointer at definition+0x20). "static" = read from the
// disassembly only; "live" = the data layout was also read from the running
// game (070_waste). No engine function listed here has been CALLED yet.

// --- Allegiance functions (call on the game thread only) ---------------------
// ai_allegiance(team a, team b), opcode 0x161, evaluator 0x1E75AC: loads two
// int16 and calls this wrapper (ecx = a, edx = b, both 16-bit). It picks the
// betrayal threshold (5 if either team is team 1, else -1) and the ticks per
// betrayal point from the difficulty, then calls the record add 0x199C38.
// static.
#define OFFSET_HALO3_AI_ALLEGIANCE            0x575430
// bool ai_allegiance_remove(team a, team b), opcode 0x162, evaluator 0x1E75DC:
// finds the record (either order), calls the record update 0x19A0C0 with
// "remove", then deletes the record from the table. static.
#define OFFSET_HALO3_AI_ALLEGIANCE_REMOVE     0x199D58
// record update(record*, bool broken (dl), bool remove (r8b), bool (r9b)):
// the one place that writes the two team masks (friends +0x144: set/clear both
// directions; allied +0x184: cleared while "broken"), sets record +0xA to
// `broken`, then calls 0x56E918, which walks the actors of both teams and
// refreshes the hostility cached in their perception props (so an allegiance
// change takes effect on AI that already see each other). Returns at once if
// !remove and record +0xA already == broken. Betrayal break calls it as
// (record, 1, 0, 1); putting an allegiance back as (record, 0, 0, 0). static.
#define OFFSET_HALO3_ALLEGIANCE_RECORD_UPDATE 0x19A0C0
// actor erase(actor datum, bool): ai_erase's per-actor step (0x574530 calls
// it for each actor of the squad; ai_erase_all's evaluator 0x1E7270 likewise).
// Detaches the actor (0x55F2F0) then deletes its unit (object_delete 0x342ED8
// with 0, a deferred variant 0x343098 with 1) or, with no unit, its swarm.
// static.
#define OFFSET_HALO3_ACTOR_ERASE              0x560AA8

// --- Layouts (live on 070_waste) ----------------------------------------------
// The allegiance table is the TLS slot 48 block itself: u16 record count at +0
// (max 16), records of 0x14 from +4, then the friends[16] masks at +0x144
// (game_team_is_friendly, "not hostile") and allied[16] at +0x184 (cleared for
// a broken allegiance). Live: 11 records; (2,1) (3,1) (1,8) (1,5) with
// threshold 5 and 1200 ticks per point, the rest -1.
#define HALO3_ALLEG_COUNT           0x0
#define HALO3_ALLEG_RECORDS         0x4
#define HALO3_ALLEG_RECORD_SIZE     0x14
#define HALO3_ALLEG_MAX_RECORDS     16
#define HALO3_ALLEG_REC_A           0x0   // int16 team
#define HALO3_ALLEG_REC_B           0x2   // int16 team
#define HALO3_ALLEG_REC_THRESHOLD   0x4   // int16 betrayal threshold, -1 = never (0x199E48: score >= threshold breaks)
#define HALO3_ALLEG_REC_BROKEN      0xA   // byte, 1 = broken (set by the record update's `broken`)
#define HALO3_ALLEG_REC_SCORE       0xC   // int16 betrayal score (hit +1, kill +3, forgive -1, decays)
#define HALO3_ALLEGIANCE_ALLIED     0x184 // uint32[16]

// Actor (TLS slot 1, 0xAA0 each; live): +0x14 unit datum (offsets.h), +0x18
// swarm, +0x20 int16 team. Unit +0x168 holds its actor's full datum, +0x174 the team.
#define HALO3_ACTOR_TEAM            0x20
#define HALO3_ACTOR_SWARM           0x18
