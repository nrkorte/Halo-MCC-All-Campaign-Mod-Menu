#pragma once

// Offsets for ai_upgrade.cpp (Upgrade AI, Random AI swap, Squads) in halo4.dll,
// MCC 1.3528.0.0. Kept apart from offsets.h, which other work items edit.
// Found 2026-10-12 from the HS table (tools/re/h4/hs_funcs.txt) and checked
// against the running game on m60_rescue where marked.
//
// ai_erase (evaluator 0x71224C): walks the actors of the squad (iterator
// 0x710F1C / 0x710FC8) and calls 0x731290(actor datum, bool) for each (an actor
// equal to the one currently updating, global at RVA 0xECE87C, is only flagged:
// +0x89 |= 0x80). 0x731290 reads the actor's unit (+0xC) and another datum
// (+0x14), frees the actor (0x72F5D8) and then object_delete's the unit
// (0x5D4228 = OFFSET_HALO4_OBJECT_DELETE; the bool picks 0x5D43A4 instead). So it
// is the whole erase: the unit must not be deleted separately. Static only (the
// call is untested); the shape is Reach's 0x6AA324 and Halo 3's 0x560AA8.
#define OFFSET_HALO4_ACTOR_ERASE   0x731290  // void(actor datum, bool = false)
// ai_migrate (evaluator 0x713C44 -> 0x713A58(squad a, squad b)): walks the
// actors of squad a and calls 0x732610(actor datum, int16 squad b) for each,
// then moves the squad's loose objects (which one actor doesn't need).
// 0x732610 reads the actor's old squad from actor +0x20 and the new squad's
// cell fields (squad +0x98/+0x9A, squads are 0xF0 bytes), detaches the actor
// and attaches it to squad b; -1 detaches only. Static only; Reach's 0x6AB15C.
#define OFFSET_HALO4_ACTOR_MIGRATE 0x732610  // void(actor datum, int16 squad index)

// Actor fields (0x1380 each, TLS slot 774; HALO4_ACTOR_UNIT 0xC is in offsets.h).
// Checked live on m60_rescue (34 actors): +0x20 is the squad index (203, 207,
// 145...; squads are 0xF0 each in TLS slot 9, 256 high-water, and +0x24 holds
// the same number; ai_migrate's 0x732610 reads +0x20), and the character tag
// datum is at +0x48 (storm_pawn_prime, storm_pawn_sniper, storm_knight_ranger:
// the exact variant the actor runs); +0x2C is the same field one parent up
// (storm_pawn for all three pawn variants), the way Reach has +0x6C and +0x4C.
// The team is at +0x88 and equals the unit's team (+0x498) in every actor read
// (8 = forerunner, the Prometheans). The squadless spawns the mod makes weren't
// on the map when this was read, so a -1 there is assumed from the other
// titles.
#define HALO4_ACTOR_SQUAD_INDEX    0x20  // int32, -1 = squadless (index into TLS slot 9)
#define HALO4_ACTOR_CHARACTER      0x48  // datum of the 'char' tag
#define HALO4_ACTOR_BASE_CHARACTER 0x2C  // datum of the base 'char' (a preset variant's parent; see above)
