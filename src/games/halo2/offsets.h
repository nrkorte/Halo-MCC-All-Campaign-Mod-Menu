#pragma once

// halo2.dll (Halo 2, classic campaign) offsets for MCC 1.3528.0.0. Found
// from the HaloScript function table (RVA 0xE04850; each definition holds
// its name at +0x8 and its evaluator at +0x20) and checked live in a
// mission. Like Halo: CE, the game state is in plain globals. The engine's
// cheat_* script globals have no address here, unlike CE's.

// --- Hooks --------------------------------------------------------------
// Game tick: runs each subsystem, then increments the tick (RVA 0x6A765D).
#define OFFSET_HALO2_GAME_TICK 0x6A72A0
// Checkpoints: game_save_immediate calls 0x700760() (RVA 0x789C68),
// game_revert calls 0x67A960() (RVA 0x789A08). Neither takes arguments.
#define OFFSET_HALO2_GAME_SAVE_IMMEDIATE 0x700760
#define OFFSET_HALO2_GAME_REVERT         0x67A960
// Damage to an object's model sections (a Ghost's wings, a Wraith's armor:
// what breaks off and sparks). Args: damage data, scale (xmm1), object datum
// (r8d), result (r9, set up by the caller). It runs before body damage
// (RVA 0x90F328) and ignores the "cannot take damage" flag.
#define OFFSET_HALO2_SECTION_DAMAGE 0x90B4F0
// Unit actions: 0x93B750(unit, action id) runs the action's handler from a
// table (0xE18CA0). Flip ejection, found live (2026-10-02) by logging call
// stacks on the seat exit 0x93A490: the unit update 0x92AEA0 counts ticks
// upside down (+0x393) against the seat's turnover time (seat +0x20) and
// then sends action 0x1E (eject; handler 0x936E10) at 0x92B1B2. About 20
// other places send 0x1E; only the one returning to 0x92B1B7 is skipped. Your
// own exit goes through 0x93BB30 (detach with a shove) instead.
#define OFFSET_HALO2_UNIT_ACTION 0x93B750
#define HALO2_ACTION_EJECT       0x1E
#define HALO2_FLIP_EJECT_RETURN  0x92B1B7
// Player control: per-player entries (0xB8 each) at +0x38 of the struct this
// points to (RVA 0x6C0E07). Each starts with the look yaw (0..2pi) and pitch
// that MCC keeps across ticks and the camera follows (found live: nudging it
// turns the view and sticks). The per-tick input record built from it can't
// be used to turn the view: it's rebuilt every tick.
#define OFFSET_HALO2_PLAYER_CONTROL   0x15E40D0  // char*
#define HALO2_PLAYER_CONTROL_ENTRIES  0x38
#define HALO2_PLAYER_CONTROL_SIZE     0xB8
#define HALO2_PLAYER_CONTROL_YAW      0x0
#define HALO2_PLAYER_CONTROL_PITCH    0x4

// --- Engine functions (call on the game thread only) ---------------------
// From unit_add_equipment (RVA 0x8F0733).
// object_placement_data_new(placement*, tag datum, owner object, 0)
#define OFFSET_HALO2_OBJECT_PLACEMENT_NEW 0x8D8880
// object datum object_new(placement*)
#define OFFSET_HALO2_OBJECT_NEW           0x8D79D0
// bool unit_add_weapon(unit, weapon, mode); the loadout passes 1
#define OFFSET_HALO2_UNIT_ADD_WEAPON      0x8F0880
#define HALO2_ADD_WEAPON_MODE             1
// object_delete(object datum)
#define OFFSET_HALO2_OBJECT_DELETE        0x8D4D10
// object_set_position(object, position*, forward*, up*, void*): what
// object_teleport ends with (RVA 0x7B79C8)
#define OFFSET_HALO2_OBJECT_SET_POSITION  0x8DAF00
#define HALO2_PLACEMENT_DATA_SIZE         0x400  // oversized buffer
// Placement fields (actor_create fills them in before object_new, RVA
// 0x621FE9..0x62204B): variant name string id, position, then the forward
// and up vectors it builds from the facing angles (0x7003F0).
#define HALO2_PLACEMENT_VARIANT           0xC
#define HALO2_PLACEMENT_POSITION          0x1C   // float[3]
#define HALO2_PLACEMENT_FORWARD           0x28   // float[3]
#define HALO2_PLACEMENT_UP                0x34   // float[3]

