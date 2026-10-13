#pragma once

// halo4.dll offsets for MCC 1.3528.0.0, found 2026-10-02 (statically, and
// checked live in Midnight, m90_sacrifice, where marked). "RVA" = address in
// halo4.dll relative to its base. Halo 4's engine is Reach's, but the layouts
// all moved, so every value here was re-found.
//
// Halo 4's HaloScript function definitions aren't in the DLL image: they're
// C++ objects built on the heap at startup (vtable +0, return type +8, name
// +0x10, evaluator +0x60). tools/re/h4/hsdump.py reads them from a running
// game into hs_funcs.txt (name -> evaluator RVA), which is where most of the
// "evaluator" RVAs below come from. Tag layouts are from Assembly's Halo4MCC
// plugins (tools/re/h4/xml).

// --- Hooks --------------------------------------------------------------

// Main loop: runs on the game thread per session (AlphaRing's
// OFFSET_HALO4_PF_ENGINE). Only used to grab that thread's TLS block.
#define OFFSET_HALO4_ENGINE 0x616A8
// One game tick: called once per tick from the tick loop (RVA 0x9A4A9; the
// tick adds to game time at 0x9ADC8). AlphaRing's OFFSET_HALO4_PF_WORLD.
#define OFFSET_HALO4_GAME_TICK 0x9A51C
// Checkpoints. game_save_immediate calls 0x194DBC(3) (RVA 0x194BE8; game_save
// passes 1); game_revert (RVA 0x62310) writes 0x0C01 to this word, a revert
// request the main loop acts on, as in Reach.
#define OFFSET_HALO4_GAME_SAVE        0x194DBC
#define HALO4_GAME_SAVE_IMMEDIATE     3
#define OFFSET_HALO4_GAME_REVERT_FLAG 0x293DEAB
#define HALO4_GAME_REVERT_REQUEST     0x0C01
// Barrel fire(weapon datum, int16 barrel, bool): fires one barrel; it uses up
// the magazine (0x615668) and adds heat (0x615EF8). Called by the weapon
// update (RVA 0x60A2D7). Hooked to stop AI firing.
#define OFFSET_HALO4_BARREL_FIRE 0x614FC0
// Barrel fire only marks the barrel as fired (barrel state +4 bit 1, weapon
// +0x490 bit 0); the projectile is made after the objects update by 0x607630 ->
// 0x60768C -> 0x6176B8, which looks the tag up through this:
// uint32* projectile_of(uint32* out, weapon datum, int16 barrel) reads the
// barrel's projectile datum (+0x11C) and applies Scaleshot (weap +0x524). Also
// used by two queries (0x60B270, 0x61970F). Found statically 2026-10-04.
#define OFFSET_HALO4_BARREL_PROJECTILE_OF 0x6826E0
// object_set_velocity's core (evaluator 0x68376C -> 0x683608 local-space ->
// here): (object, const float* linear world or null, const float* angular or
// null); writes +0x88/+0x94, updates the physics (0xF1ADC) and wakes the
// object. Reach's 0x46CD64.
#define OFFSET_HALO4_OBJECT_SET_VELOCITY 0x5D1580
// Active skulls: u64 at game globals (TLS slot 8) +0x2CF78, bit = script
// skull enum (iron 0 ... blue 16); skull_enable (0x9C2F8) then calls
// 0x64D860(mask*), a Black Eye shield refresh. Copied from the game options
// at game start (0x99CB0), so the mod re-applies it.
#define HALO4_GAME_GLOBALS_SKULLS 0x2CF78
#define OFFSET_HALO4_SKULLS_CHANGED 0x64D860
#define HALO4_SKULL_COUNT 17
// Flip ejection (found statically 2026-10-02, the same shape as Halo 3's):
// the unit update 0x62EA4C compares the unit's ticks upside down (int8 at
// unit +0xF4A) with its seat's turnover time (vehi seat +0x30) and, once
// past it, sends unit action 0x19 through 0x62C598(unit, action) at
// 0x62EB92. 0x62C598 has ~90 other callers, so the hook only drops 0x19 for
// players when it returns to 0x62EB97.
#define OFFSET_HALO4_UNIT_ACTION      0x62C598
#define HALO4_FLIP_EJECT_RETURN       0x62EB97
#define HALO4_UNIT_ACTION_FLIP_EJECT  0x19
// Following (third-person) camera update, vtable[2] of vtable RVA 0xD4EF38
// (director_set_camera_mode 0x10BD98, mode 0): update(camera*, player index,
// float dt, observer command*). With the third-person patch the director
// runs this camera (checked live).
#define OFFSET_HALO4_FOLLOWING_CAMERA_UPDATE 0x2927B0
// Observer command: distance back from the focus +0x24 (the update passes
// &command->distance to its helper at RVA 0x2928D3), as in Reach.
#define HALO4_OBSERVER_COMMAND_DISTANCE 0x24

