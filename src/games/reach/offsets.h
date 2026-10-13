#pragma once

// haloreach.dll offsets for MCC 1.3528.0.0, found statically (not yet tested
// in game). "RVA" = address in haloreach.dll relative to its base. Most were
// found from the HaloScript function table (RVA 0xA28D40, 1882 entries,
// matching Assembly's ReachMCC_U13_Scripting.xml; the evaluator pointer is at
// definition +0x20). Tag layouts are from Assembly's ReachMCC plugins.

// --- Hooks --------------------------------------------------------------

// Main loop: runs on the game thread per session (AlphaRing 1.3528).
#define OFFSET_REACH_ENGINE 0x34818
// Game tick: called once per tick from the main loop's tick loop (RVA
// 0x57535). Engine calls made here run with the frame's context set up.
#define OFFSET_REACH_GAME_TICK 0x575AC
// Per-object update, void(object datum): the objects loops (RVA 0x46C096,
// 0x46C484, 0x46A4E0) call it, and it runs the type's update (animation,
// physics, weapons) through 0x475E3C. Hooked to skip frozen objects (time stop).
#define OFFSET_REACH_OBJECT_UPDATE 0x475DCC
// Checkpoints. game_save_immediate calls 0x1664E0(3) (game_save passes 1,
// RVA 0x1B0D3C); game_revert writes 0x0C01 to the word at 0x263EABB (RVA
// 0x1B08A6), a revert request the main loop acts on.
#define OFFSET_REACH_GAME_SAVE        0x1664E0
#define REACH_GAME_SAVE_IMMEDIATE     3
#define OFFSET_REACH_GAME_REVERT_FLAG 0x263EABB
// Checkpoint slots (found 2026-10-08 from the revert code at 0x16658 and
// checked live): a double buffer. 16-byte entries from RVA 0x27BC4A0 hold a
// pointer to the slot's snapshot at +0x20 and a valid byte at +0x28; the
// int32 at 0x24FB6FC says which slot is current (the revert loads that
// one). Each snapshot is 0xA60000 bytes (10,878,976, the size of
// Configutosave_HaloReach.bin, which is a copy of it) starting with
// 8EA4372E and the map path ("levels\solo\m35\m35") at +8, and a header
// of 0x1EC28 bytes that differs between saves. Writing an older snapshot
// into the current slot, then requesting a revert, goes back to it (done
// by hand: the player landed on the older save's spot).
#define OFFSET_REACH_CHECKPOINT_SLOTS     0x27BC4A0
#define OFFSET_REACH_CHECKPOINT_CURRENT   0x24FB6FC
#define REACH_CHECKPOINT_SLOT_SIZE        16
#define REACH_CHECKPOINT_SLOT_POINTER     0x20
#define REACH_CHECKPOINT_SLOT_VALID       0x28
#define REACH_CHECKPOINT_MAP_NAME         8
#define REACH_GAME_REVERT_REQUEST     0x0C01
// Weapon barrel fire(weapon datum, int16 barrel, void*, bool): called per
// barrel by the weapon update (RVA 0x4B63BE). Hooked to stop AI firing.
#define OFFSET_REACH_BARREL_FIRE 0x4C2710
// Flip ejection, found live (2026-10-02) by logging call stacks on the seat
// exit routine 0x4C9DBC: unit events are dispatched to a handler per unit
// state (0x510E38, lookup 0x510ED4); the handler 0x52FDA0(unit, event*) gets
// event 0x22's data and, if byte +0x19 is set, ejects the unit (seat exit
// called at 0x52FE58) and returns 1. Your own exit is a different path: unit
// action 0xC, detached from 0x530050. Explosions only eject you by flipping.
// Skipping just the seat exit left the unit half-ejected and unable to get
// out at all, so the handler is skipped (returns 0, not handled) for the
// eviction only.
#define OFFSET_REACH_FLIP_EJECT_HANDLER 0x52FDA0
// The handler gets its data from vtable[0](event, 0x22): a unit action
// (type at +0x10). While flipped it runs every tick with action 0x15 and
// +0x19 = 1 (the eviction), and with action 0xC, +0x19 = 0, when you press
// exit; only +0x19 set takes the eject path.
#define REACH_FLIP_EJECT_EVENT      0x22
#define REACH_FLIP_EJECT_DATA_EJECT 0x19
// Following (third-person) camera update, vtable[2] of vtable RVA 0xA2DF68
// (set by director_set_camera_mode, RVA 0xC5A31): update(camera*, player
// index, float dt, observer command*).
#define OFFSET_REACH_FOLLOWING_CAMERA_UPDATE 0x1DCADC
// Observer command (same layout as Halo 3's): focus offset +0x10, distance
// back from the focus +0x24, field of view +0x28 (written via RVA 0x1DD4FC).
#define REACH_OBSERVER_COMMAND_DISTANCE 0x24