// --- Spawning ----------------------------------------------------------------
// collision_test_vector(flags, point*, vector*, ignore object, ignore object 2,
// result*): 47 callers. The flags are the unit line-of-sight test's (RVA
// 0x8F28C1, unit to object). Result: fraction (float, 1 = no hit) at +0x4,
// set up at RVA 0x75B8B5; the mod works the point out from it.
#define OFFSET_HALO2_COLLISION_TEST_VECTOR 0x75B850
#define HALO2_COLLISION_FLAGS_LINE_OF_SIGHT 0x80080Du
#define HALO2_COLLISION_RESULT_FRACTION    0x4
#define HALO2_COLLISION_RESULT_SIZE        0x200  // oversized buffer
// actor_create(character, squad, starting location*, bool) -> actor datum or
// -1. Creates the character's unit (char +0x10) at the starting location,
// gives it the character's weapons (0x621460) and attaches the actor
// (0x6210A0). Squad -1 is handled: the game's own squadless swarm spawn
// (RVA 0x66452A) passes it, with a record built by 0x664680 (squadless
// branch, RVA 0x66471C): zeroed 0x64 bytes; position +4; BSP +0x10 = -1;
// facing yaw/pitch radians +0x14/+0x18 (from a forward vector, 0x6FBFB0);
// flags +0x1C = 4; character index/weapon +0x20 and vehicle +0x28 = -1
// (dwords); secondary weapon +0x24, grenades +0x2C, +0x3C, +0x60 = -1
// (words); swarm count +0x2E = 1. Variant name +0x30 = 0 picks the
// character's default (0x6217B0).
#define OFFSET_HALO2_ACTOR_CREATE          0x621AB0
#define HALO2_START_SIZE                   0x64
#define HALO2_START_POSITION               0x4
#define HALO2_START_BSP                    0x10
#define HALO2_START_YAW                    0x14
#define HALO2_START_FLAGS                  0x1C
#define HALO2_START_FLAGS_SQUADLESS        4
#define HALO2_START_CHARACTER              0x20
#define HALO2_START_SECONDARY_WEAPON       0x24
#define HALO2_START_VEHICLE                0x28
#define HALO2_START_GRENADES               0x2C
#define HALO2_START_SWARM_COUNT            0x2E
#define HALO2_START_3C                     0x3C
#define HALO2_START_60                     0x60
// actor_set_active(actor, bool): what the swarm spawn calls next (RVA 0x664566).
#define OFFSET_HALO2_ACTOR_SET_ACTIVE      0x6229E0
// actor_set_team(actor, int16 team): actor +0x24 and its unit's team (+0x13C)
// (what ai_set_team does per actor, RVA 0x61BCB4).
#define OFFSET_HALO2_ACTOR_SET_TEAM        0x6228D0

// --- Code patches (from the user's Cheat Engine table; checked unique) ----
// These affect every unit, not just the player.
#define HALO2_AOB_AI_CANT_FIRE  "41 0F 2F 4D 0C"                 // RVA 0x629D15; 5 x NOP
#define HALO2_AOB_AI_INVISIBLE  "F3 0F 10 40 04 F3 41 0F 10"     // RVA 0x6507F4; first 5 x NOP
#define HALO2_AOB_GRENADES      "88 81 52 02 00 00"              // RVA 0x93CA38; 6 x NOP (stores the new count)
#define HALO2_AOB_FULL_AUTO     "42 83 64 BF 04 FE 44"           // RVA 0x8EE5C7; first 6 x NOP
// Full auto for latch-rocketlauncher triggers (the rocket launcher): the same
// "trigger released" bit cleared on that behavior's own fire path.
#define HALO2_AOB_FULL_AUTO_ROCKET "42 83 64 BF 04 FE 0F B7 5C 24 34 E9"  // RVA 0x8EE77A; first 6 x NOP
// With a lock target, the rocket launcher doesn't fire on the pull: it enters
// the "locking" trigger state (5, via 0x8ED110) and fires the tracking
// (secondary) barrel on release, so holding the trigger never fires. For full
// auto, that call becomes a jmp to the secondary barrel's fire (RVA 0x8EE643),
// which the plain full-auto patch already covers.
#define HALO2_AOB_ROCKET_LOCK      "0F B7 5C 24 34 0F B7 D3 41 8B CC E8 ?? ?? ?? ?? E9"  // RVA 0x8EE739
#define HALO2_ROCKET_LOCK_OFFSET   5  // jmp rel32 -0x100 (0x8EE73E -> 0x8EE643)
// No recoil: skips the call that kicks the player's view after a shot (RVA
// 0x8E6BF9; it takes the barrel tag's recoil range, as in Halo 3).
// Camo stays on when firing: the unit-control update turns active camo off
// when a trigger is pressed (RVA 0x8F5EDE); that call becomes NOPs.
#define HALO2_AOB_CAMO_FIRE      "44 38 B0 08 03 00 00 74 0C 33 D2 41 8B CF E8"  // RVA 0x8F5ED0
#define HALO2_CAMO_FIRE_OFFSET   14
// Firing a shot (RVA 0x8E6A3F, with the weapon tag's camo ding) and throwing
// a grenade (RVA 0x93CDD0) both go through 0x8EFF70, which turns camo off
// if it's on (RVA 0x8EFFBA); its je becomes a jmp past the camo ding too,
// straight to the "unit fired" event (RVA 0x8F0032), so shots don't flicker it. The grenade throw also
// turns it off directly (RVA 0x8F499A); that call becomes NOPs.
#define HALO2_AOB_CAMO_SHOT       "80 B8 08 03 00 00 00 74 18 33 D2 8B CB"  // RVA 0x8EFFB3
#define HALO2_CAMO_SHOT_OFFSET    7
#define HALO2_AOB_CAMO_GRENADE    "33 D2 41 8B CF E8 ?? ?? ?? ?? 4C 8D 9C 24 80 01 00 00"  // RVA 0x8F4995
#define HALO2_CAMO_GRENADE_OFFSET 5
#define HALO2_AOB_NO_RECOIL      "F3 0F 58 D1 F3 0F 11 95 6C 1C 00 00 E8"  // RVA 0x8E6BED
#define HALO2_NO_RECOIL_OFFSET   12                                         // the call; 5 x NOP

