#pragma once

// Offsets for fun_extras.cpp (launch on hit, giant/tiny AI, double jump, poison
// touch). Found 2026-10-12 in halo4.dll 1.3528.0.0 (m60_rescue, running).

// object_set_scale's core, HaloScript evaluator RVA 0x683544 in hs_funcs.txt:
// (object datum in ecx, float scale in xmm1, uint16 ticks in r8w). Same shape as
// Reach's 0x4D9B38: it recurses over the object's children (first child +0x20,
// next sibling +0x1C, via the self-call at 0x6835DC, so held weapons scale with
// it) and calls 0x5D144C per object, which stores the scale at object +0xA4
// (target) and, for ticks = 0, +0xA0 (current), then 0x5D0F38 to refit the
// bounds. Static only; the call itself is untested.
#define OFFSET_HALO4_OBJECT_SET_SCALE 0x683544
// The scale the object is heading for (written by 0x5D144C at 0x5D1494); the
// current one sits at +0xA0. Live: 1.0 / 1.0 on every biped in m60_rescue.
#define HALO4_FX_OBJECT_SCALE_TARGET 0xA4