// --- Engine functions (call on the main thread only) ---------------------
// Found in the starting-loadout code (RVA 0x48D618).
// object_placement_data_new(placement*, tag datum, owner object, 0)
#define OFFSET_REACH_OBJECT_PLACEMENT_NEW 0x49CC24
// object datum object_new(placement*)
#define OFFSET_REACH_OBJECT_NEW           0x46D6E0
// object_delete(object datum)
#define OFFSET_REACH_OBJECT_DELETE        0x46F0B0
// bool unit_add_weapon_to_inventory(unit, weapon, mode); mode 1 = free slot
#define OFFSET_REACH_UNIT_ADD_WEAPON      0x48DCE0
#define REACH_ADD_WEAPON_FREE_SLOT        1
// Armor abilities, from the starting-loadout code (0x48D7F0): the profile's
// equipment is made like a weapon (owner = the unit), its energy (+0x1EC) set
// to 1.0, run through 0x3A645C(object) and 0x3A664C(object, 0x1000000), then
// attached with 0x48CEC8(unit, equipment). 0x48D230(unit) removes the unit's
// current one (deletes it and clears unit +0x374/+0x378).
#define OFFSET_REACH_UNIT_REMOVE_EQUIPMENT 0x48D230
#define OFFSET_REACH_UNIT_ADD_EQUIPMENT    0x48CEC8
#define OFFSET_REACH_EQUIPMENT_INIT        0x3A645C
#define OFFSET_REACH_EQUIPMENT_SET_FLAGS   0x3A664C
#define REACH_EQUIPMENT_FLAGS_VALUE        0x1000000
// unit_kill(unit, 0, true, 2, 0): the arguments the unit_kill script passes (RVA 0x19DDD1)
#define OFFSET_REACH_UNIT_KILL            0x47CAA8
// object_teleport(object, position*, forward*, up*, bool)  (object_teleport script, RVA 0x1EC73A).
// Unused: for a player it only moves them with bool = true, which runs a
// respawn-style move (RVA 0x49278) that snaps their view level.
#define OFFSET_REACH_OBJECT_TELEPORT      0x1EC468
// object_set_position(object, position*, forward*, up*, 0, false): what
// teleport and the player move (RVA 0x49428) call to place an object; with
// forward/up null it leaves facing, view and camera alone.
#define OFFSET_REACH_OBJECT_SET_POSITION  0x46CB38
// Sets an object's velocity (world space) and its physics' (object, linear*,
// angular* or null); the core of object_set_velocity (evaluator 0x19B554 ->
// 0x4D9BF8 -> here). Writing +0x68 alone is overwritten from the physics
// next tick (checked live on a standing player 2026-10-04).
// object_set_scale(object, float scale, uint16 ticks) (script evaluator RVA
// 0x19B394): sets the object's and its children's scale (a held weapon grows
// with its owner). ticks 0 = at once: the target scale goes to object +0x84
// and the current scale to +0x80 (RVA 0x46CC34), and the object is flagged
// for update (0x46C854).
#define OFFSET_REACH_OBJECT_SET_SCALE     0x4D9B38
#define REACH_OBJECT_SCALE_TARGET         0x84   // float
#define OFFSET_REACH_OBJECT_SET_VELOCITY  0x46CD64
// unit_get_head_position(unit, float out[3])  (objects_can_see_object, RVA 0x1EB9D7)
#define OFFSET_REACH_UNIT_HEAD_POSITION   0x48A020
#define REACH_PLACEMENT_DATA_SIZE         0x400  // real size is ~0x1B0; oversized buffer
// Placement data fields (copied from an object at RVA 0x4C6482): position
// +0x1C, forward +0x28 (default 1,0,0), up +0x34 (default 0,0,1).
#define REACH_PLACEMENT_POSITION          0x1C
#define REACH_PLACEMENT_FORWARD           0x28
// Object definitions (every object tag starts with these)
#define REACH_OBJE_BOUNDING_RADIUS        0xC    // float
#define REACH_OBJE_DEFAULT_VARIANT        0x60   // string id, 0 = the model's first
// Placement data: the model variant to create, by name (object_create_variant
// passes it through RVA 0x47B4D0 into placement +0xC). 0 = the default.
#define REACH_PLACEMENT_VARIANT           0xC
// objects_attach(parent, parent marker, child, child marker): the script
// command's engine call (evaluator RVA 0x19B200). Markers are string ids.
#define OFFSET_REACH_OBJECTS_ATTACH       0x470998
// hlmt (model): variants, each with objects attached to it at markers (how
// a Warthog gets its turret).
#define REACH_HLMT_VARIANTS               0x84   // block, element 0x38
#define REACH_HLMT_VARIANT_SIZE           0x38
#define REACH_HLMT_VARIANT_NAME           0x0    // string id
#define REACH_HLMT_VARIANT_OBJECTS        0x20   // block, element 0x20
#define REACH_HLMT_OBJECT_SIZE            0x20
#define REACH_HLMT_OBJECT_PARENT_MARKER   0x0    // string id
#define REACH_HLMT_OBJECT_CHILD_MARKER    0x4    // string id
#define REACH_HLMT_OBJECT_CHILD           0x18   // object datum (tag reference at +0xC)
// bool collision_test_vector(flags, bool, point*, vector*, ignore object,
// ignore object, -1, result*): the engine's ray test (found 2026-10-02 by
// what projectile, camera and AI code all call; huge stack for the BSP walk).
// Hits within point + vector. Result: fraction along the vector +0x4, hit
// point +0x8 (the observer's camera collision, RVA 0x1DFB0A, reads both).
#define OFFSET_REACH_COLLISION_TEST_VECTOR 0x12969C
#define REACH_COLLISION_RESULT_FRACTION   0x4
#define REACH_COLLISION_RESULT_POINT      0x8
#define REACH_COLLISION_RESULT_SIZE       0x400  // oversized buffer
// The flags the observer camera tests with (the 64-bit constant at RVA
// 0x2902590, read live): world geometry plus the larger object types. Low
// dword = test flags, high dword = object type mask.
#define REACH_COLLISION_FLAGS_CAMERA      0x9A500000809ull