// Third person (found 2026-10-13). The director builds each local player's
// camera from one of two camera types and keeps it in a 0x150-byte entry
// (array found through the lea at RVA 0x6D4B8D; camera update function at
// +0x20, the mode word it last saw at +0x70). The deciding function (RVA
// 0x6D4C50) returns 1 for third-person camera types (a vehicle seat that asks
// for it, the dead camera) and 0 on foot; the ja at RVA 0x6D4DDE sends the
// other cases to a return of the seat flag, so NOPing it always returns 1.
// The director only switches camera types when the mode word changes, so
// the mod writes a bogus mode to make it look again (first person update
// function RVA 0x77DEB0, third person 0x77C9F0).
#define HALO2_AOB_THIRD_PERSON    "66 41 3B DD 77 14 41 8B C5"  // RVA 0x6D4DDA
#define HALO2_THIRD_PERSON_OFFSET 4                               // the ja; 2 x NOP
#define HALO2_AOB_DIRECTORS       "4C 8D 35 ?? ?? ?? ?? 84 DB 75 08 66 42 39 74 37 70"  // RVA 0x6D4B8D
#define HALO2_DIRECTOR_CAMERA_FN  0x20
#define HALO2_DIRECTOR_MODE       0x70
#define HALO2_CAMERA_FIRST_PERSON 0x77DEB0
#define HALO2_CAMERA_THIRD_PERSON 0x77C9F0

// Field of view (found 2026-10-13): the camera reads each player's FOV setting
// (int32 degrees, 78 by default, MCC's own option) through RVA 0x3C230, which
// clamps it to 70..120 and turns it into radians. Player 0's int is at the RVA
// below, the others 0xACC apart. Writing it takes effect at once.
#define HALO2_FOV_SETTING   0x1A81584
#define HALO2_FOV_STRIDE    0xACC
#define HALO2_FOV_MIN       70
#define HALO2_FOV_MAX       120
#define HALO2_FOV_DEFAULT   78

// Explosion size (found 2026-10-13): the hidden skull at index 16 ("Boom") doubles
// the reach of area damage; the user confirmed it with a grenade thrown out of range
// (skull on: hurt; off: not). The skull is read at two sites (skull check 0x6EEF70,
// then "is this the other mode" 0x6F4A20): RVA 0x90A95F doubles the damage query's
// radius (addss xmm6, xmm6), RVA 0x90B1DA picks a 2.0 factor (xmm12) over 1.0 (xmm8)
// for the per-object range. The mod points those two calls at its own code (which
// calls 0x6F4A20 as before and applies the slider's factor instead of 2) and keeps
// the skull on while the slider isn't 1.
#define HALO2_SKULL_BOOM          16
#define HALO2_AOB_BOOM_QUERY      "B9 10 00 00 00 E8 ?? ?? ?? ?? 84 C0 74 0D E8 ?? ?? ?? ?? 84 C0 75 04 F3 0F 58 F6"  // RVA 0x90A951
#define HALO2_AOB_BOOM_RANGE      "B9 10 00 00 00 E8 ?? ?? ?? ?? 84 C0 74 0F E8 ?? ?? ?? ?? 84 C0 75 06 41 0F 28 C4 EB 04 41 0F 28 C0"  // RVA 0x90B1CC
#define HALO2_BOOM_CALL_OFFSET    14  // the call to 0x6F4A20 inside both patterns

