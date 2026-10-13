#pragma once

// halo3odst.dll offsets for equipment.cpp (Give equipment, Perfect camo, the
// deployed-equipment lifetime edit). MCC 1.3528.0.0, found 2026-10-11.
// "static" = from the disassembly (tools/re/odst); "live" = also read from the
// running game (sc120). The engine functions below have NOT been called yet
// (needs a test); their code shape was compared with Halo 3's.
//
// ODST's campaign does have equipment: the map carries 36 'eqip' tags (live,
// sc120: bubbleshield, regenerator, instantcover, invincibility, jammer,
// powerdrain, superflare, tripmine, autoturret, gravlift, invisibility, health
// packs, ...) and unit_add_equipment (HS evaluator 0x2192B0) exists. The unit
// has the slot at +0x29C (ODST_UNIT_EQUIPMENT).

// --- Engine functions (game thread only) ------------------------------------
// unit_add_equipment(unit datum, equipment datum) at RVA 0x39BBF0 (static;
// ported from Halo 3's by masked bytes, then read: writes unit +0x29C
// (carried) and +0x2A0 (game tick), detaches from the map, tells the HUD).
// Called by the player starting loadout at 0x1DDD88 right after
// placement_new(0x37C954)(placement, tag, owner = unit, 0) + object_new
// (0x37CF34) + 0x20B80(obj) and the remove call 0x1DDD38. (The HS function
// "unit_add_equipment" 0x2192B0 takes a starting profile instead and calls
// 0x39C3F8; not used.)
#define OFFSET_ODST_UNIT_ADD_EQUIPMENT    0x39BBF0
// unit_remove_equipment(unit datum) at RVA 0x39BE74 (static): if +0x29C is set,
// detach/delete helper 0x39E240(unit, equipment, 1), clears +0x29C/+0x2A0 to
// -1, clears the player's cached slot. The starting loadout calls it first.
#define OFFSET_ODST_UNIT_REMOVE_EQUIPMENT 0x39BE74

// --- eqip tag (Assembly eqip.xml, values read live on sc120) ----------------
// Pickups (what a player carries) are the "<name>_equipment" tags: Charges
// (+0x1A4 = ODST_EQIP_USES) is 1 on them and 0 on the object a use creates.
// Deployed ones carry the lifetime (live: bubbleshield 21 s / 20 s, regenerator
// 16 / 15, jammer 21 / 20, powerdrain 7 / 6.5, superflare 6 / 5, gravlift
// 30 / 30, tripmine 90 / 90; same numbers as Halo 3). Duration is Halo 3's
// +0x18C field (named "Cooldown Time" in ODST's Assembly plugin).
#define ODST_EQIP_DURATION              0x19C  // float seconds (live)
#define ODST_EQIP_PROXIMITY_MINE        0x1D8  // block, element 0x30 (live)
#define ODST_EQIP_MINE_SIZE             0x30
#define ODST_EQIP_MINE_SELF_DESTRUCT    0x24   // float seconds, "0 means never destroy" (live)
#define ODST_EQIP_FOREVER               36000.0f

// --- Active camo ------------------------------------------------------------
// Same mechanic as Halo 3: the ding function (RVA 0x3A25A0, ported from
// Halo 3's 0x35D744; static) subtracts the shot's ding from the level at unit
// +0x374 and, when unit flag bit 3 is set and the level is below 0.05, resets
// it to 0.05. HS player_active_camouflage_on (0x2248F6) reads the level too.
// Live (sc120): level +0x374 = 0, target +0x378 = 1.0, flags +0x168 = 0x3.
#define ODST_UNIT_CAMO_LEVEL            0x374
#define ODST_UNIT_FLAG_CAMO_ON          0x8u   // in unit +0x168 (ODST_UNIT_FLAGS)
// weap "Active Camo Ding": +0x3E0 (Assembly; live: smg_silenced 0.18,
// rocket_launcher 1.0).
#define ODST_WEAP_CAMO_DING             0x3E0  // float