// AI spawning (found 2026-10-02 under ai_place -> 0x6C2B10 -> 0x694CB4):
// 0x6A6958(int16 spawn point, request*) creates one actor and its unit and
// returns the actor datum (actors: TLS slot 1, 0xD00 each). With spawn point
// -1 it uses only the request; 0x73152C spawns squadless actors that way.
// The request (0x6C bytes) is set up by 0x6A6848, then: position +0x0,
// facing yaw/pitch +0x10/+0x14, flags word +0x1C (|= 8 by that caller),
// +0x1E = 2, +0x22 = 1, character tag +0x24.
#define OFFSET_REACH_ACTOR_REQUEST_INIT   0x6A6848
#define OFFSET_REACH_ACTOR_SPAWN          0x6A6958
#define REACH_ACTOR_REQUEST_SIZE          0x6C
#define REACH_ACTOR_REQUEST_POSITION      0x0
#define REACH_ACTOR_REQUEST_YAW           0x10
#define REACH_ACTOR_REQUEST_PITCH         0x14
#define REACH_ACTOR_REQUEST_FLAGS         0x1C
#define REACH_ACTOR_REQUEST_FLAG_FREE     0x8
#define REACH_ACTOR_REQUEST_1E            0x1E   // 2, as 0x7316FC sets it
#define REACH_ACTOR_REQUEST_22            0x22   // 1, likewise
#define REACH_ACTOR_REQUEST_CHARACTER     0x24
// Weapon tags the spawner gives the unit (primary +0x28, secondary +0x2C, -1
// = the character's own; the function at RVA 0x6A57EC makes each weapon
// object and adds it to the unit).
#define REACH_ACTOR_REQUEST_PRIMARY       0x28
#define REACH_ACTOR_REQUEST_SECONDARY     0x2C
// actor_set_active(actor, bool) (actor flags +6 bit 3), called right after
// the spawn (RVA 0x731688); actor_set_team(actor, team): actor +0x2C and its
// unit (ai_set_team's per-actor step, RVA 0x6C1CA3).
#define OFFSET_REACH_ACTOR_SET_ACTIVE     0x6A7D98
#define OFFSET_REACH_ACTOR_SET_TEAM       0x6A7C38
#define REACH_TEAM_COVENANT               3
// actor +0x1C: its unit (checked live). When a spawned actor's unit "dies",
// the game deletes the actor but keeps the unit, then attaches a new actor
// to the same unit ~4 s later at full health (watched live 2026-10-02;
// Spartans at least), holding whatever it still had.
#define REACH_ACTOR_UNIT                  0x1C
#define REACH_ACTOR_CHARACTER             0x6C   // 'char' tag (Kat: spartan_kat; +0x4C held spartan_generic; checked live)
#define REACH_TAG_CHAR                    0x63686172  // 'char'
// Actor fields read by actor_set_team (RVA 0x6A7C38) and the per-actor
// squad move (below); team and squad checked live on m35 2026-10-06 (team
// matches the unit's +0x1CA; squad indices 6..144, -1 = none).
#define REACH_ACTOR_FLAGS                 0x6    // byte; bit 1: drives a swarm (actor_set_team, RVA 0x6A7C8A)
#define REACH_ACTOR_TEAM                  0x2C   // int8
#define REACH_ACTOR_SQUAD                 0x3C   // int32 squad index (squads: TLS slot 8, 0xEC each), -1 = none
// actor_erase(actor, bool): ai_erase's per-actor step (evaluator RVA
// 0x1A1838). Detaches the actor, then deletes its unit (object_delete with
// false, RVA 0x6AA383).
#define OFFSET_REACH_ACTOR_ERASE          0x6AA324
// actor_migrate(actor, int16 squad): ai_migrate's per-actor step (its core
// 0x6BFD80 calls it at RVA 0x6BFDFF for each actor of the source).
#define OFFSET_REACH_ACTOR_MIGRATE        0x6AB15C
// Teams (the script "team" enum). Two teams are enemies unless the second's
// bit is in the first's mask at allegiance +0x164 (RVA 0x1363B0; the
// allegiance table is TLS slot 52). Read live on m35: player and human list
// each other; covenant, brute, mule, spare and covenant_player list only
// themselves, so they are all enemies of each other and of humans.
#define REACH_TEAM_BRUTE                  4
#define REACH_TEAM_MULE                   5
#define REACH_TEAM_SPARE                  6
#define REACH_TEAM_COVENANT_PLAYER        7

// Damage: every hit on an object ends in the cause-damage function (RVA
// 0x4DCB30; the object_cause_damage wrapper at 0x49E980 and the projectile
// path both call it): (damage_data*, float scale, object datum, region
// word, 3 more stack args). The damage_data (built by 0x4FAD7C) starts with
// the effect tag and flags, then the responsible player datum (+8) and
// unit datum (+0xC).
#define OFFSET_REACH_OBJECT_CAUSE_DAMAGE  0x4DCB30
#define REACH_DAMAGE_OWNER_PLAYER         0x8
#define REACH_DAMAGE_OWNER_UNIT           0xC

// Betrayal records (TLS slot 52, the allegiance table, also holds the team
// masks above). A u16 count at +0, then 0x14-byte records from +0: int8
// team A (+4), team B (+5), int16 threshold (+6, 5 for player/human),
// u16 score (+0xE). Hits on the other side add to the score (RVA 0x136708);
// at the threshold the allegiance breaks (0x136B88) and the masks lose the
// other's bit. Read live on m50 2026-10-08: one record, player(1)/human(2).
#define REACH_TLS_SLOT_ALLEGIANCE         52
#define REACH_BETRAYAL_RECORD_SIZE        0x14
#define REACH_BETRAYAL_TEAM_A             0x4
#define REACH_BETRAYAL_TEAM_B             0x5
#define REACH_BETRAYAL_THRESHOLD          0x6
#define REACH_BETRAYAL_SCORE              0xE
#define REACH_ALLEGIANCE_MASKS            0x164

// --- Code patches (found by byte pattern) ---------------------------------
// Third person: the player director's update picks following (0) if the
// unit's seat wants it (RVA 0xC6948, like Halo 3's 0x1326E8), else first
// person (3). The jne becomes jmp, so it always picks following.
#define REACH_AOB_THIRD_PERSON    "41 FF 50 20 45 8A EC 83 7D E7 00 75 5B"  // RVA 0x1348E3
#define REACH_THIRD_PERSON_OFFSET 11                                         // the jne, RVA 0x1348EE
#define REACH_THIRD_PERSON_PATCH  {0xEB}
// Enemies can't see you, from the user's Cheat Engine table: two copies of
// an actor perception record are skipped (NOP the load and the destination).
#define REACH_AOB_AI_INVISIBLE_1  "41 0F 10 00 48 8B D7"  // RVA 0x6F101E; 7 bytes NOP
#define REACH_AOB_AI_INVISIBLE_2  "0F 10 07 48 8B D3 0F"  // RVA 0x6EFA71; first 6 bytes NOP
// No heat, from the Cheat Engine table ("No Plasma Heat"): the store of a
// weapon's heat after a shot adds to it (RVA 0x4C14D9); 8 bytes NOP. Every
// weapon (AI too). Used with no overheat because the per-tick heat reset
// didn't reach a Scorpion's gunner-seat gun (tested 2026-10-02).
#define REACH_AOB_NO_HEAT         "F3 0F 11 83 E0 01 00 00 E8"  // RVA 0x4C14D9

// --- Thread-local game state -------------------------------------------
// TLS block slots (byte offset / 8), from the scripts that read them.
#define REACH_TLS_SLOT_ACTORS          1    // data array, entry 0xD00 (ai_set_blind, RVA 0x6BE26E)
#define REACH_TLS_SLOT_OBJECTS         2    // data array (RVA 0x4739F1)
#define REACH_TLS_SLOT_PLAYERS         3    // data array, entry 0x490 (players, RVA 0x199692)
#define REACH_TLS_SLOT_AI_GLOBALS      6    // ai_grenades, RVA 0x1A0959
#define REACH_TLS_SLOT_GAME_GLOBALS    9    // RVA 0x58B78
#define REACH_TLS_SLOT_GAME_TIME       20   // game_tick_get, RVA 0x1B6FDF
#define REACH_TLS_SLOT_PLAYER_CONTROL  27   // player_set_pitch, RVA 0x65EA7
#define REACH_TLS_SLOT_PHYSICS         102  // physics_set_gravity, RVA 0x19C657
#define REACH_TLS_SLOT_CHUD            182  // chud_show, RVA 0x1B30D5
#define REACH_TLS_SLOT_AIRSTRIKE       62   // airstrike_enable, RVA 0x1B8BEA