// --- Globals ----------------------------------------------------------------
#define OFFSET_HALO2_OBJECTS      0x18B7398  // data array* (unit_kill, RVA 0x8FAF3A)
#define OFFSET_HALO2_OBJECT_HEAP  0x18B7360  // char*; data = align16(heap + 0x57) + entry offset (RVA 0x76B328)
#define OFFSET_HALO2_PLAYERS      0xE80A28   // data array*, 0x224 each (players, RVA 0x7B817F)
#define OFFSET_HALO2_AI_RECORDS   0x1E92340  // data array*, 0x98 each (ai_set_deaf, RVA 0x6190BD)
#define OFFSET_HALO2_ACTORS       0x1A79298  // data array*, 0x8A0 each (actor_set_team, RVA 0x6228DA; checked live)
#define HALO2_ACTOR_UNIT          0x18       // datum (checked live)
#define OFFSET_HALO2_AI_GLOBALS   0x1A79258  // char* (ai_grenades, RVA 0x611060)
#define OFFSET_HALO2_GAME_TIME    0x15FE008  // char* (game_tick_get, RVA 0x706860)
#define OFFSET_HALO2_GRAVITY      0x1652740  // float* (physics_set_gravity, RVA 0x761438)
#define OFFSET_HALO2_HUD          0x187E350  // char* (show_hud, RVA 0x82BE80)
// Skulls ("ice cream flavors"): one byte each at 0x15F25F0 + index (the
// skull check, RVA 0x6EEF70; debug_ice_cream_flavor_status_* globals point
// at them). Envy is 0; Acrophobia, which MCC added, is 34 (the Cheat
// Engine table's address is the Windows Store build's).
#define OFFSET_HALO2_SKULLS       0x15F25F0
#define HALO2_SKULL_COUNT         35  // bytes 0..34 (check 0x6EEF70: cmp 0x22); read live, cleared on game reset (0x6EFC80)
// object_set_velocity's core (evaluator 0x783FA0 -> 0x90A000 local space ->
// here): (object, const float* linear world or null, const float* angular or
// null); writes +0x88/+0x94, updates the physics (0x705D50) and wakes the
// object. The biped code sets velocity through it too. Found statically 2026-10-04.
#define OFFSET_HALO2_OBJECT_SET_VELOCITY 0x8DB9D0
// Projectile creation (weapon datum, int16 barrel, int, bool): reads the
// barrel's projectile (+0x8C, datum +0x90 at 0x8E5500; on barrel 0 with
// weapon +0x1A6 > 0, barrel 1's) and makes the projectiles. Barrel fire
// (0x8E6180, the trigger code's per-shot call) only marks the barrel; after
// the objects update, 0x8EF960 calls this once per marked barrel, for every
// weapon (AI too). Hooked for the chaos gun and recoil push.
#define OFFSET_HALO2_BARREL_CREATE_PROJECTILES 0x8E4940
#define HALO2_SKULL_ENVY          0
#define HALO2_SKULL_ACROPHOBIA    34

#define HALO2_AI_GLOBALS_GRENADES 0x340  // byte
#define HALO2_AI_RECORD_FLAGS     0x2    // word; 0x2 = deaf
#define HALO2_AI_FLAG_DEAF        0x2u
#define HALO2_GAME_TIME_SPEED     0xC    // float, 1.0 = normal (checked live)
#define HALO2_HUD_SHOW            0x26A  // byte

// data arrays (RVA 0x67B360)
#define HALO2_ARRAY_ENTRY_SIZE  0x24  // int32
#define HALO2_ARRAY_HIGH_WATER  0x3C  // int32
#define HALO2_ARRAY_DATA        0x48  // int64 offset from the header
// object table entry (0xC each): u16 salt, byte type at +3, int32 data offset at +8
#define HALO2_OBJECT_ENTRY_TYPE   0x3
#define HALO2_OBJECT_ENTRY_OFFSET 0x8

// --- Players ----------------------------------------------------------------
#define HALO2_PLAYER_UNIT 0x2C

// --- Objects (checked live) -------------------------------------------------
#define HALO2_OBJECT_TAG_DATUM    0x0
#define HALO2_OBJECT_POSITION     0x64
#define HALO2_OBJECT_PARENT       0x14   // or -1 (checked live: a held weapon points at its unit)
// Attached objects: first child, then each child's next sibling (-1 ends).
// object_cannot_take_damage (RVA 0x90EB00) also flags children whose object
// flags (+0x4) have bit 26 set, e.g. a vehicle's attached turrets.
#define HALO2_OBJECT_NEXT_SIBLING 0x0C
#define HALO2_OBJECT_FIRST_CHILD  0x10
#define HALO2_OBJECT_FLAGS        0x04
#define HALO2_OBJECT_FLAG_SHARES_DAMAGE_FLAGS (1u << 26)
#define HALO2_OBJECT_VELOCITY     0x88
#define HALO2_OBJECT_HEALTH       0xEC  // float, normalized (max health at +0xE4, checked live)
#define HALO2_OBJECT_SHIELD       0xF0  // (max shield at +0xE8)
#define HALO2_OBJECT_DAMAGE_FLAGS 0x10A  // word
#define HALO2_DAMAGE_FLAG_KILL               0x20u  // unit_kill, RVA 0x8FAF67
#define HALO2_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE 0x80u  // object_cannot_take_damage, RVA 0x90EAC8

