#pragma once

// halo4.dll offsets for teams.cpp (Nuke, Despawn custom AI, Random AI teams,
// Your team, Allies never turn against you). MCC 1.3528.0.0, found 2026-10-12
// with tools/re/h4 against the Halo 4 campaign running live (m60_rescue).
// "static" = disassembly only; "live" = the data was also read (or poked and
// restored) in the running game. No engine function listed here has been
// CALLED yet.

// actor_erase(actor datum, bool deferred): the per-actor step of ai_erase /
// ai_erase_all (evaluator 0x7122E8 calls it for every actor of every squad,
// with edx = 0). Byte-for-byte the shape of Reach's 0x6AA324: actor entry
// 0x1380 from TLS slot 774, unit datum +0xC, swarm +0x14; detaches the actor
// (0x72F5D8), then object_delete (0x5D4228) on its unit, or the deferred
// delete (0x5D43A4) when the bool is set. static.
#define OFFSET_HALO4_ACTOR_ERASE              0x731290

// Allegiance record update(record*, bool broken (dl), bool remove (r8b),
// bool (r9b)): record* is the record's team-A byte (table + 4 + 0x14 * i).
// Returns at once if !remove and the record's broken byte (+8) already equals
// `broken`. Otherwise sets it and rewrites the friends mask (+0x144) from
// `remove` and the hostility mask (+0x168, both directions) from `broken`
// (a broken allegiance clears the bits), then refreshes the AI of both teams
// (0x70DF60). The betrayal break calls it as (rec, 1, 0, 1), the record add
// as (rec, 0, 0, 0); we put an allegiance back with (rec, 0, 0, 0). static.
#define OFFSET_HALO4_ALLEGIANCE_RECORD_UPDATE 0x208180

// --- Layouts -------------------------------------------------------------------
// Allegiance table = TLS slot 51 (offsets.h): u16 count at +0 (max 16);
// record i is table + 4 + 0x14 * i. Live on m60_rescue: one record,
// {team 1, team 2, threshold 5, 1200 ticks per point, not broken}; masks at
// +0x168: team 1 = 0x6, team 2 = 0x6, every other team only itself.
// Score accumulation is 0x207DF0 (callers: hit/kill in the prop pass
// 0x7BF18D, 0x70DB30): it adds to the score and, when
// score >= threshold and threshold != -1, calls the record update with
// broken = 1. static; layout live (record bytes read, offsets match the add
// code at 0x207BF4).
#define HALO4_ALLEG_COUNT           0x0
#define HALO4_ALLEG_RECORDS         0x4
#define HALO4_ALLEG_RECORD_SIZE     0x14
#define HALO4_ALLEG_MAX_RECORDS     16
#define HALO4_ALLEG_REC_A           0x0   // int8 team
#define HALO4_ALLEG_REC_B           0x1   // int8 team
#define HALO4_ALLEG_REC_THRESHOLD   0x2   // int16, -1 = never (0x207F2D skips the break)
#define HALO4_ALLEG_REC_BROKEN      0x8   // byte, 1 = broken
#define HALO4_ALLEG_REC_SCORE       0xA   // uint16 betrayal score

// Player record team: int8 at player entry +2 (the word at +2 is copied to
// the unit's word at +0x498 = team + a second byte; the copy is in the unit
// update, RVA 0x5EC667, for a unit with a player datum at +0x49C). Live on
// m60_rescue: writing 7 there made the player unit's team 7 within 150 ms and
// writing 1 put it back (so it sticks, unlike writing the unit's own).
#define HALO4_TEAMS_PLAYER_TEAM     0x2

// Actor 'char' tag datums (live, 12 actors on m60_rescue): +0x2C is the
// character the squad asked for (storm_pawn), +0x48 the one actually used
// (storm_pawn_prime); the names are the same for plain characters.
#define HALO4_TEAMS_ACTOR_CHARACTER 0x48
