#pragma once

// Offsets for ai_upgrade.cpp (Upgrade AI, Random AI swap, Squads) in halo2.dll,
// MCC 1.3528.0.0. Kept apart from offsets.h, which other work items edit.

// Per-actor cores of the HaloScript functions (HS table, tools/re/h2/hs.py; found
// statically 2026-10-10, NOT called live):
//  ai_migrate (evaluator 0x786780) -> 0x619e40(squad a, squad b) -> 0x619c50
//  walks the actors of squad a and calls 0x625840(actor datum, squad b word) for
//  each. 0x625840 detaches the actor from its squad (0x63A5A0), then attaches it
//  (0x63A380(actor, squad, 1)) when the actor is awake, else stores the squad in
//  actor +0x34 to attach later. The squad argument is the index into the squad
//  array (OFFSET_HALO2_AI_RECORDS, 0x98 each); -1 detaches only.
#define OFFSET_HALO2_ACTOR_MIGRATE 0x625840  // void(actor datum, int16 squad)
//  ai_erase (evaluator 0x786560) -> 0x618CC0 walks the actors of a squad and, for
//  each, reads the unit (actor +0x18), calls 0x624B20(actor datum) (the actor
//  delete: it ends in a tail call to 0x623490(actor, 1); the unit's own death
//  path 0x8F6BAF calls it too) and then object_delete(unit).
#define OFFSET_HALO2_ACTOR_ERASE   0x624B20  // void(actor datum)

// Actor fields (0x8A0 each, OFFSET_HALO2_ACTORS). Checked live on 05b_deltatowers:
// actor 0 (a marine) had +0x30 = 0 and squad 0's first-actor field (+0x68) held
// that actor's datum; two buggers had +0x30 = 4 and 3, squads 4 and 3 pointed
// back at them. +0x54 holds the 'char' tag datum (marine, bugger, bugger).
#define HALO2_ACTOR_SQUAD_INDEX   0x30  // int32, -1 = squadless (index into the squad array)
#define HALO2_ACTOR_CHARACTER     0x54  // datum of the 'char' tag

// Object forward vector (float[3]; checked live: unit forwards were unit length,
// horizontal, and matched the way the AI stood).
#define HALO2_OBJECT_FORWARD      0x70