#define HALO2_OBJECT_TYPE_BIPED  0
#define HALO2_OBJECT_TYPE_VEHICLE 1
#define HALO2_OBJECT_TYPE_WEAPON 2

// --- Units ------------------------------------------------------------------
#define HALO2_UNIT_ACTOR             0x130  // datum, -1 if not AI (ai_braindead_by_unit, RVA 0x61AEBC)
#define HALO2_UNIT_FLAGS             0x138
#define HALO2_UNIT_FLAG_AI_DISREGARD 0x80u   // ai_disregard, RVA 0x61B01C
#define HALO2_UNIT_TEAM              0x13C  // int16 (unit_get_team_index, RVA 0x8F9958)
#define HALO2_UNIT_AIM_VECTOR        0x168  // float[3] (unit control, RVA 0x8F58BD)
// Active camo (Arbiter, or anyone with Envy): state byte (0 off, 1 on),
// ticks left, and the camo flag in the unit flags (RVA 0x8F1A75).
#define HALO2_UNIT_CAMO_STATE        0x308
#define HALO2_UNIT_CAMO_TICKS        0x30A  // int16
#define HALO2_CAMO_ON                1
#define HALO2_UNIT_CURRENT_WEAPON    0x226  // int8 (RVA 0x759BC9)
#define HALO2_UNIT_DESIRED_WEAPON    0x22A  // int8 (copied from unit control, RVA 0x8F5877)
#define HALO2_UNIT_WEAPONS           0x22C  // datum[4] (checked live)
#define HALO2_UNIT_WEAPON_COUNT      4
#define HALO2_PLAYER_WEAPON_SLOTS    2

#define HALO2_TEAM_PLAYER 1
#define HALO2_TEAM_HUMAN  2
#define HALO2_TEAM_COVENANT 3
#define HALO2_TEAM_HERETIC  6  // the Arbiter's enemies; the Covenant are his allies

// --- Weapons (checked live) -------------------------------------------------
#define HALO2_WEAPON_HEAT          0x184  // float
#define HALO2_WEAPON_AGE           0x188  // float, battery used
#define HALO2_WEAPON_TRIGGER       0x1A8  // byte: ticks since the last shot, 0x7F when idle
#define HALO2_WEAPON_FIRE_DELAY    0x1AA  // word (Cheat Engine rapid fire)
// Spread (found live 2026-10-13 by logging a weapon while firing): these floats
// climb from 0 to 1 at about 0.08 per sample (50 ms) while the trigger is held and
// decay after: +0x1B8 and +0x1C4 are the two barrels' error (0xC apart), +0x1C8
// follows the first (smoothed). Zeroed each frame = no spread.
#define HALO2_WEAPON_ERROR0        0x1B8
#define HALO2_WEAPON_ERROR1        0x1C4
#define HALO2_WEAPON_ERROR_SMOOTH  0x1C8
#define HALO2_TRIGGER_READY        0x7F
#define HALO2_WEAPON_MAGAZINES     0x22A  // 8 each: +4 int16 reserve, +6 int16 loaded
#define HALO2_WEAPON_MAGAZINE_SIZE 0x8
#define HALO2_MAGAZINE_RESERVE     0x4
#define HALO2_MAGAZINE_LOADED      0x6

// --- Tags -------------------------------------------------------------------
// Tag entries are {group magic, datum, int32 offset, size}; data and block
// elements are at tag base + offset (RVA 0x759C13).
#define OFFSET_HALO2_TAG_ENTRIES 0x15E4B30  // entry*
#define OFFSET_HALO2_TAG_BASE    0x15E4B38  // char*
// A block offset with the top bit set is relative to this second base
// instead (btr 0x1F, then add; RVA 0x6370AB).
#define OFFSET_HALO2_TAG_BASE_2  0xE80AC0   // char*
#define OFFSET_HALO2_MAP_HEADER  0x15E4B50  // char* (Assembly H2MCC poking)
#define HALO2_TAG_ENTRY_SIZE     0x10
#define HALO2_TAG_ENTRY_DATUM    0x4
#define HALO2_TAG_ENTRY_OFFSET   0x8
// Tag blocks are {int32 count, int32 offset}; tag references {group, datum}.
#define HALO2_TAGREF_DATUM       0x4