// Airstrikes (the target locator, "target_laser"). The weapon fires an
// airstrike_marker_round; where it lands, the engine designates a strike
// (RVA 0x4DB0FC) if the state is ready and launches are left. The update
// (RVA 0x4DAA48) then steps through: 1 delay (tag +0x8 s) -> one launch used
// -> 2/3 fire the tag's missiles (+0x18 count over +0x14 s) -> 4 pause
// (+0xC s) -> 5 cooldown (+0x10 s) -> 0 ready, or 6 when no launches are left.
// Sword Base: 3 s delay, 7 missiles over 4.5 s, 1 s pause, 9.5 s cooldown.
// Only one strike can run at a time: the system is 0x3C bytes (an 8-byte
// header and one 0x34-byte strike, registered at RVA 0x4DA8E2) and both the
// reset (RVA 0x4DA9F0) and the update loop stop after one entry.
#define OFFSET_REACH_AIRSTRIKE_UPDATE 0x4DAA48  // void(): steps the strike one tick (called from the game tick)
#define REACH_AIRSTRIKE_SIZE        0x3C
#define REACH_AIRSTRIKE_SHOTS       0x28  // int32 missiles fired in this strike
#define REACH_AIRSTRIKE_ENABLED     0x0   // byte
#define REACH_AIRSTRIKE_STATE       0x8   // int16
#define REACH_AIRSTRIKE_FREE        0xA   // byte: this strike uses no launch (checked when leaving state 1)
#define REACH_AIRSTRIKE_NO_COOLDOWN 0xB   // byte: skip the cooldown (checked in state 4)
#define REACH_AIRSTRIKE_TIMER       0x10  // int32 ticks; the state moves on once it's <= 0
#define REACH_AIRSTRIKE_LAUNCHES    0x24  // int32, -1 = unlimited (airstrike_set_launches, RVA 0x4DAF64)
#define REACH_AIRSTRIKE_STATE_READY    0
#define REACH_AIRSTRIKE_STATE_DELAY    1
#define REACH_AIRSTRIKE_STATE_PAUSE    4
#define REACH_AIRSTRIKE_STATE_COOLDOWN 5
#define REACH_AIRSTRIKE_STATE_EMPTY    6

// data arrays (Reach layout differs from Halo 3's; RVA 0x137C4)
#define REACH_ARRAY_ENTRY_SIZE 0x20  // int64
#define REACH_ARRAY_HIGH_WATER 0x44  // int32, entries in use are below this
#define REACH_ARRAY_DATA       0x50  // char*
// object table entry: u16 salt, byte type at +4, object* at +0x10 (RVA 0x473A02)
// Object table entry byte +3, bit 0x02: set on every object whose update the
// time stop hook skipped; only a real update clears it, and while it's set the
// object (an AI unit) doesn't move at all. Found live 2026-10-09: all AI read
// 8702 in entry +2..+3 after time stop, the player and healthy AI 8700; clearing
// the bit by hand set a stuck unit walking again. Time stop clears it on release.
#define REACH_OBJECT_ENTRY_SKIPPED_FLAGS 3
#define REACH_OBJECT_ENTRY_SKIPPED_BIT   0x02
#define REACH_OBJECT_ENTRY_TYPE   0x4
#define REACH_OBJECT_ENTRY_OBJECT 0x10

// HUD: chud_show writes this byte 16 times, 0xC60 apart (RVA 0x1B30E5)
#define REACH_CHUD_SHOW        0x320
#define REACH_CHUD_SHOW_STRIDE 0xC60
#define REACH_CHUD_SHOW_COUNT  16

// game globals
#define REACH_GAME_GLOBALS_MODE  0x10  // byte: 1 = campaign (RVA 0x58B7C)
// Active skulls: u64 mask, bit = the script "skull" enum (iron 0 ... blue 16).
// skull_enable (evaluator RVA 0x1B83A0) sets the bit, then calls 0x4E2B98
// (mask*), which refreshes the players' shields for Black Eye (bit 1).
// Checked live 2026-10-04 on m50: Grunt Birthday Party alone read 0x1000.
#define REACH_GAME_GLOBALS_SKULLS 0x1EAC8
#define OFFSET_REACH_SKULLS_CHANGED 0x4E2B98
#define REACH_SKULL_COUNT 17
#define REACH_GAME_MODE_CAMPAIGN 1

// ai globals
#define REACH_AI_GLOBALS_GRENADES 0x3F8  // byte, AI may throw grenades (ai_grenades, RVA 0x1A095F)

// actors
#define REACH_ACTOR_DEAF 0x344  // byte (ai_set_deaf, RVA 0x6BF1CA); +0x343 is blind

// game time
#define REACH_GAME_TIME_TICKS 0xC   // int32 (game_tick_get, RVA 0x1B6FE3)
#define REACH_GAME_TIME_SPEED 0x10  // float, 1.0 = normal (RVA 0x5CB26)

// physics constants
#define REACH_PHYSICS_GRAVITY 0x0  // float (physics_set_gravity, RVA 0x19C662)

// player control: one entry per local player
#define REACH_PLAYER_CONTROL_SIZE  0x180  // RVA 0x65EA0
#define REACH_PLAYER_CONTROL_YAW   0x94   // float radians (RVA 0x631AC)
#define REACH_PLAYER_CONTROL_PITCH 0x98   // float radians (RVA 0x65EB0)

// --- Players --------------------------------------------------------------
#define REACH_PLAYER_UNIT_DATUM 0x28  // RVA 0x1996B5
// int8 team. The unit update copies it to the unit's team (+0x1CA) every
// tick (RVA 0x481638), so writing the unit's alone reverts at once; this
// one sticks (checked live on m35 2026-10-07: 1 = player; set to 3 the
// unit followed and troopers showed red on the motion tracker).
#define REACH_PLAYER_TEAM       0xAC

