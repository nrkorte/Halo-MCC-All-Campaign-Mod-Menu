#pragma once

// Offsets for fun_extras.cpp (launch on hit, giant/tiny AI, double jump, poison
// touch). Found 2026-10-10 in halo3.dll 1.3528.0.0 (070_waste, running);
// "live" = read from the running game, "static" = disassembly only.

// object_set_scale's core: evaluator (HS opcode 0x06B, definition 0x7EDBF0,
// evaluator pointer 0x1E365C) reads the three script args and calls
// 0x39BA04(object datum in ecx, float scale in xmm1, uint16 ticks in r8w). It
// walks the object's children (first child +0xC, next sibling +0x8, recursing
// itself) so held weapons are scaled by the one call, and on each object calls
// 0x341028, which stores the scale at object +0x8C right away, rescales a block
// referenced by the word at +0x130, and fixes the bounds. ticks = 0 means at
// once. Static only; the call itself is untested.
#define OFFSET_HALO3_OBJECT_SET_SCALE 0x39BA04
// Scale float in the object data (0x341028 writes it at +0x8C). Live: 1.000 on
// every biped and vehicle in the table (070_waste).
#define HALO3_FX_OBJECT_SCALE 0x8C

// Biped "airborne" bit: byte +0x4DC, bit 0x2. The control code at 0x3CF05D and
// the jump-related test at 0x3CFAB1 both test it, and the jump function itself
// (0x3CF490: vel += (tag jump velocity at bipd +0x2C8 - dot(vel, up)) * up, then
// the set-velocity core 0x3411A4) is the one the mod's own jump boost copies.
// Live (sampled at 50 Hz while the user jumped): clear while standing or
// walking on level floor, set from the tick of take-off (vz = 3.01 = the tag's
// jump velocity) until the instant of landing, for five full jumps of
// 1.4-1.5 s each (the airtime of a 3.0 jump at gravity 4.17). It also flickers on
// for a few ticks on slopes/steps; harmless here.
#define HALO3_FX_BIPED_AIRBORNE_BYTE 0x4DC
#define HALO3_FX_BIPED_AIRBORNE_BIT  0x2
// Character physics mode byte +0x4DE: 1 on every living biped (live); the jump
// function skips the jump when it is 6 (seen live for a few ticks next to a
// ledge/climb), so air jumps need 1 as well.
#define HALO3_FX_BIPED_PHYSICS_MODE  0x4DE
#define HALO3_FX_PHYSICS_GROUND      1