// Map header (Assembly H2MCC LayoutsU1). The .map files are zlib-compressed
// in 0x40000-byte chunks after the 0x380-byte header; file offsets in the
// header minus 0x380 index the decompressed data (checked on 07a_highcharity).
#define HALO2_MAP_HEADER_SIZE        0x380
#define HALO2_MAP_FILE_TABLE_COUNT   0x20
#define HALO2_MAP_FILE_TABLE_OFFSET  0x24
#define HALO2_MAP_FILE_TABLE_SIZE    0x28
#define HALO2_MAP_FILE_INDEX_OFFSET  0x2C
#define HALO2_MAP_INTERNAL_NAME      0xB0
// String ids: index = id & 0xFFFF (the top byte is the length).
#define HALO2_MAP_STRING_COUNT        0x30
#define HALO2_MAP_STRING_TABLE_OFFSET 0x34
#define HALO2_MAP_STRING_TABLE_SIZE   0x38
#define HALO2_MAP_STRING_INDEX_OFFSET 0x3C
#define HALO2_MAP_CHUNK_SIZE         0x308
#define HALO2_MAP_CHUNK_TABLE        0x310  // {int32 compressed size, int32 file offset}[count]
#define HALO2_MAP_CHUNK_COUNT        0x314

// obje (every object tag)
#define HALO2_OBJE_BOUNDING_RADIUS  0x4   // float
#define HALO2_OBJE_DEFAULT_VARIANT  0x30  // string id
#define HALO2_OBJE_MODEL            0x38  // datum of the hlmt (tag reference at 0x34)

// hlmt: model variants (element 0x38, name string id first; checked live:
// the Warthog's default, troop, damaged, gauss)
#define HALO2_HLMT_VARIANTS      0x50
#define HALO2_HLMT_VARIANT_SIZE  0x38

// scnr palettes and squads (checked live on 03b_newmombasa; the squad
// placement reads them at RVA 0x637098, 0x639D44 and 0x6220A5).
#define HALO2_SCNR_VEHICLE_PALETTE   0x78   // block, element 0x28, datum at +4
#define HALO2_SCNR_WEAPON_PALETTE    0x98   // same layout
#define HALO2_PALETTE_ENTRY_SIZE     0x28
#define HALO2_PALETTE_ENTRY_DATUM    0x4
#define HALO2_SCNR_SQUADS            0x160  // block, element 0x74
#define HALO2_SQUAD_SIZE             0x74
#define HALO2_SQUAD_CHARACTER        0x36   // int16 character palette index
#define HALO2_SQUAD_WEAPON           0x3C   // int16 weapon palette index
#define HALO2_SQUAD_SECONDARY        0x3E
#define HALO2_SQUAD_STARTS           0x48   // block of starting locations (HALO2_START_SIZE)
#define HALO2_SCNR_CHARACTER_PALETTE 0x178  // block, element 8, datum at +4
#define HALO2_CHARACTER_PALETTE_SIZE 0x8
// Starting location fields that override the squad's when not -1.
#define HALO2_START_CHARACTER_INDEX  0x20   // int16
#define HALO2_START_WEAPON           0x22   // int16 weapon palette index

// char (checked live on 03b_newmombasa): unit and parent character datums.
// Templates ("ai\generic") have no unit.
#define HALO2_CHAR_PARENT 0x8
#define HALO2_CHAR_UNIT   0x10

// bipd
#define HALO2_BIPD_JUMP_VELOCITY 0x1F8

// weap
#define HALO2_WEAP_AUTOAIM_ANGLE     0x208
#define HALO2_WEAP_AUTOAIM_RANGE     0x20C
#define HALO2_WEAP_MAGNETISM_ANGLE   0x210
#define HALO2_WEAP_MAGNETISM_RANGE   0x214
#define HALO2_WEAP_DEVIATION_ANGLE   0x218
#define HALO2_WEAP_MAGAZINES         0x2C0  // block, element 0x5C
#define HALO2_WEAP_MAGAZINE_DEF_SIZE 0x5C
#define HALO2_MAGDEF_ROUNDS_TOTAL_MAX  0x8
#define HALO2_MAGDEF_ROUNDS_LOADED_MAX 0xA
#define HALO2_MAGDEF_RELOAD_TIME       0x10  // float seconds
#define HALO2_MAGDEF_CHAMBER_TIME      0x18  // float seconds
#define HALO2_WEAP_BARRELS           0x2D0  // block, element 0xEC
#define HALO2_WEAP_BARREL_SIZE       0xEC
#define HALO2_BARREL_PROJECTILE      0x8C   // tag reference
#define HALO2_BARREL_ROUNDS_PER_SECOND 0x4  // float range (min, max)
#define HALO2_BARREL_FIRE_RECOVERY   0x20   // float seconds
// Spread in the barrel tag (Assembly H2MCC weap.xml; the live SMG's values were
// 0.5 / 1.5 / 3 degrees at 0x58 / 0x5C / 0x60 and 1 / 1 / 2.75 at 0x70 / 0x74 / 0x78):
// two sets of "minimum error" + "error angle" (min, max), then the projectile
// distribution angle (pellet spread). Radians.
#define HALO2_BARREL_MIN_ERROR_DUAL   0x58
#define HALO2_BARREL_ERROR_DUAL       0x5C   // 2 floats
#define HALO2_BARREL_DISTRIBUTION     0x6C
#define HALO2_BARREL_MIN_ERROR        0x70
#define HALO2_BARREL_ERROR            0x74   // 2 floats

