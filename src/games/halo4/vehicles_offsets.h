#pragma once

// Halo 4 offsets for vehicles.cpp (AI-only seats, take pilot seat, upgrade
// vehicle, stuck-seat exit). All found statically 2026-10-12 by porting
// Reach's functions (reach/offsets.h, "vehi seats") and read against the
// running game (m60_rescue) where noted. Halo 4's unit_enter_vehicle has
// Reach's shape (unlike Halo 3's), so the seating goes through it with the
// "immediate" flag, as in Reach.

// unit_enter_vehicle(unit, vehicle, seat name (string id), bool immediate):
// the core under the HaloScript commands (unit_enter_vehicle's evaluator
// 0x699128 is `xor r9d, r9d; jmp 0x698F54`, unit_enter_vehicle_immediate's
// 0x699130 sets r9b to 1 first; Reach's 0x4F97BC has the identical body).
// It finds the vehicle's seats whose name matches (0x1806063BC lists them,
// attached turrets included), skips occupied ones (0x5F4D68) and, unless
// immediate, asks 0x5F323C whether the unit may enter; then queues action
// 0x10 {+4 vehicle, +8 int16 seat, +0xA byte immediate, +0xB byte 7} through
// 0x62C5DC. Immediate skips the can-enter test (which is where "Invalid For
// Player" and missing seat animations stop a player). A seated unit is
// ejected first (action 0x19). Static only, not yet called.
#define OFFSET_HALO4_UNIT_ENTER_VEHICLE 0x698F54

// The unit in a vehicle's seat: (vehicle datum, seat index) -> datum or -1.
// Walks the vehicle's children (first child +0x20, next sibling +0x1C) for
// the one whose parent seat (+0x2C) matches; what unit_enter_vehicle calls
// before taking a seat (0x69908F). Reach's 0x486290. Static (the loop was
// read; no vehicle was in the live map to try it on).
#define OFFSET_HALO4_SEAT_OCCUPANT      0x5F4D68

// objects_detach(parent, child)'s engine call (evaluator 0x6834F4 checks the
// child's parent, then calls this with the child): the object type's detach
// callback (a unit leaves its seat), then the generic detach 0x5D6FB0.
// Same shape as Reach's 0x47133C. Static.
#define OFFSET_HALO4_OBJECT_DETACH      0x5D6F10

// Vehicle seat flags (vehi seat block at +0x510, element 0x16C; Assembly's
// Halo4MCC plugin). Same bits as Reach. Checked live on m60_rescue: the
// Phantom's pilot seat is 0x421F (Invisible, Locked, Driver, Gunner, third
// person camera and Invalid For Player, bit 14), its other seats 0x2004000
// and similar; the Banshee's pilot seat 0x440001C has no bit 14, nor does the
// Pelican's 0x25D (so a player may already fly a Pelican there).
#define HALO4_SEAT_FLAG_INVISIBLE          0x1u      // "completely enclosed by vehicle"
#define HALO4_SEAT_FLAG_DRIVER             0x4u
#define HALO4_SEAT_FLAG_INVALID_FOR_PLAYER 0x4000u   // bit 14
// The seat's "seat animation" string id: what unit_enter_vehicle matches
// (cmp at 0x698F86 reads +8 of the seat definition, Reach's +4). Checked
// live: distinct per seat (Phantom 0x35B1, 0x35B2, ...; Banshee 0x32DC,
// 0x32DF, ...).
#define HALO4_SEAT_NAME                    0x8