// --- Engine functions (call on the main thread only) ---------------------
// object_placement_data_new(placement*, tag datum, owner object, 0): clears
// 0x2B0 bytes (callers e.g. RVA 0x5FD03E, then object_new).
#define OFFSET_HALO4_OBJECT_PLACEMENT_NEW 0x62303C
// object datum object_new(placement*)  (RVA 0x5FD047)
#define OFFSET_HALO4_OBJECT_NEW           0x5D2034
// object_delete(object datum)  (object_destroy's evaluator 0x2845A0 -> 0x283730)
#define OFFSET_HALO4_OBJECT_DELETE        0x5D4228
// bool unit_add_weapon(unit, weapon, mode): unit_add_weapon's evaluator
// (0x5FD69C) checks the objects and calls this. Mode 1 as in Reach.
#define OFFSET_HALO4_UNIT_ADD_WEAPON      0x5FD704
#define HALO4_ADD_WEAPON_FREE_SLOT        1
// unit_kill(unit, 0, true, 2, 0): what the unit_kill script passes (evaluator 0x6981B8)
#define OFFSET_HALO4_UNIT_KILL            0x5E5AD8
// object_set_position(object, position*, forward*, up*, 0, false): the last
// step of object_teleport (0x284A10, call at 0x284C05), same as Reach's.
#define OFFSET_HALO4_OBJECT_SET_POSITION  0x5D1350
// unit_get_head_position(unit, float out[3]): marker "head" position
// (objects_can_see_object, RVA 0x283D23)
#define OFFSET_HALO4_UNIT_HEAD_POSITION   0x5F8870
#define HALO4_PLACEMENT_DATA_SIZE         0x400  // real size is 0x2B0; oversized buffer

// Placement fields, as in Reach (the actor spawn's unit placement, RVA
// 0x72DAB9..0x72DB26): variant name string id, position, forward, up.
#define HALO4_PLACEMENT_VARIANT           0xC
#define HALO4_PLACEMENT_POSITION          0x1C
#define HALO4_PLACEMENT_FORWARD           0x28
#define HALO4_PLACEMENT_UP                0x34

// --- Spawning (found 2026-10-04 on m60_rescue) -------------------------------
// The ray test takes a query struct, unlike Reach's argument list:
// bool collision_test(query*, result*) (RVA 0x1C1D4C, 154 callers; found
// by porting Reach's callers at 0x50DA02 and 0x5414DB). Query: +0 int32 kind
// (0x1A at 70 of the callers, including the one ported from Reach), +4 u64
// flags (that caller loads the global at RVA 0x2FFB118, 0x100001049 live),
// +0xC start point, +0x18 end point, +0x24 int32[64] objects to ignore with
// their count in the byte at +0x124, +0x128 int16 0 (RVA 0x6F1D88..0x6F1E34).
// Result: fraction +4 (1 = no hit), point +8 (set up by 0x1C12A8).
#define OFFSET_HALO4_COLLISION_TEST       0x1C1D4C
#define OFFSET_HALO4_COLLISION_FLAGS      0x2FFB118  // u64
#define HALO4_COLLISION_KIND              0x1A
#define HALO4_QUERY_KIND                  0x0
#define HALO4_QUERY_FLAGS                 0x4
#define HALO4_QUERY_START                 0xC
#define HALO4_QUERY_END                   0x18
#define HALO4_QUERY_IGNORE                0x24
#define HALO4_QUERY_IGNORE_COUNT          0x124
#define HALO4_QUERY_SIZE                  0x200  // oversized buffer
#define HALO4_COLLISION_RESULT_FRACTION   0x4
#define HALO4_COLLISION_RESULT_SIZE       0x200  // oversized buffer
// AI, as in Reach: the squadless spawn (Reach's 0x73152C ported, RVA
// 0x7C99C4) builds a request (0x7C9BA4: init 0x72D308, 0x74 bytes; the
// fields are Reach's + 4), then actor_spawn(int16 -1, request) (0x72D434)
// and actor_set_active(actor, true) (0x72E9B4). With no weapons in the
// request (+0x2C/+0x30, tag datums), the weapon giver (0x72C140) uses the
// character's own (RVA 0x72C298).
#define OFFSET_HALO4_ACTOR_REQUEST_INIT   0x72D308
#define OFFSET_HALO4_ACTOR_SPAWN          0x72D434
#define HALO4_ACTOR_REQUEST_POSITION      0x0
#define HALO4_ACTOR_REQUEST_C             0xC    // int32 -1, then +0x10 -1
#define HALO4_ACTOR_REQUEST_YAW           0x14
#define HALO4_ACTOR_REQUEST_PITCH         0x18
#define HALO4_ACTOR_REQUEST_1C            0x1C   // 0
#define HALO4_ACTOR_REQUEST_FLAGS         0x20
#define HALO4_ACTOR_REQUEST_FLAG_FREE     0x8
#define HALO4_ACTOR_REQUEST_22            0x22   // 2
#define HALO4_ACTOR_REQUEST_26            0x26   // 1
#define HALO4_ACTOR_REQUEST_CHARACTER     0x28
// actor_set_team(actor, team): actor +0x88 and its unit's team (+0x498)
// (what ai_set_team does per actor, RVA 0x715F73). Actors: TLS slot 774,
// 0x1380 each, unit +0xC.
#define OFFSET_HALO4_ACTOR_SET_ACTIVE     0x72E9B4
#define OFFSET_HALO4_ACTOR_SET_TEAM       0x72E820
#define HALO4_ACTOR_UNIT                  0xC
// Designer zones, as in Reach but 64 masks wide (Halo 4 missions have up
// to 51). No script command loads them; cinematic_zone_activate (RVA
// 0x6231C) shows the layout: its pending cinematic masks (0x293DEE0/E4)
// follow the designer ones, 64-bit deactivate 0x293DED0 and activate
// 0x293DED8, which it checks before setting the shared update flag (byte
// 0x293DEB6). The active designer mask is the u64 at 0x275D5D0 (it equals
// the current zone set's runtime designer mask). Tested live on m60_rescue:
// writing activate = zone 9 and the flag loaded it in under 0.3 s.
#define OFFSET_HALO4_DESIGNER_ZONES_ACTIVE   0x275D5D0  // u64
#define OFFSET_HALO4_DESIGNER_ZONES_PENDING  0x293DED0  // u64 deactivate, u64 activate
#define OFFSET_HALO4_ZONE_UPDATE_PENDING     0x293DEB6  // byte flag