// vehi (Assembly H2MCC vehi.xml; checked live: a Ghost's max forward speed
// x3 roughly doubled its top speed). Wheeled/tracked vehicles also go by
// the engine's maximum angular velocity.
// Seats (unit part of vehi; Assembly H2MCC vehi.xml): someone can only board
// a seat with "Boarding Seat" (flags bit 11) and a boarding seat index other
// than -1 (both read at RVA 0x8C8458, the index again at 0x93727D).
#define HALO2_VEHI_SEATS              0x1C8  // block, element 0xB0
#define HALO2_VEHI_SEAT_SIZE          0xB0
#define HALO2_SEAT_FLAGS              0x0
#define HALO2_SEAT_FLAG_BOARDING_SEAT 0x800u
#define HALO2_SEAT_BOARDING_INDEX     0x3E   // int16, the seat whose occupant gets pulled out
#define HALO2_UNIT_SEAT               0x210  // int16, seat index in the parent vehicle (RVA 0x93726B)
#define HALO2_VEHI_MAX_FORWARD_SPEED  0x1F4
#define HALO2_VEHI_MAX_REVERSE_SPEED  0x1F8
#define HALO2_VEHI_SPEED_ACCELERATION 0x1FC
#define HALO2_VEHI_SPEED_DECELERATION 0x200
#define HALO2_VEHI_MAX_LEFT_SLIDE     0x22C
#define HALO2_VEHI_MAX_RIGHT_SLIDE    0x230
#define HALO2_VEHI_SLIDE_ACCELERATION 0x234
#define HALO2_VEHI_SLIDE_DECELERATION 0x238
#define HALO2_VEHI_ENGINE_MAX_ANGULAR_VELOCITY 0x264
// Grip: hover/flying vehicles (alien scout physics, RVA 0x915240) push their
// velocity toward where they face with a force capped by these accelerations
// (0x915806). The heading itself turns at any speed (steering, 0x91C6D0, is
// the angle to the aim), so a faster vehicle with the same caps slides wide.

// matg (tag 0)
// matg grenades: block of the grenade types (element 0 = frag, 1 = plasma),
// each with the projectile thrown (tag reference at +0x24).
#define HALO2_MATG_GRENADES           0x100
#define HALO2_MATG_GRENADE_SIZE       0x2C
#define HALO2_MATG_GRENADE_PROJECTILE 0x24
#define HALO2_MATG_PLAYER_INFORMATION 0x130  // block, element 0x11C
#define HALO2_MATG_PLAYER_INFO_SIZE   0x11C
// Speeds: walk 0x24; run forward/back/side/accel 0x2C-0x38; sneak
// forward/back/side/accel 0x3C-0x48; airborne accel 0x4C (checked live).

// scnr
#define HALO2_SCNR_KILL_TRIGGERS 0x230  // block (Cheat Engine "frontier" reads its count)

// sbsp: vehicles are pulled back inside a sphere (0 radius = no limit; checked
// live on 03b_newmombasa: 240 around (-175, 320, 0) blocks Banshees at the bridge)
#define HALO2_SBSP_VEHICLE_LIMIT_RADIUS 0x1C0  // float, then point3 center
// Vehicles below the floor are pushed up (checked live: -3 on 03b_newmombasa
// pinned a Banshee to the tunnel ceiling). 0 seems to mean unset: that map's
// ceiling is 0 and Banshees fly well above it.
#define HALO2_SBSP_VEHICLE_FLOOR   0x1C  // float
#define HALO2_SBSP_VEHICLE_CEILING 0x20  // float

#define HALO2_TAG_SBSP 0x73627370  // 'sbsp'
#define HALO2_TAG_WEAP 0x77656170  // 'weap'
#define HALO2_TAG_PROJ 0x70726F6A  // 'proj'
#define HALO2_TAG_SCNR 0x73636E72  // 'scnr'
#define HALO2_TAG_MATG 0x6D617467  // 'matg'
#define HALO2_TAG_CHAR 0x63686172  // 'char'
#define HALO2_TAG_VEHI 0x76656869  // 'vehi'
#define HALO2_TAG_EQIP 0x65716970  // 'eqip'
#define HALO2_TAG_SCEN 0x7363656E  // 'scen'
#define HALO2_TAG_MACH 0x6D616368  // 'mach'
#define HALO2_TAG_CTRL 0x6374726C  // 'ctrl'
#define HALO2_TAG_BLOC 0x626C6F63  // 'bloc' (crates)