// --- Objects --------------------------------------------------------------
#define REACH_OBJECT_TAG_DATUM    0x0
#define REACH_OBJECT_PARENT       0x14   // or -1 (RVA 0x4D9A24)
#define REACH_OBJECT_NEXT_SIBLING 0x0C   // attached objects: first child, then siblings (RVA 0x4FB131, 0x4FB173)
#define REACH_OBJECT_FIRST_CHILD  0x10
#define REACH_OBJECT_POSITION     0x44   // Vector3 (RVA 0x471D71)
#define REACH_OBJECT_FORWARD      0x50   // (object_set_velocity, RVA 0x4D9C70)
#define REACH_OBJECT_UP           0x5C
#define REACH_OBJECT_VELOCITY     0x68   // (RVA 0x46CD15)
#define REACH_OBJECT_DAMAGE_FLAGS 0x138
#define REACH_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE    (1u << 7)   // object_cannot_take_damage, RVA 0x4FB120
#define REACH_DAMAGE_FLAG_IGNORES_EMP           (1u << 24)  // object_ignores_emp, RVA 0x19B92A
#define REACH_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE (1u << 18) // object_cannot_die, RVA 0x4FB0AE
#define REACH_DAMAGE_FLAG_DEAD                  (1u << 2)   // ai_resurrect skips units without it (RVA 0x1A16B2)

// AI revives: the engine revives units on an 8-entry list in the AI globals
// (TLS slot 6), e.g. Kat on Sword Base and every Spartan the mod spawns.
// An actor spawned with the revive flag is added by RVA 0x6A669C (unit at
// +0x0, which also sets object +0x143 bit 0); RVA 0x6923CC empties an entry
// as written below. Emptying Kat's entry and clearing her cannot-die flag let
// her die for good (tested 2026-10-03).
#define REACH_AI_REVIVES          0x43C  // in the AI globals
#define REACH_AI_REVIVE_SIZE      0x18
#define REACH_AI_REVIVE_COUNT     8
#define REACH_AI_REVIVE_UNIT      0x0    // datum, -1 = free
#define REACH_OBJECT_REVIVABLE    0x143  // byte, bit 0 set while on the revive list
// Vitality is a list of damage sections at a self-relative offset: int16 size
// in bytes at +0x184, int16 offset at +0x186; 0x18 bytes each, current
// vitality (normalized) at +0x10. Section 0 is health (object_get_health,
// RVA 0x49DC41); the shield section is chosen from the model (RVA 0x49DDEC).
#define REACH_OBJECT_SECTIONS_SIZE   0x184
#define REACH_OBJECT_SECTIONS_OFFSET 0x186
#define REACH_SECTION_SIZE           0x18
#define REACH_SECTION_VITALITY       0x10

#define REACH_OBJECT_TYPE_BIPED     0
#define REACH_OBJECT_TYPE_VEHICLE   1
#define REACH_OBJECT_TYPE_WEAPON    2
#define REACH_OBJECT_TYPE_EQUIPMENT 3
#define REACH_OBJECT_TYPE_PROJECTILE 5
// Equipment object +0xCC: -1 until the ability is used, then a value (seen
// live as 0x180080E1 on the drop shield bubble the moment it was activated).
#define REACH_EQUIPMENT_ACTIVE_HANDLE 0xCC
#define REACH_OBJECT_SPHERE_CENTRE    0x20   // float[3], the object's bounding sphere
#define REACH_OBJECT_SPHERE_RADIUS    0x3C   // float (a Spartan reads 0.43, the drop shield bubble 1.8)

// --- Units ------------------------------------------------------------------
#define REACH_UNIT_ACTOR             0x1BC  // datum, -1 if not AI-controlled
#define REACH_UNIT_FLAGS             0x1C4
#define REACH_UNIT_FLAG_AI_DISREGARD 0x20u   // ai_disregard, RVA 0x1A1FC2
#define REACH_UNIT_TEAM              0x1CA  // int8 (unit_get_team_index, RVA 0x19E7A8)
#define REACH_UNIT_PLAYER_INDEX      0x1CC  // -1 if not a player
// Active camo's effectiveness on the unit: current (+0x49C) and previous (+0x4A0)
// value, 1.0 = perfect. Found live 2026-10-09 by diffing a standing and a running
// player with active camo on: both read 1.0 still and fell to 0.57-0.85 while moving.
// The camo is on while the equipment's active handle is set and the value is above 0.
#define REACH_UNIT_CAMO_VALUE        0x49C  // float
#define REACH_UNIT_CAMO_PREVIOUS     0x4A0  // float
#define REACH_UNIT_EMP_STUN          0x4B0  // int16 ticks left; > 0 = shut down (unit_is_emp_stunned, RVA 0x19DF58)
#define REACH_UNIT_CURRENT_WEAPON    0x34A  // int8 (RVA 0x48323A)
#define REACH_UNIT_DESIRED_WEAPON    0x34E  // int8 (weapon cycling, RVA 0x4825E5)
#define REACH_UNIT_WEAPONS           0x350  // uint32 datum[4] (unit_has_weapon, RVA 0x48E2F6)
#define REACH_UNIT_EQUIPMENT         0x374  // datum of the armor ability (RVA 0x48D305)
#define REACH_UNIT_GRENADE_COUNTS    0x37E  // int8[2] (unit_get_total_grenade_count, RVA 0x19F730)
#define REACH_UNIT_WEAPON_COUNT      4
#define REACH_UNIT_GRENADE_TYPES     2
#define REACH_PLAYER_WEAPON_SLOTS    2

// Campaign teams (Reach has no allegiance switching like Halo 3's).
#define REACH_TEAM_PLAYER 1
#define REACH_TEAM_HUMAN  2

// --- Weapons ----------------------------------------------------------------
#define REACH_WEAPON_HEAT          0x1E0  // float (Cheat Engine "no plasma heat")
#define REACH_WEAPON_AGE           0x1E4  // float, battery used (Cheat Engine "inf plasma ammo")
#define REACH_WEAPON_BARRELS       0x240  // 0x34 each (spread at RVA 0x4C43F0)
#define REACH_WEAPON_BARREL_SIZE   0x34
#define REACH_WEAPON_BARREL_COUNT  2
#define REACH_BARREL_ERROR         0x1C   // float spread (RVA 0x4C4562)
// Rapid fire resets these on the players' weapons every tick. They're what
// the Cheat Engine table's rapid fire stops the game resetting after a shot
// (RVAs 0x4C0B79, 0x4C20AD, 0x4C21DD); that patch hit every weapon, the AI's
// too, so it isn't used.
#define REACH_BARREL_TICKS_SINCE_SHOT 0x0 // int8, capped at 0x7F
#define REACH_BARREL_TICKS_MAX     0x7F
#define REACH_BARREL_FIRE_DELAY    0x2    // int16 recovery ticks after a shot
#define REACH_WEAPON_TRIGGERS      0x2A8  // 0xC each (RVA 0x4C1B85)
#define REACH_WEAPON_TRIGGER_SIZE  0xC
#define REACH_WEAPON_TRIGGER_COUNT 2
#define REACH_TRIGGER_STATE        0x0    // byte: 1 charging, 2 charged (fires) (checked live, target laser)
#define REACH_TRIGGER_STATE_CHARGING 1
#define REACH_TRIGGER_FLAGS        0x1    // byte
#define REACH_TRIGGER_FLAG_READY   0x1u   // needed to fire; cleared by a semi-auto shot (RVA 0x4B7A7C)
#define REACH_TRIGGER_CHARGE_TICKS 0x4    // int16 charge ticks left (target laser: 102 = 1.7 s, checked live)
#define REACH_WEAPON_MAGAZINES     0x2C0  // 0x1A each (RVA 0x4C0E33)
#define REACH_WEAPON_MAGAZINE_SIZE 0x1A
#define REACH_MAGAZINE_RESERVE     0x6    // int16 (RVA 0x51CCB)
#define REACH_MAGAZINE_LOADED      0xA    // int16