// --- Code patches (found by byte pattern) ---------------------------------
// From the user's Cheat Engine table (all unique on Steam 1.3528).
// Third person: the player's camera picks the following camera when the held
// weapon's tag asks for it (weap +0x2A2 bit 0); the je is NOPed so it always
// does. Checked live: the director then runs the mode-0 following camera.
#define HALO4_AOB_THIRD_PERSON    "74 03 44 89 2F 41"  // RVA 0x10D407; 2 bytes NOP
// Rapid fire: the barrel's fire-delay counter (barrel +2, weapon +0x51E) is
// never set after a shot. Every weapon (AI too).
#define HALO4_AOB_RAPID_FIRE      "66 89 AF 1E 05 00 00"  // RVA 0x616E2C; 7 bytes NOP
// Rapid fire and Halo 4's weapon tuning (title update balance): the barrel's
// fire recovery comes from the game variant's per-weapon-type tuning table
// when the type has a rate-of-fire override (0x61CB9C; the AR has one), and
// the tag's own recovery/rounds per second are ignored. The je that picks
// the tag's values when there's no override becomes jmp while rapid fire is
// on, so the rapid-fire tag edits apply to every weapon (found 2026-10-02:
// the AR didn't speed up).
#define HALO4_AOB_FIRE_TUNING     "41 C0 E8 06 41 F6 C0 01 74 4B"  // RVA 0x61CBC6
#define HALO4_FIRE_TUNING_OFFSET  8                                 // the je, RVA 0x61CBCE
#define HALO4_FIRE_TUNING_PATCH   {0xEB}
// No heat ("No Plasma Overheat"): the store of a weapon's heat after a shot
// (weapon +0x4A8). Every weapon (AI too).
#define HALO4_AOB_NO_HEAT         "F3 0F 11 83 A8 04 00 00 E8"  // RVA 0x61603C; 8 bytes NOP

// --- Thread-local game state -------------------------------------------
// halo4.dll's TLS index is at RVA 0x1057218. Slots (byte offset / 8), from
// the scripts that read them (checked live by array name).
#define HALO4_TLS_SLOT_PLAYERS         1    // "players", entry 0xF18 (players, RVA 0x28327D)
#define HALO4_TLS_SLOT_OBJECTS         3    // "object" (object_get_parent, RVA 0x68330B)
#define HALO4_TLS_SLOT_AI_GLOBALS      6    // ai_grenades, RVA 0x72ADF0
#define HALO4_TLS_SLOT_GAME_GLOBALS    8    // RVA 0x9C5AF
#define HALO4_TLS_SLOT_SQUADS          9    // "squad", entry 0xF0 (ai_set_deaf, RVA 0x71279E)
#define HALO4_TLS_SLOT_GAME_TIME       18   // game_tick_get, RVA 0xA0438
#define HALO4_TLS_SLOT_PLAYER_CONTROL  22   // player_set_pitch, RVA 0xABEBA (points at the entries)
#define HALO4_TLS_SLOT_PHYSICS         68   // physics_set_gravity, RVA 0x129B77
#define HALO4_TLS_SLOT_ACTORS          774  // "actor", entry 0x1380 (actor iterator, RVA 0x711011)

// data arrays (same layout as Reach's, checked live)
#define HALO4_ARRAY_ENTRY_SIZE 0x20  // int64
#define HALO4_ARRAY_HIGH_WATER 0x44  // int32, entries in use are below this
#define HALO4_ARRAY_DATA       0x50  // char*
// object table entry (0x18 each): u16 salt, byte type at +4, object* at +0x10
#define HALO4_OBJECT_ENTRY_TYPE   0x4
#define HALO4_OBJECT_ENTRY_OBJECT 0x10

