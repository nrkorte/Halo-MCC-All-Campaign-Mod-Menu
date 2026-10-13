#pragma once

// Halo 3 offsets for vehicles.cpp (AI-only seats, take pilot seat, upgrade
// vehicle, stuck-seat exit). All found statically 2026-10-10 with
// tools/re (hsfunc.py: evaluator = definition + 0x20) and checked against the
// running game where noted. Halo 3 has no unit_enter_vehicle_immediate like
// Reach's, so the seating goes through the unit action queue instead.

// Unit action queue: (unit datum, action struct*) -> bool. Calls the action's
// handler from the table at RVA 0x814E60 (type * 8) right away (RVA 0x379C44;
// the type-only wrapper 0x379C08 is OFFSET_HALO3_UNIT_ACTION_QUEUE in
// offsets.h). Static.
#define OFFSET_HALO3_UNIT_ACTION_QUEUE_STRUCT 0x379C44

// The action unit_enter_vehicle (HS opcode 0xEE, evaluator RVA 0x1E562C ->
// 0x3A39E0) queues once its checks pass (RVA 0x3A3B13..0x3A3B44):
//   +0x0 dword 0x11, +0x4 dword vehicle datum, +0x8 word seat index,
//   +0xA word 0x0100 (byte +0xB = 1: "magic"), struct size 0x34, rest zero.
// Handler 0x3B368C -> transition 0x1A (0x3DEDD0): with the magic byte set it
// skips the "does this unit have the seat's animation" check (0x38D5DC) and
// 0x38E598 skips the can-enter test (0x353774). unit_enter_vehicle itself
// first asks 0x3523E0(unit, vehicle, seat), which fails for seats whose
// animation the unit's graph lacks (live, 070_waste: the Master Chief's graph
// has no pelican/phantom driver animation, 0 of 42 phantom seats), so the mod
// queues the action itself and skips that test. Static.
#define HALO3_ACTION_ENTER_VEHICLE      0x11
#define HALO3_ACTION_STRUCT_SIZE        0x34
#define HALO3_ACTION_ENTER_VEHICLE_SEAT 0x8     // int16
#define HALO3_ACTION_ENTER_VEHICLE_MAGIC 0xA    // uint16 0x0100

// objects_detach(parent, child) (HS opcode 0x6A, evaluator RVA 0x1E35F0)
// checks the child's parent, then calls 0x3448E0(child datum): the object
// type's detach callback (a unit leaves its seat), then the generic detach.
// The same shape as Reach's 0x47133C. Static.
#define OFFSET_HALO3_OBJECT_DETACH      0x3448E0

// The unit in a vehicle's seat: (vehicle datum, seat index) -> datum or -1.
// Walks the vehicle's children (first child +0xC, next sibling +0x8) for a
// unit whose seat (+0x24E) matches; called by unit_enter_vehicle at 0x3A3AE9
// before taking a seat. Same job as Reach's 0x486290. Static.
#define OFFSET_HALO3_SEAT_OCCUPANT      0x353384

// Vehicle seat flags (vehi.xml, seat block at +0x258, element 0xD4).
// "Invalid For Player" is bit 16 here (Reach: 14). Checked live on 070_waste:
// pelican seat 0 = 0x18414, phantom seat 0 = 0x18415 (also Invisible), all
// other Phantom/Pelican seats, the troop carrier's and the Scorpion's four
// passenger seats carry it too; Banshee/Ghost/Warthog/Wraith drivers don't.
#define HALO3_SEAT_FLAG_INVISIBLE        0x1u      // "completely enclosed by vehicle"
#define HALO3_SEAT_FLAG_DRIVER           0x4u
#define HALO3_SEAT_FLAG_INVALID_FOR_PLAYER 0x10000u