// --- Equipment (armor abilities) --------------------------------------------
// eqip tag: the "Proximity Mine" block's Self Destruct Time (seconds after
// creation the object removes itself; 0 = never). The drop_shield_shield
// bubble has 15 here; every live bubble died exactly 15 s after it was made
// (found live 2026-10-08), whatever the equipment's own duration (-1).
#define REACH_EQIP_PROXIMITY_MINE      0x2A4  // block, element 0x30
#define REACH_EQIP_MINE_SIZE           0x30
#define REACH_EQIP_MINE_SELF_DESTRUCT  0x24   // float
#define REACH_EQUIPMENT_ENERGY 0x1EC  // float, 1.0 = full (Cheat Engine "infinite armor ability", RVA 0x4B48C9)

// --- Tags -------------------------------------------------------------------
// Tag data = bases[address >> 28] + address * 4 (tag_get, RVA 0x31AE8).
#define OFFSET_REACH_TAG_BASES       0x4E39F20  // char*[16]
#define OFFSET_REACH_TAG_INSTANCES   0xC1A600   // {i16 group, u16 salt, u32 address}*
#define OFFSET_REACH_TAG_GROUPS      0x263EA48  // char*: tag count and group table
#define REACH_TAG_GROUPS_COUNT       0x3FFF8    // int32 tag count
#define REACH_TAG_GROUPS_TABLE       0x3FFFC    // {magic, parent, grandparent, ?}[group]
#define OFFSET_REACH_SCENARIO        0xC1A230   // char* scnr data (RVA 0x31613)
#define OFFSET_REACH_GLOBALS         0xC1A238   // char* matg data (RVA 0x31631)
// Loaded cache files: an array of 0xA0D0-byte slots, each starting with the
// map header (RVA 0x31455, 0x31E04).
#define OFFSET_REACH_MAP_SLOTS       0x25BDA30  // char*
#define OFFSET_REACH_MAP_SLOT_COUNT  0x25BDA38  // int32
#define REACH_MAP_SLOT_SIZE          0xA0D0
#define REACH_MAP_SLOT_HEADER        0x10   // the header follows two pointers (checked live)
#define REACH_MAP_INTERNAL_NAME      0xC0

// Map file header (Assembly ReachMCC LayoutsU13): same fields as Halo 3's.
#define REACH_MAP_HEADER_SIZE        0xA000
#define REACH_MAP_FILE_TABLE_COUNT   0x20
#define REACH_MAP_FILE_TABLE_OFFSET  0x24
#define REACH_MAP_FILE_TABLE_SIZE    0x28
#define REACH_MAP_FILE_INDEX_OFFSET  0x2C
#define REACH_MAP_OFFSET_MASK0       0x4CC
// String ids (Assembly's ReachMCC LayoutsU13 header). An id is namespace
// (bits 17-24) and index; namespace 0 indices past its count are stored
// after every namespace (checked live on m35: 0x7EC = "rocket").
#define REACH_MAP_STRING_COUNT       0x30
#define REACH_MAP_STRING_DATA        0x34
#define REACH_MAP_STRING_DATA_SIZE   0x38
#define REACH_MAP_STRING_INDEX       0x3C
#define REACH_MAP_STRING_NAMESPACE_COUNT 0x40
#define REACH_MAP_STRING_NAMESPACES  0x44  // int32 string count per namespace

#define REACH_TAGREF_DATUM 0xC

// bipd
#define REACH_BIPD_JUMP_VELOCITY 0x4F0  // float

// vehi physics types (Assembly ReachMCC vehi.xml): one block per type, only
// the vehicle's own filled in. Vehicle speed scales each type's max speeds
// and acceleration, or its engine's max angular velocity. (VTOL, the Falcon,
// has no plain speed fields and isn't scaled.)
#define REACH_VEHI_TANK      0x4D0  // element 0x70: engine max angular velocity +0x3C
#define REACH_VEHI_JEEP      0x4DC  // 0x58: engine +0x18
#define REACH_VEHI_PLANE     0x4E8  // 0x4C: forward/reverse speed +0x0/+0x4, acceleration +0x8
#define REACH_VEHI_WOLVERINE 0x4F4  // 0x60: engine +0x18
#define REACH_VEHI_SCOUT     0x500  // 0x74: +0x8/+0xC/+0x10; engine max vehicle speeds +0x4C/+0x60
#define REACH_VEHI_FIGHTER   0x50C  // 0x64: +0x14/+0x18/+0x1C
#define REACH_VEHI_MANTIS    0x524  // 0xAC: +0x14/+0x18/+0x1C
#define REACH_VEHI_CHOPPER   0x53C  // 0x7C: engine +0x18
#define REACH_VEHI_GUARDIAN  0x548  // 0x30: forward/reverse speed +0x8/+0xC, acceleration +0x10
#define REACH_VEHI_BOAT      0x560  // 0x60: engine max angular velocity +0x24
#define REACH_VEHI_SPACE_FIGHTER 0x56C  // 0xC4 (Sabre): full/neutral/reverse throttle speed +0x14/+0x18/+0x1C, acceleration +0x20
#define REACH_VEHI_REVENANT  0x578  // 0xA8: engine +0x3C, forward/reverse speed +0x70/+0x74, acceleration +0x78
// Seats (unit part of vehi): flags +0x0, bit 1 "Locked": when occupied,
// enemies can't enter (board) the seat.
// Boost: power burned per second (1 = all of it in a second).
#define REACH_VEHI_BOOST_POWER_PER_SECOND 0x48C
#define REACH_VEHI_SEATS       0x444  // block, element 0x12C
#define REACH_VEHI_SEAT_SIZE   0x12C
#define REACH_SEAT_FLAG_LOCKED 0x2u
#define REACH_UNIT_SEAT        0x336  // int16, seat index in the parent vehicle (written at RVA 0x4C9165)
#define REACH_SEAT_FLAG_BOARDING_SEAT 0x400u  // bit 10: a seat others board through (read at RVA 0x4C58D, 0x104080)
// AI seat reservations on a vehicle object: a bitmask, all ones = reserved
// for no AI (ai_vehicle_reserve, RVA 0x6C1EE8).
#define REACH_VEHICLE_AI_RESERVED 0xB80  // uint64