// HUD: chud_show(bool) sets bit 0 of this mask (RVA 0x3C31B0); the mask is
// 0x3FF normally. Clearing bit 0 hides the whole HUD (checked live).
#define OFFSET_HALO4_CHUD_GLOBALS  0x4969D28  // char*
#define HALO4_CHUD_SHOW_MASK       0x2A38     // uint32
#define HALO4_CHUD_SHOW_BIT        0x1u

// ai globals
#define HALO4_AI_GLOBALS_GRENADES 0x3F8  // byte, AI may throw grenades (ai_grenades, RVA 0x72AE0D; 1 live)

// AI hearing: ai_set_deaf (RVA 0x712704) sets a squad flag (word +2 bit 1)
// or, per actor, byte +0x35C bit 0x20.
#define HALO4_SQUAD_FLAGS      0x2
#define HALO4_SQUAD_FLAG_DEAF  0x2u
#define HALO4_ACTOR_FLAGS      0x35C
#define HALO4_ACTOR_FLAG_DEAF  0x20u

// game time
#define HALO4_GAME_TIME_TICKS 0xC   // int32 (game_tick_get)
#define HALO4_GAME_TIME_SPEED 0x10  // float, 1.0 = normal (checked live)

// physics constants
#define HALO4_PHYSICS_GRAVITY 0x0  // float (physics_set_gravity; 4.1713 live)

// player control: one entry per local player (player_set_pitch, RVA 0xABED6)
#define HALO4_PLAYER_CONTROL_SIZE  0x1F0
#define HALO4_PLAYER_CONTROL_YAW   0x138  // float radians, 0..2pi (checked live against the unit's facing)
#define HALO4_PLAYER_CONTROL_PITCH 0x13C  // float radians

// --- Players --------------------------------------------------------------
#define HALO4_PLAYER_UNIT_DATUM 0x2C  // (players, RVA 0x2832B8; checked live)

// --- Objects --------------------------------------------------------------
#define HALO4_OBJECT_TAG_DATUM    0x8    // (unit_has_weapon core, RVA 0x5FDE0A)
#define HALO4_OBJECT_NEXT_SIBLING 0x1C   // attached objects: first child, then siblings (RVA 0x69FDB1, 0x69FDEF)
#define HALO4_OBJECT_FIRST_CHILD  0x20
#define HALO4_OBJECT_PARENT       0x24   // or -1 (object_get_parent, RVA 0x683338)
#define HALO4_OBJECT_PARENT_SEAT  0x2C   // int16, the seat in the parent vehicle (RVA 0x62EB57)
#define HALO4_OBJECT_POSITION     0x64   // Vector3 (checked live)
#define HALO4_OBJECT_FORWARD      0x70
#define HALO4_OBJECT_UP           0x7C
#define HALO4_OBJECT_VELOCITY     0x88
#define HALO4_OBJECT_DAMAGE_FLAGS 0x16C
#define HALO4_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE     (1u << 7)   // object_cannot_take_damage, RVA 0x69FD99
#define HALO4_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE (1u << 18)  // object_cannot_die, RVA 0x69E672
#define HALO4_DAMAGE_FLAG_IGNORES_EMP            (1u << 25)  // object_ignores_emp, RVA 0x62C445
// Vitality, as in Reach: damage sections, int16 size in bytes at +0x1D0 and
// int16 offset at +0x1D2; 0x18 bytes each, vitality at +0x10. Section 0 is
// health (RVA 0x62AFA8); the shield section is chosen from the model
// (RVA 0x62B1B0). Checked live: Chief has 3 sections, the shield is #2.
#define HALO4_OBJECT_SECTIONS_SIZE   0x1D0
#define HALO4_OBJECT_SECTIONS_OFFSET 0x1D2
#define HALO4_SECTION_SIZE           0x18
#define HALO4_SECTION_VITALITY       0x10

#define HALO4_OBJECT_TYPE_BIPED     0
#define HALO4_OBJECT_TYPE_VEHICLE   1   // (ai_vehicle_reserve, RVA 0x7168AA)
#define HALO4_OBJECT_TYPE_WEAPON    2
#define HALO4_OBJECT_TYPE_EQUIPMENT 3

