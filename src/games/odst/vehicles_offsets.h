#pragma once

// ODST offsets for vehicles.cpp (AI-only seats, take pilot seat, upgrade
// vehicle, stuck-seat exit). All found statically 2026-10-11 by reading
// halo3odst.dll with tools/re/odst (od.py, port.py, hs.py); none was called
// or checked live yet. Seat/vehicle tag layouts are in odst/offsets.h
// (ODST_VEHI_SEATS 0x278 element 0xD4, ODST_UNIT_SEAT 0x262, ...), checked
// against tools/re/odst/xml/vehi.xml.

// Unit action queue: (unit datum, action struct*) -> bool. Looks the handler
// up in the table at RVA 0x857070 (type * 8; the entry's first qword is the
// handler) and runs it right away. 0x3C0E60 is the struct version, 0x3C0E24
// (OFFSET_ODST_UNIT_ACTION_QUEUE in offsets.h) the type-only wrapper; port.py
// maps Halo 3's 0x379C44/0x379C08 to them and the code matches (movsxd of
// [rdx], handler table lookup). Static.
#define OFFSET_ODST_UNIT_ACTION_QUEUE_STRUCT 0x3C0E60

// The action unit_enter_vehicle (evaluator RVA 0x2183E0 -> 0x3C5754(unit,
// vehicle, seat label string id)) queues at 0x3C58A8 once its checks pass:
//   +0x0 dword 0x11, +0x4 dword vehicle datum, +0x8 word seat INDEX,
//   +0xA byte flags (unit_enter_vehicle passes 2), struct memset 0x2C.
// Differences from Halo 3: the struct is 0x2C (Halo 3 0x34) and the two flag
// bytes Halo 3 keeps at +0xA / +0xB are the two low bits of the single byte at
// +0xA here (handler 0x3FA32C: bit 0 -> transition byte +0x12, bit 1 ->
// transition byte +0x1, the "magic" byte; it also requires the seat's label
// (seat +0x4) to be nonzero and not -1). Transition 0x1A's handler 0x424E30
// reads that byte ([rdx+1]) and passes it to 0x3D1D1C, which then skips the
// animation check 0x3CF7B0 (Halo 3 0x38D5DC), and on to 0x3D11B4 (Halo 3's
// 0x38E598), which skips the can-enter test 0x397E48 (Halo 3's 0x353774).
// unit_enter_vehicle's own pre-check, 0x396AAC (Halo 3's 0x3523E0), asks
// 0x3CEC14 whether the unit's graph has the seat's animation, so the mod
// queues the action itself with the magic bit set: bit 1, so byte 2. Static.
#define ODST_ACTION_ENTER_VEHICLE        0x11
#define ODST_ACTION_STRUCT_SIZE          0x40    // oversized; the real size is 0x2C
#define ODST_ACTION_ENTER_VEHICLE_SEAT   0x8     // int16 seat index
#define ODST_ACTION_ENTER_VEHICLE_FLAGS  0xA     // uint8
#define ODST_ACTION_ENTER_VEHICLE_MAGIC  0x02    // bit 1: skip the animation and can-enter tests

// objects_detach(parent, child) (HS evaluator RVA 0x215E10, found by name
// with hs.py) checks the child's parent, then calls 0x3800AC(child datum): the
// object type's detach callback (a unit leaves its seat), then the generic
// detach (0x38014C). Same shape as Halo 3's 0x3448E0. Static.
#define OFFSET_ODST_OBJECT_DETACH        0x3800AC

// The unit in a vehicle's seat: (vehicle datum, seat index) -> datum or -1.
// Walks the vehicle's children (first child +0xC, next sibling +0x8) for a
// biped/vehicle whose seat (+0x262) matches; unit_enter_vehicle calls it at
// 0x3C5850 before taking a seat. Halo 3's 0x353384. Static.
#define OFFSET_ODST_SEAT_OCCUPANT        0x397A58

// Vehicle seat flags (vehi.xml, seat block at +0x278, element 0xD4): the same
// bits as Halo 3 ("Invalid For Player" is bit 16).
#define ODST_SEAT_FLAG_INVISIBLE         0x1u      // "completely enclosed by vehicle"
#define ODST_SEAT_FLAG_DRIVER            0x4u
#define ODST_SEAT_FLAG_INVALID_FOR_PLAYER 0x10000u