// --- Teams, allegiances, nuke and despawn (added 2026-10-10) -------------------
// Team numbers are HaloScript's "team" enum (read from the enum's string table at
// RVA 0xDFCF80): 0 default, 1 player, 2 human, 3 covenant, 4 flood, 5 sentinel,
// 6 heretic, 7 prophet, 8..15 unused8..unused15. A live actor's team is at actor
// +0x24 (words checked live in "Regret": marine 2, honor guards 7) and its unit's
// at +0x13C; actor_set_team writes both.
#define HALO2_TEAM_FLOOD    4
#define HALO2_TEAM_SENTINEL 5
#define HALO2_TEAM_PROPHET  7
// "Rogue": unused8. Nothing is allied with it. Every team is its own friend (the
// diagonal of the friendly mask below), so, as in CE, the rogue team's own bit is
// cleared to make rogues fight each other too.
#define HALO2_TEAM_ROGUE    8
#define HALO2_ACTOR_TEAM    0x24  // int16 (checked live)

// The allegiance table: a pointer (the global is an 0xE4-byte state block; found
// from ai_allegiance's core 0x619E60 > 0x7568D0, read live in "Regret").
//   +0      u16 record count (at most 8; the add function refuses more)
//   +4      records, 0x14 each: u16 team A, u16 team B, i16 betrayal threshold (-1 =
//           never breaks; 5 for a player allegiance), i16 ticks one betrayal point
//           lasts (5400, from the difficulty), byte +8/+9 who can betray whom, byte
//           +0xA "broken", byte +0xB scratch, u16 +0xC betrayal score, u16 +0xE
//           ticks until it decays (the decay is the tick function 0x756F20)
//   +0xA4   256-bit mask, bit teamA*16+teamB: allegiances in force (set on both
//           orders; a broken one keeps its bit here)
//   +0xC4   256-bit mask, same indexing: "not hostile" (every team's own bit
//           plus allied teams; broken allegiances lose it). 0x757160 reads it
//           inverted as "are enemies"; 0x619F60 (ai_allegiance_broken) is
//           in-A4-but-not-in-C4. Live in "Regret": records (3,7) and (1,2,
//           threshold 5, 5400); C4 rows 1 and 2 = {1,2}, 3 and 7 = {3,7}.
#define OFFSET_HALO2_ALLEGIANCE      0x1650EB8  // char*
#define HALO2_ALLEG_COUNT            0x0
#define HALO2_ALLEG_RECORDS          0x4
#define HALO2_ALLEG_RECORD_SIZE      0x14
#define HALO2_ALLEG_MAX_RECORDS      8
#define HALO2_ALLEG_REC_A            0x0
#define HALO2_ALLEG_REC_B            0x2
#define HALO2_ALLEG_REC_THRESHOLD    0x4   // int16
#define HALO2_ALLEG_REC_BROKEN       0xA   // byte
#define HALO2_ALLEG_REC_SCORE        0xC   // int16
#define HALO2_ALLEG_ALLIED_MASK      0xA4
#define HALO2_ALLEG_FRIENDLY_MASK    0xC4
// ai_allegiance(a, b) core (evaluator 0x7867B0): adds the allegiance with the
// difficulty's betrayal numbers when one side is the player team, then refreshes
// the AI's cached hostility (0x6129A0 walks every actor's perception props of
// those teams, props hold a hostile flag). Static only, same call as the script.
#define OFFSET_HALO2_AI_ALLEGIANCE        0x619E60  // void(int16 a, int16 b)
// ai_allegiance_remove core (0x7867F0 > 0x619F40 > 0x756D50): bool(int16 a, int16 b)
#define OFFSET_HALO2_AI_ALLEGIANCE_REMOVE 0x619F40
// Break/unbreak one allegiance record: (record*, bool broken, bool clear allied bit)
// sets rec +0xA, sets or clears the C4 (and A4) mask bits, refreshes the props.
// The betrayal add (0x756AB0) calls it (record, 1, 0) when the score reaches the
// threshold; (record, 0, 0) puts the allegiance back (what the decay does inline).
#define OFFSET_HALO2_ALLEGIANCE_BREAK     0x756750
// The betrayal add: (team A, team B, kind 0 hit / 1 kill / 2 forgive) from the
// single caller 0x635C10; adds 1 / 3 / -1 to the record's score.
// unit_kill's core (evaluator 0x784FE0 > 0x8FAF30): sets the damage flag 0x20
// (HALO2_DAMAGE_FLAG_KILL) and flags the object changed. Static only.
#define OFFSET_HALO2_UNIT_KILL            0x8FAF30  // void(unit datum)
// ai_erase's per-actor step (0x618CC0, its loop at 0x618D00..0x618D5F): reads the
// actor's unit (+0x18) and +0x1C first, calls 0x624B20(actor datum) (detach the
// actor: drops its weapons etc.), then object_delete(unit) or, with no unit,
// 0x662580(+0x1C value). Static only.
#define OFFSET_HALO2_ACTOR_DELETE         0x624B20  // void(actor datum)
#define OFFSET_HALO2_ACTOR_DELETE_SWARM   0x662580  // void(datum from actor +0x1C)
#define HALO2_ACTOR_SWARM                 0x1C