// --- Units ------------------------------------------------------------------
#define HALO4_UNIT_ACTOR             0x484  // datum, -1 if not AI-controlled (read next to the actor array 94 times)
#define HALO4_UNIT_FLAGS             0x490  // uint64
#define HALO4_UNIT_FLAG_AI_DISREGARD 0x20u  // ai_disregard, RVA 0x715078
#define HALO4_UNIT_TEAM              0x498  // int8 (unit_get_team_index, RVA 0x6037F9; 1 live)
#define HALO4_UNIT_PLAYER            0x49C  // player datum, -1 if not a player (checked live)
#define HALO4_UNIT_CURRENT_WEAPON    0x63A  // int8 (read before the weapon lookup 0x5FA1C4, e.g. RVA 0x10D3BB)
#define HALO4_UNIT_DESIRED_WEAPON    0x63E  // int8 (written by weapon changes, e.g. RVA 0x5EF491)
#define HALO4_UNIT_WEAPONS           0x640  // uint32 datum[4] (unit_has_weapon core, RVA 0x5FDDEE)
#define HALO4_UNIT_EQUIPMENT         0x664  // datum of the armor ability (checked live)
#define HALO4_UNIT_GRENADE_COUNTS    0x676  // int8[] (unit_get_total_grenade_count, RVA 0x697E30)
// Sprint is a hidden equipment object ("hero_assist" tag, found live
// 2026-10-13 among the equipment objects: its parent is the player's unit,
// but it isn't in unit +0x664). Its energy (HALO4_EQUIPMENT_ENERGY) is the
// stamina. (Unit +0xF82/+0xF84 look like sprint state but are the sprint
// animation's phase: rewriting them made the screen shake.)
#define HALO4_UNIT_EMP_STUN          0x7DA  // int16 ticks; > 0 = shut down (unit_is_emp_stunned, RVA 0x6982F4)
#define HALO4_UNIT_WEAPON_COUNT      4
#define HALO4_UNIT_GRENADE_TYPES     4      // array size used here; the grenade list has 3 (frag, plasma, pulse)
#define HALO4_PLAYER_WEAPON_SLOTS    2

// Campaign teams (the script "team" enum from Assembly's Halo 4 scripting XML,
// same list as Reach's plus forerunner): 0 default, 1 player, 2 human,
// 3 covenant, 4 brute, 5 mule, 6 spare, 7 covenant_player, 8 forerunner.
#define HALO4_TEAM_PLAYER 1
#define HALO4_TEAM_HUMAN  2
#define HALO4_TEAM_COVENANT 3  // Prometheans are forerunner (8)
#define HALO4_TEAM_BRUTE    4
#define HALO4_TEAM_MULE     5
#define HALO4_TEAM_SPARE    6
#define HALO4_TEAM_COVENANT_PLAYER 7
#define HALO4_TEAM_FORERUNNER 8
#define HALO4_TEAM_COUNT    9

// Team allegiances: TLS slot 51 (offset 0x198 in the thread's block; the
// same TLS index variable as the other slots, RVA 0x1057218). Found 2026-10-10,
// static only (no live Halo 4): the "teams hostile" test at RVA 0x207AFC
// (60 callers, Reach's 0x1363B0): -1 on either side = hostile, same team =
// NOT hostile (so two AI of one team are always friends), else hostile when
// bit b of the u32 mask for team a is clear: masks at +0x168 (9 dwords, one
// per team; a second set at +0x144 is written beside it by the allegiance
// setter at 0x2081F0, called by ai_allegiance's core 0x207BF4). Reach's
// equivalent is slot 52, masks +0x164, where player/human list each other and
// every other team only itself; the table's layout (u16 count at +0, 0x14-byte
// records from +4: team A +4, team B +5) matches the code at 0x207BF4.
#define HALO4_TLS_SLOT_ALLEGIANCE      51
#define HALO4_ALLEGIANCE_FRIENDS       0x144  // uint32[9], written by the setter (flag differs from the next)
#define HALO4_ALLEGIANCE_MASKS         0x168  // uint32[9]: bit b of [a] set = a is not hostile to b

// --- Weapons ----------------------------------------------------------------
#define HALO4_WEAPON_HEAT          0x4A8  // float (Cheat Engine "no plasma overheat")
#define HALO4_WEAPON_AGE           0x4AC  // float, battery used (Cheat Engine "inf plasma ammo")
#define HALO4_WEAPON_BARRELS       0x51C  // 0x34 each (barrel functions, e.g. RVA 0x616D9D)
#define HALO4_WEAPON_BARREL_SIZE   0x34
#define HALO4_WEAPON_BARREL_COUNT  2
#define HALO4_BARREL_ERROR         0x1C   // float spread: max(error, new) at RVA 0x60A6F8
#define HALO4_WEAPON_TRIGGERS      0x584  // 0x10 each (index * 16 + 0x588/0x58A in the trigger update)
#define HALO4_WEAPON_TRIGGER_SIZE  0x10
#define HALO4_WEAPON_TRIGGER_COUNT 2
#define HALO4_TRIGGER_FLAGS        0x6    // word
#define HALO4_TRIGGER_FLAG_READY   0x1u   // cleared by a semi-auto shot (Cheat Engine "full auto", RVA 0x609868)
#define HALO4_WEAPON_MAGAZINES     0x5A4  // 0x1A each (RVA 0x615712); checked live (BR 36/72)
#define HALO4_WEAPON_MAGAZINE_SIZE 0x1A
#define HALO4_MAGAZINE_RESERVE     0x6    // int16
#define HALO4_MAGAZINE_LOADED      0xA    // int16 (Cheat Engine "bottomless clip", RVA 0x61574C)

