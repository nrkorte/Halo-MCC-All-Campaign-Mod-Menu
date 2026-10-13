#pragma once

// Halo 4 offsets used only by spawn.cpp's shared (internal.h) functions.

// Actor team byte: actor_set_team (RVA 0x72E820) writes the new team to
// actor + 0x88 (`mov byte ptr [rbx+0x88], r10b`). Static; not read live.
#define HALO4_ACTOR_TEAM 0x88

// actor_set_team's branch on `test byte ptr [actor+0x89], 2` (RVA 0x72E877):
// with the bit clear it writes the unit's team (unit +0x498, word team|0xFF00)
// itself (0x72E91D); with it set it takes another path (a walk over the
// actor's props) that never writes the unit's team. A freshly spawned,
// activated actor is the second kind in Halo 3 (where this bug was seen), so
// the mod writes the unit's team byte (HALO4_UNIT_TEAM) too. Static.
#define HALO4_ACTOR_TEAM_FLAGS_BYTE 0x89
