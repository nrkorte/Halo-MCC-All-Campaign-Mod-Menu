#pragma once

// halo3.dll offsets for equipment.cpp (Give equipment, Perfect camo, the
// deployed-equipment lifetime edit). MCC 1.3528.0.0, found 2026-10-10.
// "static" = read from the disassembly (tools/re, text.pkl); "live" = the
// data layout was also read from the running game (070_waste). The engine
// functions below have NOT been called yet (needs a test); their entry bytes
// were compared with halo3.img live and match.

// --- Engine functions (game thread only) ------------------------------------
// unit_add_equipment(unit datum, equipment datum) at RVA 0x3574B4 (static).
// Writes unit +0x288 (carried equipment) and +0x28C (game tick), attaches the
// object to the unit (disconnects it from the map if it was free, detaches it
// from a parent), tells the HUD/player stats (0x384004). Callers: the player
// starting loadout (0x1AF214, preceded by placement_new + object_new with the
// unit as owner and a call to 0x357738), the AI starting equipment (0x55CBAC,
// the same without the cleanup call) and the pickup (0x3B7C7A, after 0x357680
// which drops the held one if allowed). This is the "unit_add_equipment" half
// of what Reach does with 0x48CEC8.
#define OFFSET_HALO3_UNIT_ADD_EQUIPMENT    0x3574B4
// unit_remove_equipment(unit datum) at RVA 0x357738 (static): if +0x288 is set,
// calls the detach/delete helper 0x3596B0(unit, equipment, 1) (mode 1 ends in
// object_delete 0x342ED8), clears +0x288/+0x28C to -1, flags the unit
// (0x1EC3C, 0x10000000) and clears the actor's cached equipment slot. The
// starting loadout code (0x1AF1C4) calls it before adding. Reach's 0x48D230
// analogue. (0x3575DC is the other remover, mode 0, used by 0x357680 when the
// carried one has no uses left; not used here.)
#define OFFSET_HALO3_UNIT_REMOVE_EQUIPMENT 0x357738

// --- eqip tag (Assembly eqip.xml, offsets checked live on 070_waste) --------
// Pickups (what a player carries) are the "<name>_equipment" tags: Charges
// (+0x194, the existing HALO3_EQIP_USES) is 1 on them and 0 on the object a use
// creates (the "Spawner" block's Spawned Object, e.g. bubbleshield). The deployed
// ones carry the lifetime: Duration and the Proximity Mine block's Self Destruct
// Time (bubbleshield 21 s / 20 s, regenerator 16 / 15, jammer 21 / 20,
// powerdrain 7 / 6.5, superflare 6 / 5, gravlift 30 / 30, tripmine 90 / 90).
#define HALO3_EQIP_DURATION              0x18C  // float seconds (live)
#define HALO3_EQIP_PROXIMITY_MINE        0x1C8  // block, element 0x30 (live: count read on every deployed tag)
#define HALO3_EQIP_MINE_SIZE             0x30
#define HALO3_EQIP_MINE_SELF_DESTRUCT    0x24   // float seconds, "0 means never destroy" (live)
// Duration written instead (seconds). Large but still a safe tick count
// (36000 s * 60 = 2.16 million).
#define HALO3_EQIP_FOREVER               36000.0f

// --- Active camo (static for the engine side, live for the layouts) ---------
// Unit camo state (update 0x350520, setters 0x35D614 / 0x35D66C / 0x35D6E4,
// ding 0x35D744): +0x360 float current level (what AI perception 0x5D2885 and
// the HS player_active_camouflage_on 0x1F13B6 read), +0x364 float target (1.0
// on the player, live), +0x36C float change per second. Unit flags (+0x170)
// bit 3 = camo switched on: the update ramps the level to the target while it
// is set and down to 0 when it isn't. The player's invisibility equipment turns
// it on and off from the per-tick handler at 0xEB9FC (reads the eqip's
// Invisibility Mode block at +0x1EC, Invisible Time + Transition Time).
#define HALO3_UNIT_CAMO_LEVEL            0x360
#define HALO3_UNIT_FLAG_CAMO_ON          0x8u   // in unit +0x170 (HALO3_UNIT_FLAGS)
// A shot subtracts the weapon's "Active Camo Ding" (weap +0x3D0) from the level
// (0x36726E, 0x35D744; player-owned shots only because of the +0x178 player
// check at 0x367252). Read live: battle_rifle 0.25, magnum 0.4, sniper 1.0,
// rocket launcher 1.0, energy sword 0.1. Damage taken (0x354780) and melee
// (0x3B0E2A) ding too; those only get the level restored afterwards.
#define HALO3_WEAP_CAMO_DING             0x3D0  // float