// object definitions
#define REACH_OBJE_MODEL 0x64  // tag reference

// hlmt (model): shield section lookup (RVA 0x49DE62)
#define REACH_HLMT_MAX_SHIELD      0xD0   // float; > 0 if the model has shields
#define REACH_HLMT_SECTIONS        0xE0   // block, element 0xC0
#define REACH_HLMT_SECTION_SIZE    0xC0
#define REACH_HLMT_SECTION_SHIELD  0xC    // int32; not 0/-1 marks the shield section

// weap
#define REACH_WEAP_FLAGS              0x204
#define REACH_WEAP_FLAG_NOT_FOR_PLAYER (1u << 22)
#define REACH_WEAP_FLAG_MELEE_ONLY    (1u << 18)  // energy sword, gravity hammer
// char: a character inherits from its parent. One with no unit anywhere up
// the chain is a template or behaviour preset ("generic", "*_aggressive",
// "spartan_speed_7"), not something that can be spawned (46 of 77 on m20;
// found live 2026-10-03).
#define REACH_CHAR_PARENT             0x10   // tag reference datum (ref at +0x4)
#define REACH_CHAR_UNIT               0x20   // tag reference datum (ref at +0x14), -1 = the parent's
#define REACH_WEAP_AUTOAIM_ANGLE      0x2D4
#define REACH_WEAP_AUTOAIM_RANGE      0x2D8
#define REACH_WEAP_AUTOAIM_FALLOFF    0x2DC
#define REACH_WEAP_DEVIATION_ANGLE    0x2F4
#define REACH_WEAP_MAGAZINES          0x3F0  // block, element 0x60
#define REACH_WEAP_MAGAZINE_DEF_SIZE  0x60
#define REACH_MAGDEF_ROUNDS_TOTAL_MAX 0x8
#define REACH_MAGDEF_ROUNDS_LOADED_MAX 0xA
#define REACH_WEAP_TRIGGERS           0x3FC  // block, element 0x8C
#define REACH_WEAP_TRIGGER_DEF_SIZE   0x8C
#define REACH_TRIGDEF_BEHAVIOR        0x6    // int16
#define REACH_TRIGGER_SPEW_CHARGE     6      // the target locator's "hold to lock on"
#define REACH_WEAP_BARRELS            0x408  // block, element 0x184
#define REACH_WEAP_BARREL_SIZE        0x184
#define REACH_BARREL_PROJECTILE       0x104  // tag reference
#define REACH_BARREL_FIRING_EFFECTS   0x178  // block, element 0xC4
#define REACH_FIRING_EFFECT_SIZE      0xC4
#define REACH_FIRING_EFFECT_DAMAGE    0x44   // tag reference: the shot's kick on the shooter
// Also cleared for vehicle guns: the optional secondary firing damage and
// the rider responses (what the seated players feel).
#define REACH_FIRING_EFFECT_SECONDARY_DAMAGE       0x74
#define REACH_FIRING_EFFECT_RIDER_RESPONSE         0x84
#define REACH_FIRING_EFFECT_SECONDARY_RIDER_DAMAGE 0xB4
// The firing effects themselves (effe tags) can push the shooter: the
// Scorpion cannon's has an acceleration of -15 (the tank lurching back).
#define REACH_FIRING_EFFECT_EFFECT           0x4    // tag reference (effe)
#define REACH_FIRING_EFFECT_SECONDARY_EFFECT 0x34
#define REACH_EFFE_EVENTS               0x2C   // block, element 0x40
#define REACH_EFFE_EVENT_SIZE           0x40
#define REACH_EFFE_EVENT_ACCELERATIONS  0x28   // block, element 0x14
#define REACH_EFFE_ACCELERATION_SIZE    0x14
#define REACH_EFFE_ACCELERATION         0x8    // float

// matg
#define REACH_MATG_GRENADES           0x444  // block, element 0x38; +0x0 int16 maximum count
#define REACH_MATG_GRENADE_DEF_SIZE   0x38
#define REACH_MATG_GRENADE_PROJECTILE 0x28  // tag reference to the projectile thrown (element 0 = frag)
// Field of view (found 2026-10-13): two floats in the module's data, degrees. The
// first is the first-person field of view (78 by default, MCC's own option), the
// second the third-person one (79). The camera follows them at once; no clamp was
// seen up to 120.
#define REACH_FOV_SETTING             0x2A03CDC
#define REACH_FOV_MIN                 65
#define REACH_FOV_MAX                 140
#define REACH_FOV_DEFAULT             78
// Explosion acceleration (found 2026-10-13). Hidden skull bit 11 ("Superman (internal
// name)") makes explosions push much harder (user-confirmed). Two places read it:
// RVA 0x4E3818 returns the acceleration magnitude a damage gives an object (its
// `addss xmm1, xmm1` doubles it with the skull), and RVA 0x5233A0 pushes loose
// objects away from a blast (factor 5.0, 10.0 with the skull; the load at 0x5234C3).
// The mod hooks the first (scaling its result) and points the second's load at its
// own float.
#define OFFSET_REACH_EXPLOSION_ACCEL  0x4E3818
#define REACH_AOB_EXPLOSION_PUSH      "F3 0F 10 15 ?? ?? ?? ?? B8 48 00 00 00 4A 8B 04 30 8B 88 C8 EA 01 00 48 C1 E9 0B F6 C1 01"  // RVA 0x5234C3
#define REACH_EXPLOSION_PUSH_FACTOR   5.0f
#define REACH_MATG_PLAYER_INFORMATION 0x468  // block, element 0xF4; first 10 floats are speeds
#define REACH_MATG_PLAYER_INFO_SIZE   0xF4