// --- Equipment (armor abilities) --------------------------------------------
#define HALO4_EQUIPMENT_ENERGY 0x544  // float, 1.0 = full (Cheat Engine "inf ability", RVA 0x624838; 1.0 live)

// --- Tags -------------------------------------------------------------------
// Tag data = bases[address >> 28] + address * 4 (RVA 0x62B1C5, checked live).
#define OFFSET_HALO4_TAG_BASES       0x496A180  // char*[16]
#define OFFSET_HALO4_TAG_INSTANCES   0x107C0B0  // {i16 group, u16 salt, u32 address}* (points at RVA 0x28BCD50)
#define OFFSET_HALO4_SCENARIO        0x4967A30  // char* scnr data (checked live)
#define OFFSET_HALO4_GLOBALS         0x4967A28  // char* matg data (RVA 0x195580; checked live)
// The loaded cache file: a pointer to a buffer starting with two pointers and
// then the map header (Assembly's H4MCC "headerPointer" for 1.3528), and a
// count after it.
#define OFFSET_HALO4_MAP_SLOTS       0x28BCD30  // char*
#define OFFSET_HALO4_MAP_SLOT_COUNT  0x28BCD38  // int32
#define HALO4_MAP_SLOT_HEADER        0x10
#define HALO4_MAP_INTERNAL_NAME      0xB8
// The tag index header (runtime address at header +0x2E0): tag group count
// +0, group table +8 ({magic, parent, grandparent, name} 16 bytes each), tag
// count +0x10.
#define HALO4_MAP_INDEX_HEADER       0x2E0
#define HALO4_INDEX_GROUP_TABLE      0x8
#define HALO4_INDEX_TAG_COUNT        0x10

// Map file header (Assembly H4MCC LayoutsU6): same fields as Reach's, bigger
// header, offset masks at +0x4C4 (mask 0 applies to the tag names).
#define HALO4_MAP_HEADER_SIZE        0x1E000
#define HALO4_MAP_FILE_TABLE_COUNT   0x20
#define HALO4_MAP_FILE_TABLE_OFFSET  0x24
#define HALO4_MAP_FILE_TABLE_SIZE    0x28
#define HALO4_MAP_FILE_INDEX_OFFSET  0x2C
#define HALO4_MAP_OFFSET_MASK0       0x4C4

#define HALO4_TAGREF_DATUM 0xC

// bipd
#define HALO4_BIPD_JUMP_VELOCITY 0x5F0  // float (2.36 live; Cheat Engine "jump height")

// vehi physics types (Assembly Halo4MCC vehi.xml; Reach's blocks moved by
// 0x100): one block per type, only the vehicle's own filled in. Vehicle
// speed scales each type's max speeds and acceleration, or its engine's max
// angular velocity. (VTOL, the Pelican/Falcon type, isn't scaled.)
#define HALO4_VEHI_TANK          0x5D0  // element 0x94: engine max angular velocity +0x3C
#define HALO4_VEHI_JEEP          0x5DC  // 0x5C: engine +0x18
#define HALO4_VEHI_PLANE         0x5E8  // 0x64: forward/reverse speed +0x0/+0x4, acceleration +0x8
#define HALO4_VEHI_WOLVERINE     0x5F4  // 0x60: engine +0x18
#define HALO4_VEHI_SCOUT         0x600  // 0x78: +0x8/+0xC/+0x10; max vehicle speeds +0x50/+0x64
#define HALO4_VEHI_FIGHTER       0x60C  // 0x68: +0x14/+0x18/+0x1C
#define HALO4_VEHI_MANTIS        0x624  // 0xAC: +0x14/+0x18/+0x1C
#define HALO4_VEHI_CHOPPER       0x63C  // 0x7C: engine +0x18
#define HALO4_VEHI_GUARDIAN      0x648  // 0x30: +0x8/+0xC/+0x10
#define HALO4_VEHI_JACKAL_GLIDER 0x654  // 0x170: +0x8/+0xC/+0x10; max vehicle speeds +0x3C/+0x50
#define HALO4_VEHI_BOAT          0x660  // 0x60: engine max angular velocity +0x24
#define HALO4_VEHI_SPACE_FIGHTER 0x66C  // 0x110 (Broadsword): full/neutral/reverse throttle speed +0x14/+0x18/+0x1C, acceleration +0x20
#define HALO4_VEHI_REVENANT      0x678  // 0xCC: engine +0x3C, forward/reverse speed +0x94/+0x98, acceleration +0x9C
// Boost: power burned per second (1 = all of it in a second).
#define HALO4_VEHI_BOOST_POWER_PER_SECOND 0x55C
// Seats (vehi +0x510, element 0x16C): flags +0x0, bit 1 "Locked", bit 10
// "Boarding Seat" (same bits as Reach).
#define HALO4_VEHI_SEATS       0x510
#define HALO4_VEHI_SEAT_SIZE   0x16C
#define HALO4_SEAT_FLAG_LOCKED 0x2u
#define HALO4_SEAT_FLAG_BOARDING_SEAT 0x400u
#define HALO4_SEAT_BOARDING_INDEX 0x4A  // int16: the seat a boarding seat takes over, -1 if none (Ghost: side seats 1-3 -> 0)
// AI seat reservations on a vehicle object: a bitmask, all ones = reserved
// for no AI (ai_vehicle_reserve, RVA 0x7168C0).
#define HALO4_VEHICLE_AI_RESERVED 0x10F4  // uint64

