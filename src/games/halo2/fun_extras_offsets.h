#pragma once

// Offsets for fun_extras.cpp (launch on hit, giant/tiny AI, double jump, poison
// touch). Found 2026-10-10 in halo2.dll 1.3528.0.0; "live" = read from the
// running game (Regret), "static" = disassembly only.

// object_set_scale's core (evaluator 0x783F20 -> 0x90A410), found with
// tools/re/h2 deep.py. (object datum, float scale in xmm1, int16 ticks in r8w);
// ticks 0 = instantly. It recurses over the object's children (a unit's held
// weapons follow), then 0x8DB5E0 stores the scale and rescales the object's
// bounds. Static only; the call itself is untested.
#define OFFSET_HALO2_OBJECT_SET_SCALE 0x90A410
// Scale float in the object data (0x8DB5E0 writes it at +0xA0). Live: 1.000 on
// every object in the table (Regret).
#define HALO2_FX_OBJECT_SCALE 0xA0

// unit_kill's core (evaluator 0x784FE0 -> 0x8FAF30(object datum)): ors 0x20 into
// the damage flags (+0x10A) and wakes the object. Static only; the mod's damage
// multiplier does the same flag write by hand (live-tested).
#define OFFSET_HALO2_UNIT_KILL_CORE 0x8FAF30

// Biped state. Word at +0x390: the biped post-physics update (0x929300..0x929A30)
// clears bit 0x1000 every tick and sets it again when the character physics
// found a ground surface (it then also copies the ground point and normal to
// +0x3B0/+0x3BC). Fall damage and the landing code read it the same way.
// Live: 0x1000 on every living biped standing on the floor, 0 on corpses;
// whether it really clears in mid-air is read from the code, not seen live.
#define HALO2_FX_BIPED_FLAGS         0x390  // word
#define HALO2_FX_BIPED_FLAG_GROUND   0x1000u
// Byte at +0x428: character physics mode (switch in 0x751F20, 0x92B448): 1 =
// ground (every living biped, live), 3 = dead (every corpse, live). The jump
// test (0x927B70) accepts 1 or 3.
#define HALO2_FX_BIPED_PHYSICS_MODE  0x428
#define HALO2_FX_PHYSICS_GROUND      1