// scnr
#define REACH_SCNR_SOFT_CEILINGS      0x25C  // block, element 0xC; +0x0 flags16, +0x2 runtime flags16
#define REACH_SCNR_SOFT_CEILING_SIZE  0xC
#define REACH_SCNR_KILL_TRIGGERS      0x4C8  // block
#define REACH_SOFT_CEILING_IGNORE_ALL 0xF
// Designer zones: named sets of extra tags (one vehicle, character or
// weapon each) that a mission loads part-way through. Each zone set lists
// the designer zones it loads; a tag only in an unloaded one can't be made
// (found 2026-10-03 on Sword Base: the hunter is zone 14, loaded from
// zoneset_courtyard_return on).
#define REACH_SCNR_ZONE_SETS          0xAC   // block, element 0x13C
#define REACH_SCNR_ZONE_SET_SIZE      0x13C
#define REACH_ZONE_SET_DESIGNER_LOADED   0x11C  // flags32 designer zones this zone set allows
#define REACH_ZONE_SET_DESIGNER_UNLOADED 0x120  // flags32 designer zones a switch to it unloads (RVA 0x14F13)
#define REACH_SCNR_DESIGNER_ZONES     0x740  // block, element 0xBC
#define REACH_SCNR_DESIGNER_ZONE_SIZE 0xBC
#define REACH_DESIGNER_ZONE_BIPEDS    0x8    // block of int16 palette indices
#define REACH_DESIGNER_ZONE_VEHICLES  0x14
#define REACH_DESIGNER_ZONE_WEAPONS   0x20
#define REACH_DESIGNER_ZONE_EQUIPMENT 0x2C
#define REACH_DESIGNER_ZONE_SCENERY   0x38
#define REACH_DESIGNER_ZONE_MACHINES  0x44
#define REACH_DESIGNER_ZONE_CRATES    0x74
#define REACH_DESIGNER_ZONE_CHARACTERS 0xA4
#define REACH_SCNR_BIPED_PALETTE      0x120  // blocks, element 0x10: a tag reference
#define REACH_SCNR_VEHICLE_PALETTE    0x138
#define REACH_SCNR_EQUIPMENT_PALETTE  0x150
#define REACH_SCNR_SCENERY_PALETTE    0x108
#define REACH_SCNR_MACHINE_PALETTE    0x18C
#define REACH_SCNR_CRATE_PALETTE      0x60C
#define REACH_SCNR_WEAPON_PALETTE     0x168
#define REACH_SCNR_CHARACTER_PALETTE  0x3EC
#define REACH_PALETTE_ENTRY_SIZE      0x10
#define REACH_PALETTE_ENTRY_DATUM     0xC
// The designer_zone_activate script command (RVA 0x1B7038) only takes
// zones the zone set allows; the mod writes the pending masks itself.
// The game's zone update (RVA 0x33CCE) sees the flag and applies
// active = (active & ~deactivate) | activate (RVA 0x154BC) the next frame.
// A tag's objects are deleted when its zone unloads.
#define OFFSET_REACH_DESIGNER_ZONES_ACTIVE   0x4E2FB24  // u32 mask
#define OFFSET_REACH_DESIGNER_ZONES_PENDING  0x263EAD8  // u32 deactivate, u32 activate
#define OFFSET_REACH_ZONE_UPDATE_PENDING     0x263EAC4  // byte flag
#define OFFSET_REACH_ZONE_SET_INDEX          0xAFBE30   // int32 current zone set

#define REACH_TAG_WEAP 0x77656170  // 'weap'
#define REACH_TAG_VEHI 0x76656869  // 'vehi'
#define REACH_TAG_HLMT 0x686C6D74  // 'hlmt'
// vehi seats. A Pelican's or Phantom's driver seat is flagged invalid for
// players; clearing that is all it takes to fly one (tested live on m35
// 2026-10-03: handles like a Falcon, third-person camera, exits normally).
#define REACH_VEHI_SEATS                  0x444  // block, element 0x12C
#define REACH_VEHI_SEAT_SIZE              0x12C
#define REACH_SEAT_FLAGS                  0x0
#define REACH_SEAT_FLAG_INVISIBLE         (1u << 0)  // inside the hull (a Phantom's pilot): no way out for a player
#define REACH_SEAT_FLAG_DRIVER            (1u << 2)
#define REACH_SEAT_FLAG_INVALID_FOR_PLAYER (1u << 14)
#define REACH_SEAT_NAME                   0x4    // string id ("seat animation"; scripts name seats by it)
// unit_enter_vehicle(unit, vehicle, seat name, immediate): the script
// command's engine call (unit_enter_vehicle_immediate, RVA 0x19EA64).
// Immediate first takes the unit out of its current seat, and skips the
// seat's checks (RVA 0x4F9907), so "Invalid For Player" doesn't stop it.
#define OFFSET_REACH_UNIT_ENTER_VEHICLE   0x4F97BC
// A player can't leave a Phantom's pilot seat by any exit request: the
// immediate-enter eject (0x15), unit_exit_vehicle (RVA 0x4F9A74, every
// mode) and vehicle_unload (RVA 0x4F9F38; the same action 0xC) all do
// nothing, and clearing the seat's invisible/locked flags didn't help
// (tested 2026-10-03). The flip eviction (RVA 0x52FE34) ejects with no
// checks: 0x4C7BB0(unit, 1, 0), then the seat exit 0x4C9DBC(unit, 0, 0, 0,
// 0, int* out = -1); calling those from the mod's tick crashed MCC.
// Deleting the vehicle deletes a unit seated in it (the player too).
// objects_detach(child)'s engine call (evaluator RVA 0x19B300): runs the
// object type's detach callback (a unit leaves its seat), then the generic
// detach.
#define OFFSET_REACH_OBJECT_DETACH        0x47133C
// The unit in a vehicle's seat (vehicle, seat index), or -1: what seating
// checks before taking a seat (called at RVA 0x4F98FD).
#define OFFSET_REACH_SEAT_OCCUPANT        0x486290
#define REACH_TAG_BIPD 0x62697064  // 'bipd'
#define REACH_TAG_EQIP 0x65716970  // 'eqip'
#define REACH_TAG_SCEN 0x7363656E  // 'scen'
#define REACH_TAG_MACH 0x6D616368  // 'mach'
#define REACH_TAG_BLOC 0x626C6F63  // 'bloc' (crate)
#define REACH_TAG_PROJ 0x70726F6A  // 'proj'
