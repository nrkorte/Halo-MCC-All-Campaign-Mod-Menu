#pragma once

// halo4.dll offsets for equipment.cpp (Give equipment, Perfect camo, the
// deployed-equipment lifetime edit). MCC 1.3528.0.0, found 2026-10-12 on
// m60_rescue. "static" = read from the disassembly (tools/re/h4); "live" =
// also read from the running game. The engine functions below have NOT been
// called yet (needs a test); their entry bytes were compared with the
// running halo4.dll and match.

// --- Engine functions (game thread only) ------------------------------------
// unit_set_equipment's core is RVA 0x5FC100 (static; only caller is the
// player_set_equipment evaluator 0x92C54): it removes the current ability
// (0x5FC5B8, or 0x5FC4D4 when its flag argument is set), makes the object like
// a weapon (placement_new with owner = the unit, object_new), attaches it with
// 0x5FC26C and stamps the player's HUD. The mod copies that sequence step by
// step instead of calling it, so a tag the game can't make costs nothing.
//
// unit_remove_equipment(unit datum) at RVA 0x5FC5B8 (static): if the unit's
// armor ability (+0x664) is set, calls the detach/delete helper 0x5FECF4
// (unit, equipment, 1), clears +0x664/+0x668 to -1 and tells the HUD
// (0x5EC264, 0x37). Reach's 0x48D230 analogue.
#define OFFSET_HALO4_UNIT_REMOVE_EQUIPMENT 0x5FC5B8
// unit_attach_equipment(unit datum, equipment datum, bool) at RVA 0x5FC26C
// (static): attaches the object to the unit (0x5FBFFC), writes +0x664 (the
// ability) and +0x668 (game time), HUD update (0x5EC264, 0x37) and, for a
// player's unit (+0x49C set), the pickup bookkeeping 0x626BE8(player, equipment,
// bool). Callers: 0x5FD119 (pickup), 0x5FF82C, 0x72C520, ... The bool is the
// one unit_set_equipment's fifth script argument passes (false here). Reach's
// 0x48CEC8 analogue.
#define OFFSET_HALO4_UNIT_ADD_EQUIPMENT    0x5FC26C

// --- Unit camo (static; the idle values read live: +0x7C8 0.0, +0x7D4 -1) ----
// The camo update (RVA 0x5EC2AC, per unit): while +0x7D4 is -1 the level
// (+0x7C8) falls at +0x7D0 per second (the fade-out); otherwise it moves
// towards a speed based target (the faster the unit, the lower), then +0x7CC
// follows. player_active_camouflage_on (0x85D80) is "+0x7C8 > a small value".
// unit_set_active_camo (0x698194 -> 0x605488) writes +0x7D4 (3 = on, -1 = off)
// and the rate +0x7D0 = 1 / transition time. Reach's equivalents:
// +0x49C / +0x4A0 pinned while the ability's active handle is set.
// The camo update itself (static, .pdata bounds 0x5EC2AC..0x5EC5CA, one argument:
// the unit datum). Live log 2026-10-12: a level pinned to 1.0 before the tick
// read 0.90 again on the next tick, because this function snaps the level to the
// speed based target every tick; so Perfect camo hooks it and pins afterwards.
#define OFFSET_HALO4_CAMO_UPDATE 0x5EC2AC
#define HALO4_UNIT_CAMO_VALUE    0x7C8  // float
#define HALO4_UNIT_CAMO_PREVIOUS 0x7CC  // float
#define HALO4_UNIT_CAMO_MODE     0x7D4  // int32, -1 = camo off

// --- eqip tag (Assembly eqip.xml; the Abilities block and the tag's own
// fields were checked live on m60_rescue: Duration -1 / Charges -1 on every
// armor ability, 0 / 0 on ammo boxes and grenades) -----------------------------
#define HALO4_EQIP_SELF_DESTRUCT      0xE0   // float, object Self-Destruct Time (0 on every eqip of m60_rescue)
#define HALO4_EQIP_ABILITIES          0x2F4  // block, element 0x23C
#define HALO4_EQIP_ABILITY_SIZE       0x23C
#define HALO4_EQIP_ABILITY_CHARGES    0x44   // int16, -1 = armor abilities, 0 = ammo / grenades
#define HALO4_EQIP_PROXIMITY_MINE     0x6C   // block inside the ability, element 0x30
#define HALO4_EQIP_MINE_SIZE          0x30
#define HALO4_EQIP_MINE_SELF_DESTRUCT 0x24   // float; 0 = never destroy (Reach's drop shield bubble 15 s)
// weap tag Active Camo Ding (Assembly weap.xml; static, not read live): subtracted from the
// owner's camo level when the weapon fires. Zeroed on held weapons while Perfect camo is on.
#define HALO4_WEAP_CAMO_DING          0x4AC  // float
