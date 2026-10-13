#pragma once

// Offsets for ai_upgrade.cpp (Upgrade AI, Random AI swap, Squads) in halo3.dll,
// MCC 1.3528.0.0. Kept apart from offsets.h, which other work items edit.
// Found statically 2026-10-10 from the HS table (tools/re/hsfunc.py); the actor
// fields were checked live on 070_waste.
//
// ai_migrate (opcode 0x15F, evaluator 0x1E756C) -> 0x57525C(squad a, squad b)
// walks the actors of squad a (iterator 0x57391C / 0x5739C8, which yields actor
// datums) and calls 0x5617AC(actor datum, int16 squad b) for each; the same
// function then moves the squad's loose objects (0x565AF8) which a single actor
// doesn't need. 0x5617AC: detaches the actor from its squad/cell (0x560248,
// 0x5676E8 or 0x5678A8), attaches it to squad b (0x5674F8(actor, squad, 1): the
// 1 stops it rewriting the actor's team; it sets actor +0x30 = squad, links it
// into the squad's actor list at squad +0x6C, activates it per the squad's
// flags) and gives it the squad's fire team (0x5A04C4 + 0x563660). Squads are
// 0xB0-byte entries of TLS slot 10.
#define OFFSET_HALO3_ACTOR_MIGRATE 0x5617AC  // void(actor datum, int16 squad index); -1 detaches only
// ai_erase (opcode 0x152, evaluator 0x1E7248) -> 0x574530(squad): for each actor
// of the squad, 0x560AA8(actor datum, 0) (a datum equal to the actor currently
// running its update is only flagged, +0xD, and left to finish). 0x560AA8 reads
// the unit (actor +0x14), detaches the actor (0x55F2F0), frees the actor and
// then object_delete's the unit (0x342ED8 = OFFSET_HALO3_OBJECT_DELETE; the
// argument 1 would take 0x343098 instead). So it is the whole erase: the unit
// must not be deleted separately.
#define OFFSET_HALO3_ACTOR_ERASE   0x560AA8  // void(actor datum, bool = false)

// Actor fields (0xAA0 each, TLS slot 1; HALO3_ACTOR_UNIT 0x14 is in offsets.h).
// Checked live on 070_waste: +0x5C holds the 'char' tag datum (marine,
// brute_major, grunt_ultra... and the mod's own spawns), +0x30 the squad index
// (squadless spawns -1, a grunt pack all 0x103: the same squad as the scenario
// lists), +0x20 the team, equal to the unit's team (+0x174) in every actor read.
#define HALO3_ACTOR_SQUAD_INDEX    0x30  // int32, -1 = squadless (index into TLS slot 10)
#define HALO3_ACTOR_CHARACTER      0x5C  // datum of the 'char' tag
