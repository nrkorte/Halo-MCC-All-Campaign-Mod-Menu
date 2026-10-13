#pragma once

// halo3odst.dll offsets for teams.cpp (Nuke, Despawn custom AI, Random AI teams,
// Your team, Allies never turn against you). MCC 1.3528.0.0. Found 2026-10-11 by
// static disassembly of the ODST image (tools/re/odst/od.py) starting from the
// HaloScript evaluators (tools/re/odst/hs.py: ai_allegiance 0x21A6C0,
// ai_allegiance_remove 0x21A6F0, ai_erase_all 0x21A29C); the allegiance table
// layout was also read from the running game (main menu: slot 47, 1 record).
// "static" = disassembly only; "live" = the data was read from running ODST.
// No engine function listed here has been CALLED yet.

// --- Allegiance functions (call on the game thread only) ---------------------
// ai_allegiance(team a, team b): the evaluator 0x21A6C0 loads two int16 and calls
// this wrapper (ecx = a, edx = b, 16-bit). It picks the betrayal threshold (5 if
// either team is team 1, else -1) and the ticks per point from the difficulty,
// then the record add 0x17ED04. static.
#define OFFSET_ODST_AI_ALLEGIANCE            0x5BC004
// bool ai_allegiance_remove(team a, team b): evaluator 0x21A6F0 calls this; it
// finds the record (either order), calls the record update with "remove", then
// deletes the record from the table. static.
#define OFFSET_ODST_AI_ALLEGIANCE_REMOVE     0x17EE48
// record update(record*, bool broken (dl), bool remove (r8b), bool (r9b)): the
// one place that writes the friends masks (+0x144, both directions) and allied
// masks (+0x184, cleared while "broken"), sets record +0xA to `broken` and
// refreshes the actors' cached hostility (0x5B2040). Returns at once if !remove
// and record +0xA == broken. Same signature as Halo 3's. static.
#define OFFSET_ODST_ALLEGIANCE_RECORD_UPDATE 0x17F2B8
// actor erase(actor datum, bool deferred): the per-actor step of ai_erase /
// ai_erase_all (0x21A2D4 calls it with edx = 0). Detaches the actor, then
// deletes its unit (0x37E4A4, or deferred 0x37E650) or, with no unit, its
// swarm. Actor size 0xAB0, unit at +0x14, swarm +0x18. static.
#define OFFSET_ODST_ACTOR_ERASE              0x5AB2CC

// --- Layouts -------------------------------------------------------------------
// The allegiance table is TLS slot 47 itself (the code reads [tls + 0x178]):
// u16 record count at +0 (max 16), records of 0x14 from +4, friends[16] masks at
// +0x144 and allied[16] at +0x184. Record: int16 team a, int16 team b, int16
// threshold (+4, -1 = never), int16 ticks per point (+6), +8/+9 flags, +0xA
// broken byte, +0xC int32 betrayal score (all from the record add 0x17ED04, which
// is also what the live record (1,2) threshold 5 ticks 1200 matches). live+static.
#define ODST_ALLEG_COUNT           0x0
#define ODST_ALLEG_RECORDS         0x4
#define ODST_ALLEG_RECORD_SIZE     0x14
#define ODST_ALLEG_MAX_RECORDS     16
#define ODST_ALLEG_REC_A           0x0   // int16 team
#define ODST_ALLEG_REC_B           0x2   // int16 team
#define ODST_ALLEG_REC_THRESHOLD   0x4   // int16, -1 = never
#define ODST_ALLEG_REC_BROKEN      0xA   // byte, 1 = broken
#define ODST_ALLEG_REC_SCORE       0xC   // int32 betrayal score

// Actor +0x20 int16 team (actor_set_team writes it, see spawn.cpp).
#define ODST_TEAMS_ACTOR_TEAM      0x20
