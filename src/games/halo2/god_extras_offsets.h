#pragma once

// Offsets for god_extras.cpp (god mode vs. kill volumes). Found 2026-10-10 in
// halo2.dll 1.3528.0.0; "live" = read from the running game (Regret), "static"
// = disassembly only.

// "Is this object inside a scenario kill volume": bool(__fastcall)(uint32 object
// datum). Reads scnr +0x230 (kill trigger count, 6 on Regret, live) and, round
// robin, checks one trigger volume per call (the index is (game tick + datum) %
// count, so every volume gets looked at over a few ticks) through a "disabled"
// bit vector (kill_volume_enable/disable, 0x75F520/0x75F5A0), then tests the
// object against that volume (0x678FD0). Found from kill_volume_enable's
// evaluator (HS table); the two callers were read statically:
//   0x9292F8, the biped update: if true, sets bit 0x8000 in the biped flags
//     (+0x390) and applies a damage (0x90C0C0 / 0x90BFF0 / 0x90F4B0, damage
//     flag 4) to the unit. That damage is not stopped by "cannot take damage".
//   0x942F3F, the vehicle update: if true, ors 0x20 (unit_kill) into the damage
//     flags (+0x10A), which cannot-take-damage doesn't stop either.
// Prologue read live: 48 89 5C 24 08 57 48 83 EC 20 48 8B 1D .. (mov [rsp+8],rbx;
// push rdi; sub rsp,20h; mov rbx,[rip+x]): the first 10 bytes are position
// independent, so MinHook can relocate them. Hook not yet run in game.
#define OFFSET_HALO2_IN_KILL_VOLUME 0x75F620