// object definitions
#define HALO4_OBJE_MODEL 0x64  // tag reference (datum read at +0x70, RVA 0x62B211)

// hlmt (model): shield section lookup (RVA 0x62B1B0)
#define HALO4_HLMT_MAX_SHIELD      0x104  // float; > 0 if the model has shields (70 for Chief)
#define HALO4_HLMT_SECTIONS        0x114  // block, element 0xB4
#define HALO4_HLMT_SECTION_SIZE    0xB4
#define HALO4_HLMT_SECTION_SHIELD  0xC    // int32; not 0/-1 marks the shield section

// weap
#define HALO4_WEAP_FLAGS              0x2A0
#define HALO4_WEAP_FLAG_NOT_FOR_PLAYER (1u << 23)
// "Melee Only" (energy sword, gravity hammer-class weapons): bit 19 of the same
// flags word per Assembly's weap plugin (the plugin's bit 23 is the verified
// "Cannot Be Used By Player" above, so the word's layout is right). The engine
// reads it too: 0x730147 and 0x7302A3 (AI weapon checks) and 0x610250 (a
// getter) shift the weapon tag's +0x2A0 right by 0x13 (found 2026-10-10,
// static only).
#define HALO4_WEAP_FLAG_MELEE_ONLY    (1u << 19)
#define HALO4_WEAP_AUTOAIM_ANGLE      0x3A4
#define HALO4_WEAP_AUTOAIM_RANGE      0x3A8
#define HALO4_WEAP_AUTOAIM_FALLOFF    0x3AC
#define HALO4_WEAP_DEVIATION_ANGLE    0x3C4
// Halo 4 aims with the "Aim Assist Modes" block (the Cheat Engine aimbot
// patches read these, RVA 0x1DD625): the same five fields per mode.
#define HALO4_WEAP_AIM_ASSIST_MODES   0x3F0  // block, element 0x44
#define HALO4_WEAP_AIM_ASSIST_SIZE    0x44
#define HALO4_AIM_ASSIST_ANGLE        0x8
#define HALO4_AIM_ASSIST_RANGE        0xC
#define HALO4_AIM_ASSIST_FALLOFF      0x10
#define HALO4_AIM_ASSIST_DEVIATION    0x28
#define HALO4_WEAP_MAGAZINES          0x500  // block, element 0x84
#define HALO4_WEAP_MAGAZINE_DEF_SIZE  0x84
#define HALO4_MAGDEF_ROUNDS_TOTAL_MAX 0x8
#define HALO4_MAGDEF_ROUNDS_LOADED_MAX 0xA
#define HALO4_WEAP_BARRELS            0x518  // block, element 0x190
#define HALO4_WEAP_BARREL_SIZE        0x190
#define HALO4_BARREL_ROUNDS_PER_SECOND 0x4  // rangef (min, max): automatic weapons' rate (SAW 20, AR 10)
#define HALO4_BARREL_FIRE_RECOVERY    0x28   // float seconds after a shot (SAW 0.067, rockets 0.8)
#define HALO4_RAPID_FIRE_ROUNDS_PER_SECOND 60.0f  // one a tick (60 ticks a second)
#define HALO4_BARREL_PROJECTILE       0x110  // tag reference
#define HALO4_BARREL_FIRING_EFFECTS   0x184  // block, element 0xF4
#define HALO4_FIRING_EFFECT_SIZE      0xF4
#define HALO4_FIRING_EFFECT_DAMAGE    0x54   // tag reference (drdf): the shot's kick on the shooter
#define HALO4_FIRING_EFFECT_SECONDARY_DAMAGE       0x94
#define HALO4_FIRING_EFFECT_RIDER_RESPONSE         0xA4
#define HALO4_FIRING_EFFECT_SECONDARY_RIDER_DAMAGE 0xE4
#define HALO4_FIRING_EFFECT_EFFECT           0x4    // tag reference (effe)
#define HALO4_FIRING_EFFECT_SECONDARY_EFFECT 0x44
#define HALO4_EFFE_EVENTS               0x34   // block, element 0x44
#define HALO4_EFFE_EVENT_SIZE           0x44
#define HALO4_EFFE_EVENT_ACCELERATIONS  0x2C   // block, element 0x14
#define HALO4_EFFE_ACCELERATION_SIZE    0x14
#define HALO4_EFFE_ACCELERATION         0x8    // float

