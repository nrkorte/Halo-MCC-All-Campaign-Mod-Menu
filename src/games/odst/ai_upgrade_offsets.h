#pragma once

// Offsets for ai_upgrade.cpp (Upgrade AI, Random AI swap, Squads) in
// halo3odst.dll. Kept apart from offsets.h, which other work items edit.
// Found 2026-10-11 by porting Halo 3's functions with tools/re/odst/port.py (masked bytes) and
// confirming each through ODST's own HaloScript evaluators (tools/re/odst/hs.py, od.py). The actor
// fields were read live on sc120 (tools/re/odst/livechars.py), the two functions are static only
// (never called while ODST ran; engine calls can't be made from outside the game thread).
//
// ai_migrate (hs evaluator 0x21A680) -> 0x5BBE28(squad a, squad b) walks the actors of squad a
// and calls 0x5AC060(actor datum, int16 squad b) for each (call sites 0x5BBD3F / 0x5BBE9F). That
// per-actor step: detaches the actor from its squad/cell (-1 detaches only), attaches it to
// squad b (0x614210(actor, squad, 1)) and gives it the squad's fire team. Same signature as
// Halo 3's 0x5617AC. Actors are 0xAB0 bytes, squads 0xE8 (TLS slot 10 as in Halo 3).
#define OFFSET_ODST_ACTOR_MIGRATE 0x5AC060  // void(actor datum, int16 squad index); -1 detaches only
// ai_erase (hs evaluator 0x21A274) -> 0x5BAFB4(squad): for each actor of the squad calls
// 0x5AB2CC(actor datum, bool). It reads the unit (actor +0x14) and the actor's second datum
// (+0x18), detaches the actor (0x5A98FC), then object_delete's the unit (0x37E4A4 =
// OFFSET_ODST_OBJECT_DELETE; the bool picks 0x37E650 instead) or, with no unit, erases the
// leftover (0x5FD1AC). So it is the whole erase; the unit must not be deleted separately.
#define OFFSET_ODST_ACTOR_ERASE   0x5AB2CC  // void(actor datum, bool = false)

// Actor fields (0xAB0 each, TLS slot 1; ODST_ACTOR_UNIT 0x14 is in offsets.h).
// Read live on sc120: +0x58 holds the 'char' tag datum (jackal_major, marine, marine_female,
// brute_major, brute_ultra, jackal_sniper matched by tag name), +0x30 the squad index (-1
// = squadless; 0x17 / 0x1B... for the mission's squads) and +0x20 the actor's team (7 =
// Covenant, 2 = human there). Halo 3 has the character at +0x5C and the squad at +0x30.
#define ODST_ACTOR_SQUAD_INDEX    0x30  // int32, -1 = squadless (index into TLS slot 10); also read at 0x5AC102
#define ODST_ACTOR_CHARACTER      0x58  // datum of the 'char' tag
