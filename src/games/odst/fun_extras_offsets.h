#pragma once

// Offsets for fun_extras.cpp (launch on hit, giant/tiny AI, double jump, poison
// touch) in halo3odst.dll 1.3528.0.0. Found 2026-10-11. "static" = disassembly
// only (tools/re/odst/od.py), "live" = read from the running game (MCC was on
// an ODST map, player standing still, so only non-moving values could be read).

// object_set_scale core: (object datum in ecx, float scale in xmm1, uint16 ticks
// in r8w). Found by port.py from Halo 3's 0x39BA04 and read in od.py: it walks
// the object's children (first child +0xC, next sibling +0x8, recursing, so held
// weapons are scaled too) and calls 0x37C458, which stores the scale at object
// +0x8C and rescales the block the word at +0x124 points to. ticks = 0 means at
// once. Static only; never called yet.
#define OFFSET_ODST_OBJECT_SET_SCALE 0x3E016C
// Scale float in the object data (0x37C458 writes it at +0x8C). Live: 1.0 on the
// player's biped (static code and live agree).
#define ODST_FX_OBJECT_SCALE 0x8C

// Biped "airborne" bit: byte +0x514, bit 0x2. Static: the ODST control code
// (0x...fbfde9a / fbfd40d / fbf95f9 region, 5 sites) tests `byte [biped+0x514] & 2`
// right next to compares of the physics-mode byte +0x516, the same pattern as
// Halo 3's +0x4DC/+0x4DE (both shifted by 0x38). NOT yet seen changing live (the
// game was paused / nobody jumped while sampling); live the byte is 0 while
// standing.
#define ODST_FX_BIPED_AIRBORNE_BYTE 0x514
#define ODST_FX_BIPED_AIRBORNE_BIT  0x2
// Character physics mode byte +0x516: 1 on the live player biped (live). The ODST
// jump function (RVA 0x4025A8, ported from Halo 3's 0x3CF490 by port.py) skips
// the jump when it is 6 and reads the tag's jump velocity from bipd +0x2F0
// (ODST_BIPD_JUMP_VELOCITY), so air jumps need mode 1.
#define ODST_FX_BIPED_PHYSICS_MODE 0x516
#define ODST_FX_PHYSICS_GROUND     1