// matg
#define HALO4_MATG_GRENADE_GLOBALS    0x6C8  // tag reference (gggl): its +0 block lists the grenade types
#define HALO4_GRENADE_LIST            0x0    // block, element 0x78 (RVA 0x195413); +0x0 int16 maximum count (2)
#define HALO4_GRENADE_DEF_SIZE        0x78
#define HALO4_MATG_PLAYER_INFORMATION 0x5EC  // block, element 0xDC; first 10 floats are speeds (checked live)
#define HALO4_MATG_PLAYER_INFO_SIZE   0xDC

// scnr
#define HALO4_SCNR_SOFT_CEILINGS      0x304  // block, element 0xC; +0x0 flags16, +0x2 runtime flags16
#define HALO4_SCNR_SOFT_CEILING_SIZE  0xC
#define HALO4_SCNR_KILL_TRIGGERS      0x4FC  // block (31 in Midnight)
#define HALO4_SOFT_CEILING_IGNORE_ALL 0xF

#define HALO4_TAG_WEAP 0x77656170  // 'weap'
#define HALO4_TAG_PROJ 0x70726F6A  // 'proj'
#define HALO4_TAG_CHAR 0x63686172  // 'char'
#define HALO4_TAG_VEHI 0x76656869  // 'vehi'
#define HALO4_TAG_EQIP 0x65716970  // 'eqip'
#define HALO4_TAG_SCEN 0x7363656E  // 'scen'
#define HALO4_TAG_MACH 0x6D616368  // 'mach'
#define HALO4_TAG_CTRL 0x6374726C  // 'ctrl'
#define HALO4_TAG_BLOC 0x626C6F63  // 'bloc' (crates)
#define HALO4_TAG_BIPD 0x62697064  // 'bipd'

// obje (Assembly Halo4MCC vehi.xml) and hlmt
#define HALO4_OBJE_BOUNDING_RADIUS  0x20  // float
#define HALO4_OBJE_DEFAULT_VARIANT  0x60  // string id
#define HALO4_HLMT_VARIANTS         0xC4  // block, element 0x6C, name string id first
#define HALO4_HLMT_VARIANT_SIZE     0x6C
// char: parent and unit datums, as in Reach (checked live on m60_rescue)
#define HALO4_CHAR_PARENT           0x10
#define HALO4_CHAR_UNIT             0x20
// scnr palettes (element 0x10, datum at +0xC; Assembly Halo4MCC scnr.xml)
#define HALO4_PALETTE_ENTRY_SIZE      0x10
#define HALO4_PALETTE_ENTRY_DATUM     0xC
#define HALO4_SCNR_SCENERY_PALETTE    0x15C
#define HALO4_SCNR_VEHICLE_PALETTE    0x18C
#define HALO4_SCNR_EQUIPMENT_PALETTE  0x1A4
#define HALO4_SCNR_MACHINE_PALETTE    0x1E0
#define HALO4_SCNR_CONTROL_PALETTE    0x210
#define HALO4_SCNR_CRATE_PALETTE      0x644
#define HALO4_SCNR_BIPED_PALETTE      0x174
#define HALO4_SCNR_WEAPON_PALETTE     0x1BC
#define HALO4_SCNR_CHARACTER_PALETTE  0x444
// Designer zones (checked live on m60_rescue: 51, e.g. zone 9 has the
// Ghost, Scorpion, Phantom, Warthog, Pelican and Wraith): each lists tags
// directly (+0x4, element 0x10, datum +0xC) and by palette index (int16
// blocks).
#define HALO4_SCNR_DESIGNER_ZONES     0x778  // element 0xF4
#define HALO4_SCNR_DESIGNER_ZONE_SIZE 0xF4
#define HALO4_DESIGNER_ZONE_TAGS      0x4
#define HALO4_DESIGNER_ZONE_TAG_SIZE  0x10
#define HALO4_DESIGNER_ZONE_BIPEDS    0x10
#define HALO4_DESIGNER_ZONE_VEHICLES  0x1C
#define HALO4_DESIGNER_ZONE_WEAPONS   0x28
#define HALO4_DESIGNER_ZONE_EQUIPMENT 0x34
#define HALO4_DESIGNER_ZONE_SCENERY   0x40
#define HALO4_DESIGNER_ZONE_MACHINES  0x4C
#define HALO4_DESIGNER_ZONE_CONTROLS  0x64
#define HALO4_DESIGNER_ZONE_CRATES    0x88
#define HALO4_DESIGNER_ZONE_CHARACTERS 0xB8

// String ids (Assembly H4MCC LayoutsU6, as Reach's)
#define HALO4_MAP_STRING_COUNT       0x30
#define HALO4_MAP_STRING_DATA        0x34
#define HALO4_MAP_STRING_DATA_SIZE   0x38
#define HALO4_MAP_STRING_INDEX       0x3C
#define HALO4_MAP_STRING_NAMESPACE_COUNT 0x40
#define HALO4_MAP_STRING_NAMESPACES  0x44
