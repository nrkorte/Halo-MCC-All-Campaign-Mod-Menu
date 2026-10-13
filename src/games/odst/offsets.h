// Halo 3: ODST (halo3odst.dll, MCC 1.3528). Ported from Halo 3's offsets on
// 2026-10-02: ODST runs the Halo 3 engine but most structures moved. Values
// were found by matching Halo 3 code in ODST (tools/re/odst: port.py maps
// code, evdiff.py / evvote.py compare script evaluators, tagdiff.py compares
// Assembly's tag plugins). RVA comments below are Halo 3's unless they say
// ODST; see NOTES.md for what was checked how.
#pragma once

// halo3odst.dll offsets for MCC 1.3528.0.0. Each one notes where it was
// confirmed so it can be re-found after a game update. "RVA" = address in
// halo3odst.dll relative to its base. Tag layouts are from Assembly's
// Halo3MCC plugins (github.com/XboxChaos/Assembly).

// --- Hooks --------------------------------------------------------------

// Main loop: runs once on the game thread per session (from AlphaRing).
#define OFFSET_ODST_ENGINE 0xC0520
// Per-frame player input update, called from the frame update (RVA 0xB2AA3).
// Engine calls made here run with the frame's per-thread context set up;
// from MCC's input polling they fault (null per-thread table entry).
#define OFFSET_ODST_FRAME_INPUT 0xC1ABC

// --- Engine functions (call on the main thread only) ---------------------
// object_placement_data_new(placement*, tag datum, owner object, 0)  (RVA 0x357DA6 caller)
#define OFFSET_ODST_OBJECT_PLACEMENT_NEW 0x37C954
// object datum object_new(placement*)
#define OFFSET_ODST_OBJECT_NEW           0x37CF34
// object_delete(object datum)  (RVA 0x357E40 caller)
#define OFFSET_ODST_OBJECT_DELETE        0x37E4A4
// bool unit_add_weapon_to_inventory(unit, weapon, mode)
//   mode 1: into a free slot (starting loadout, RVA 0x357E34)
//   mode 3: swap for the held weapon, dropping it (weapon pickup, RVA 0x3B32B6)
//   mode 4: dual wield (weapon pickup, RVA 0x3B3262)
#define OFFSET_ODST_UNIT_ADD_WEAPON      0x39CA58
#define ODST_ADD_WEAPON_FREE_SLOT        1
#define ODST_ADD_WEAPON_SWAP_HELD        3
// unit_kill(unit datum)  (unit_kill evaluator, RVA 0x1E4DA8)
#define OFFSET_ODST_UNIT_KILL            0x3918C8
// object_teleport(object, position*, forward*, up*, bool)  (object_teleport, RVA 0x2596C3)
// object_set_position(object, position*, forward*, up*, location* or null, false): the last
// step of object_teleport (Halo 3 RVA 0x2595F2), which also reconnects the
// object to the map. Null vectors keep the current facing. Noclip uses it:
// the teleport also writes the facing into the unit's aim, and from MCC's
// input polling it faulted halfway (after disconnecting the unit from the
// map), leaving the player invisible (2026-10-02).
#define OFFSET_ODST_OBJECT_SET_POSITION 0x37C330
// object_set_velocity's core (evaluator 0x21602C -> 0x3E0238 local space ->
// here): (object, const float* linear world or null, const float* angular or
// null); writes +0x74/+0x80, updates the physics (0x1760CC) and wakes the
// object. Reach's 0x46CD64. Found statically 2026-10-04.
#define OFFSET_ODST_OBJECT_SET_VELOCITY 0x37C5EC
// Barrel fire(weapon datum, int16 barrel, void*, int, bool): makes the
// barrel's projectiles, reading its projectile tag inside (0x3AF7DB ->
// object_placement_new 0x3AFACA). The weapon update only marks a barrel as
// fired (0x3ACCC8: barrel state +0x1AC bit 2, weapon +0x168 bit 0); after the
// objects update, 0x3A4DE4 -> 0x3A4E4C calls this once per marked barrel, for
// every weapon (AI too). Hooked for the chaos gun and recoil push.
#define OFFSET_ODST_BARREL_FIRE 0x3AF230
#define OFFSET_ODST_OBJECT_TELEPORT      0x284DA8
#define ODST_PLACEMENT_DATA_SIZE         0x400  // real size is ~0x158; oversized buffer
// unit_get_head_position(unit datum, float out[3]): the head marker
// (objects_can_see_object, RVA 0x2587B1)
#define OFFSET_ODST_UNIT_HEAD_POSITION   0x399C38

// Placement fields, as in Halo 3 (RVA 0x6D469: position +0x1C, forward
// +0x28, up +0x34).
#define ODST_PLACEMENT_VARIANT           0xC
#define ODST_PLACEMENT_POSITION          0x1C
#define ODST_PLACEMENT_FORWARD           0x28
#define ODST_PLACEMENT_UP                0x34

// --- Spawning (found statically 2026-10-04; not yet checked live) ------------
// collision_test_vector: Halo 3's 0x1FD748 ported (same signature; result
// fraction at +0x4). Flags: the global the camera loads (Halo 3's 0x222AFC8
// ported, RVA 0x243467).
#define OFFSET_ODST_COLLISION_TEST_VECTOR 0x22F80C
#define OFFSET_ODST_COLLISION_FLAGS       0x227BFC8  // u64
#define ODST_COLLISION_RESULT_FRACTION    0x4
#define ODST_COLLISION_RESULT_SIZE        0x400
// AI: unlike Halo 3, ODST spawns actors from a request, as Reach does. The
// squadless spawn (RVA 0x60005C) builds one with 0x60021C: init (0x5A79A8:
// zeroed 0x6C bytes, indices -1), position +0, yaw/pitch +0x10/+0x14,
// +0x18 = 0, +0x1E = 2, +0x22 = 1, flags +0x1C |= 0x10, character +0x24;
// then actor_spawn(int16 -1, request) (0x5A7A34) and actor_set_active
// (0x5A8DEC). Weapons are tag datums at +0x28/+0x2C (-1 = none), handed to
// the weapon giver at RVA 0x5A82AB.
#define OFFSET_ODST_ACTOR_REQUEST_INIT    0x5A79A8
#define OFFSET_ODST_ACTOR_SPAWN           0x5A7A34
#define ODST_ACTOR_REQUEST_SIZE           0x6C
#define ODST_ACTOR_REQUEST_POSITION       0x0
#define ODST_ACTOR_REQUEST_YAW            0x10
#define ODST_ACTOR_REQUEST_PITCH          0x14
#define ODST_ACTOR_REQUEST_18             0x18   // 0
#define ODST_ACTOR_REQUEST_FLAGS          0x1C
#define ODST_ACTOR_REQUEST_FLAG_FREE      0x10
#define ODST_ACTOR_REQUEST_1E             0x1E   // 2
#define ODST_ACTOR_REQUEST_22             0x22   // 1
#define ODST_ACTOR_REQUEST_CHARACTER      0x24
#define ODST_ACTOR_REQUEST_WEAPON         0x28   // tag datum
#define ODST_ACTOR_REQUEST_SECONDARY      0x2C
// actor_set_active(actor, bool); actor_set_team(actor, int16): actor +0x20
// and its unit's team (+0x16C). Actors: TLS slot 1, 0xAB0 each; unit +0x14.
#define OFFSET_ODST_ACTOR_SET_ACTIVE      0x5A8DEC
#define OFFSET_ODST_ACTOR_SET_TEAM        0x5A8C84
#define ODST_TLS_SLOT_ACTORS              1
#define ODST_ACTOR_UNIT                   0x14
// Designer zones (designer_zone_activate, RVA 0x22A77C; as in Halo 3).
#define OFFSET_ODST_DESIGNER_ZONES_ACTIVE   0x46E2614  // u32 mask
#define OFFSET_ODST_DESIGNER_ZONES_PENDING  0x20FF6D4  // u32 deactivate, u32 activate
#define OFFSET_ODST_ZONE_UPDATE_PENDING     0x20FF6C3  // byte flag

// --- Code patches (found by byte pattern, from the user's Cheat Engine table) --
// AI can't fire: removes the compare before "skip firing", so the skip always happens.
#define ODST_AI_CANT_FIRE_SIZE 5
#define ODST_AOB_AI_CANT_FIRE  "41 0F 2F 4D 14"         // RVA 0x5B0628; patch 6 x NOP
// (The "invisible to AI" patch, "F3 44 0F 10 60 04 F3 44" at RVA 0x5D8D85, blinded every AI to
// everything, allies included. It is no longer applied: "AI can't see you" only sets the
// player's ai_disregard unit flag, see ODST_UNIT_FLAG_AI_DISREGARD.)
// No recoil: skips the call that kicks the player's view after a shot (RVA
// 0x3675B6, in the loop over players using the weapon, so AI are unaffected).
// The Cheat Engine table instead zeroes the minimum recoil at RVA 0x367586.
#define ODST_AOB_RECOIL        "F3 0F 5C C6 8B CF F3 0F 59 D0 F3 0F 58 D6 E8"  // RVA 0x3675A8
#define ODST_RECOIL_CALL_OFFSET 14  // the call within the pattern; patch 5 x NOP
// AI can't hear: an actor hears a sound if hearing range^2 > distance^2; the
// range load at the start of this pattern becomes xorps xmm6,xmm6 (range 0).
// Covers every sound, not just the player's. The Cheat Engine table NOPs the
// load instead, leaving whatever was in xmm6.
#define ODST_AOB_AI_HEARING    "F3 0F 10 70 18 0F 28 C3 F3 0F 59 C3"  // RVA 0x5D94AB
#define ODST_AI_HEARING_PATCH  {0x0F, 0x57, 0xF6, 0x90, 0x90}         // 5 bytes
// Third person: the player director's update (vtable[1], RVA 0x211E80) picks
// the camera mode every tick: following (0) if the unit's seat wants it
// (RVA 0x1326E8, e.g. vehicles), otherwise first person (3). Setting the mode
// from outside (set_camera_third_person) is undone next tick, leaving the
// camera stuck mid-transition at the head. The jne becomes jmp, so it always
// picks following.
#define ODST_AOB_THIRD_PERSON    "41 FF 50 20 BE 01 00 00 00 45 85 FF 75 14"  // RVA 0x2120A1
#define ODST_THIRD_PERSON_OFFSET 12                                          // the jne, RVA 0x2120AD
#define ODST_THIRD_PERSON_PATCH  {0xEB}

// --- Thread-local game state -------------------------------------------
// Byte offset in the TLS block of the per-thread context index. It differs
// between MCC's input polling and the frame; engine code indexes a table
// with it, and the input-polling value selects an empty entry (the fault at
// RVA 0x23300E / 0x9B710).
#define ODST_TLS_THREAD_INDEX 0x18
// Halo 3 keeps its game state in TLS on the main thread. The TLS block is an
// array of pointers; these are slot numbers (byte offset = slot * 8).
#define ODST_TLS_SLOT_PLAYERS           2   // data array (RVA 0xE3CC2 reads +0x10)
#define ODST_TLS_SLOT_OBJECTS           4   // data array (RVA 0x1F73CD reads +0x38)
#define ODST_TLS_SLOT_AI_GLOBALS        7   // ai_grenades, RVA 0x1E6D54 reads +0x30
#define ODST_TLS_SLOT_GAME_GLOBALS      8   // RVA 0x36AD6C reads +0x48
#define ODST_TLS_SLOT_PLAYER_CONTROL   27   // RVA 0xF90F7 reads +0xC0
#define ODST_TLS_SLOT_GAME_TIME        25   // RVA 0x35F226 reads +0xC8
#define ODST_TLS_SLOT_PHYSICS_CONSTANTS 38  // RVA 0x1E43C9 reads +0x130
#define ODST_TLS_SLOT_ALLEGIANCE       47   // team allegiances (RVA 0x199B5A reads +0x180)
#define ODST_TLS_SLOT_DIRECTORS        50   // one per local player (RVA 0x1F642B reads +0x188)
#define ODST_TLS_SLOT_CHUD             52   // HUD globals (chud_show, RVA 0x1F451C reads +0x220)

// HUD globals
#define ODST_CHUD_SHOW 0x144  // byte, 0 hides the HUD (chud_show, RVA 0x1F452B)

// directors (camera control), one per local player
#define ODST_DIRECTOR_SIZE   0x180
#define ODST_DIRECTOR_CAMERA 0x8   // camera object; vtable[0] returns its mode (RVA 0x131B75)
#define ODST_CAMERA_FOLLOWING    0  // third person
#define ODST_CAMERA_FIRST_PERSON 3  // (set_camera_third_person false passes 3)
// Following camera update, vtable[2] (vtable RVA 0x7FB748, set at RVA 0x131F53):
// update(camera*, player index, float dt, observer command*), called by the
// director update (RVA 0x131760). The command is copied to director +0x68 and
// handed to the observer (RVA 0x13199A).
#define OFFSET_ODST_FOLLOWING_CAMERA_UPDATE 0x244118
// Observer command: float distance from the focus point back to the camera
// (RVA 0x2141D9, the length of the unit's camera track offset). The camera's
// own scale at +0x20 only moves the focus point, so it doesn't back off.
#define ODST_OBSERVER_COMMAND_DISTANCE 0x24
#define ODST_OBSERVER_COMMAND_FOCUS_OFFSET 0x10  // float[3] (RVA 0x2141F0)

// allegiance globals: in campaign, team a counts team b as a friend if bit b of
// friends[a] is set (game_team_is_friendly, RVA 0x199B08; ai_allegiance edits it).
// Outside campaign, only the same team is friendly.
#define ODST_ALLEGIANCE_FRIENDS 0x144  // uint32[16]
#define ODST_TEAM_COUNT         16

// game globals
#define ODST_GAME_GLOBALS_MODE   0x10    // byte: 1 = campaign (RVA 0x199B4A)
#define ODST_GAME_MODE_CAMPAIGN  1
#define ODST_GAME_GLOBALS_SKULLS 0xFB10  // 128-bit skull bitfield (RVA 0x36AD75)
#define ODST_SKULL_BANDANNA      23      // infinite ammo (RVA 0x36AD7B)
#define ODST_SKULL_ACROPHOBIA    45      // flight
// The original skulls are kept twice: in MCC's u64 above (primary 0-8,
// secondary 9-15) and in a primary dword +0xFB18 (iron 0 ... mythic 8) and a
// secondary dword +0xFB1C (assassin 0, blind 1, superman/cowbell 2, birthday
// party 3, daddy/IWHBYD 4, third person 5, director's cut 6). 28 engine sites
// read the dwords (0x10AE30/0x10AE64, campaign only), others the u64;
// skull_primary_enable (0x22BCE4) / skull_secondary_enable (0x22BD74) write
// both, with no refresh call. Found statically 2026-10-04.
#define ODST_GAME_GLOBALS_PRIMARY_SKULLS   0xFB18
#define ODST_GAME_GLOBALS_SECONDARY_SKULLS 0xFB1C

// ai globals
#define ODST_AI_GLOBALS_GRENADES 0x3C4  // byte, AI may throw grenades (ai_grenades, RVA 0x1E6D63)

// game time globals
#define ODST_GAME_TIME_TICKS 0xC   // int32 current tick (RVA 0x3846A2)
#define ODST_GAME_TIME_SPEED 0x10  // float, 1.0 = normal (AlphaRing game_time.h)

// physics constants
#define ODST_PHYSICS_GRAVITY 0x0            // float (RVA 0x1E43D8)
#define ODST_DEFAULT_GRAVITY 4.1712594f     // RVA 0x847220 (used by physics_set_gravity)

// player control: one entry per local player
#define ODST_PLAYER_CONTROL_SIZE  0x2C0  // RVA 0xF90FB
#define ODST_PLAYER_CONTROL_YAW   0xD4   // float radians
#define ODST_PLAYER_CONTROL_PITCH 0xD8   // float radians (player0_set_pitch, RVA 0xF9106)

// --- Players (data array entries) ---------------------------------------
#define ODST_PLAYER_UNIT_DATUM 0x28

// --- Objects (every object type) ------------------------------------------
#define ODST_OBJECT_TAG_DATUM     0x0    // tag index of the object's definition
#define ODST_OBJECT_PARENT        0x10   // object this one is attached to (e.g. a vehicle seat), or -1
// Flip ejection (found statically 2026-10-02, the same shape as Halo 2's):
// the unit update 0x37B6C8 compares its ticks upside down with the seat's
// turnover time (seat +0x24) and queues unit action 0x14 through the wrapper
// 0x379C08(unit, type) at 0x37BAA4 (returns to 0x37BAA9).
#define OFFSET_ODST_UNIT_ACTION_QUEUE 0x3C0E24
// Checkpoints, as in Reach: game_save_immediate calls 0x1C92B8(3) (RVA
// 0x1F34D4); game_revert writes 0x0C01 to the word at 0x20B96AA (RVA 0x1F2FC2).
#define OFFSET_ODST_GAME_SAVE        0x1FDFE4
#define ODST_GAME_SAVE_IMMEDIATE     3
#define OFFSET_ODST_GAME_REVERT_FLAG 0x20FF6BA
#define ODST_GAME_REVERT_REQUEST     0x0C01
#define ODST_ACTION_EJECT             0x14
#define ODST_FLIP_EJECT_RETURN        0x3894B7
#define ODST_OBJECT_NEXT_SIBLING  0x08   // attached objects: first child, then siblings (RVA 0x37274B, 0x37278C)
#define ODST_OBJECT_FIRST_CHILD   0x0C
#define ODST_OBJECT_POSITION      0x50   // Vector3
#define ODST_OBJECT_FORWARD       0x5C   // Vector3
#define ODST_OBJECT_UP            0x68   // Vector3
#define ODST_OBJECT_VELOCITY      0x74   // Vector3 (between up 0x68 and angular 0x80)
#define ODST_OBJECT_TYPE          0x96   // byte: 0 biped, 1 vehicle, 2 weapon, ...
#define ODST_OBJECT_PHYSICS_FLAGS 0xA8   // bit 8 = physics disabled (object_set_physics, RVA 0x1E34A6)
#define ODST_OBJECT_HEALTH        0xE8   // float, normalized
#define ODST_OBJECT_SHIELD        0xEC   // float, normalized
#define ODST_OBJECT_DAMAGE_FLAGS  0x104

#define ODST_OBJECT_TYPE_BIPED  0
#define ODST_OBJECT_TYPE_VEHICLE 1
#define ODST_OBJECT_TYPE_WEAPON 2
#define ODST_OBJECT_TYPE_EQUIPMENT 3

#define ODST_PHYSICS_FLAG_DISABLED             0x100u
#define ODST_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE    0x100u
#define ODST_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE 0x10000u
#define ODST_DAMAGE_FLAG_KILL                  0x40u  // set by unit_kill (RVA 0x34D556); the object update then kills

// --- Units (bipeds, vehicles) ---------------------------------------------
#define ODST_UNIT_ACTOR_INDEX     0x160  // -1 if not AI-controlled
#define ODST_UNIT_FLAGS           0x168  // (ai_disregard, RVA 0x1E7732)
#define ODST_UNIT_FLAG_AI_DISREGARD 0x80u  // AI ignores this unit. Read per perception prop by the AI:
    // RVA 0x5A4302 copies unit+0x168 bit 7 into prop+0x2E (and bit 8 into prop+0xC0) each update;
    // the prop code (0x5C3B69, 0x5C3C93, 0x5CF237, 0x5B44FB, 0x5DE56C ...) then skips such props.
#define ODST_UNIT_TEAM            0x16C  // int16 (unit_get_team_index, RVA 0x1E5433)
#define ODST_UNIT_PLAYER_INDEX    0x170  // -1 if not a player (RVA 0x36ADAC)
#define ODST_UNIT_CURRENT_WEAPON  0x276  // int8 (RVA 0x1F73EC)
#define ODST_UNIT_DESIRED_WEAPON  0x27A  // int8; the unit switches to this slot (RVA 0x35A0A2)
#define ODST_PLAYER_WEAPON_SLOTS  2      // players carry 2 (slots 0-1); more fails to add
#define ODST_UNIT_WEAPONS         0x27C  // uint32 datum[4] (RVA 0x3563C5)
#define ODST_UNIT_EQUIPMENT       0x29C  // uint32 datum of carried equipment, or -1 (RVA 0x3509AB)
#define ODST_UNIT_GRENADE_COUNTS  0x2A8  // int8[4] (unit_get_total_grenade_count, RVA 0x1E5FC3)
#define ODST_UNIT_WEAPON_COUNT    4
#define ODST_UNIT_GRENADE_TYPES   4

#define ODST_TEAM_PLAYER 1
#define ODST_TEAM_HUMAN  2
#define ODST_TEAM_COVENANT 3

// --- Weapons --------------------------------------------------------------
#define ODST_WEAPON_HEAT          0x180  // float; overheats at the tag's limit (RVA 0x36728C)
// Weapon state flags (word); bit 0x2 = overheated. The game clears it while
// cooling, once heat drops below the tag's recovery threshold (Halo 3 RVA
// 0x35FEAC). Zeroing heat mid-cooldown skipped that and left the weapon
// unable to fire (ODST beam rifle, 2026-10-02), so no overheat clears it too.
#define ODST_WEAPON_FLAGS       0x168
#define ODST_WEAPON_FLAG_OVERHEATED 0x2u
#define ODST_WEAPON_AGE           0x184  // float; battery drain for energy weapons (RVA 0x3606C0)
// Barrel state, 2 per weapon (RVA 0x35F508: index * 0x34, weapon + 0x1CC).
#define ODST_WEAPON_BARRELS       0x1A8
#define ODST_WEAPON_BARREL_SIZE   0x34
#define ODST_WEAPON_BARREL_COUNT  2
#define ODST_BARREL_TICKS_SINCE_SHOT 0x0 // int8, capped at 0x7F; a shot needs it to reach
                                          // ticks-per-round from the tag's rounds per second (RVA 0x36556B)
#define ODST_BARREL_TICKS_MAX     0x7F
#define ODST_BARREL_FIRE_DELAY    0x2    // int16 recovery ticks after a shot (RVA 0x368004)
#define ODST_BARREL_ERROR         0x1C   // float spread, 0..1, grows while firing (RVA 0x35F5C8)
// Trigger state, 2 per weapon (RVA 0x36008C: index * 12, weapon + 0x21C).
#define ODST_WEAPON_TRIGGERS      0x210
#define ODST_WEAPON_TRIGGER_SIZE  0xC
#define ODST_WEAPON_TRIGGER_COUNT 2
#define ODST_TRIGGER_FLAGS        0x6    // word
#define ODST_TRIGGER_FLAG_READY   0x1u   // set on trigger release, cleared by a semi-auto
                                          // shot (RVA 0x360D94, 0x361093)
#define ODST_WEAPON_MAGAZINES     0x228  // array of magazine state (RVA 0x35F1E5)
#define ODST_WEAPON_MAGAZINE_SIZE 0x18
#define ODST_MAGAZINE_RESERVE     0x6    // int16 rounds not in the magazine (RVA 0x362FCB)
#define ODST_MAGAZINE_LOADED      0xA    // int16 rounds in the magazine (RVA 0x35F212)

// --- Equipment ------------------------------------------------------------
// Using equipment stamps the activation tick; it stays active (then expires)
// until now - activation >= the tag's duration (RVA 0x384078). After a use,
// equipment whose uses left == 0 leaves the unit (RVA 0x357714); every check
// is == 0, so a negative count keeps it. Using equipment, and picking up
// other equipment, needs the carried one's effect to be over (RVA 0x3843D9,
// 0x3579FD).
#define ODST_EQUIPMENT_ACTIVATED  0x16C  // int32 tick of last use, or -1 (RVA 0x3846A8)
#define ODST_EQUIPMENT_USES_SPENT 0x170  // uint8 (RVA 0x3846CE)

// --- Tags -----------------------------------------------------------------
// Tag data = *tag_base + address * 4 (RVA 0x35F1BF).
#define OFFSET_ODST_TAG_BASE          0x2022AA8  // char*
#define OFFSET_ODST_TAG_INDEX_HEADER  0x20F3068  // header* (RVA 0xA6B76)
#define ODST_TAG_HEADER_GROUPS        0x8        // {u32 magic, parent, grandparent, name}* (tag_get, RVA 0xA70D6)
#define ODST_TAG_HEADER_TAG_COUNT     0x10       // int32
#define ODST_TAG_HEADER_INSTANCES     0x18       // {u16 group, u16 salt, u32 address}*
#define ODST_TAG_HEADER_GLOBAL_COUNT  0x20       // int32
#define ODST_TAG_HEADER_GLOBALS       0x28       // {u32 group magic, u32 datum}*  (scnr, matg, ...)

// Tag names aren't kept in memory in normal play (the table at RVA 0xA49148
// is only filled on a debug load path), so they're read from the .map file.
// Map header (Assembly LayoutsU13): file table count/offset/size/index at
// 0x20/0x24/0x28/0x2C; offsets are relative, add offset mask[0] at 0x4CC.
#define ODST_MAP_HEADER_SIZE        0x4000
#define ODST_MAP_FILE_TABLE_COUNT   0x20
#define ODST_MAP_FILE_TABLE_OFFSET  0x24
#define ODST_MAP_FILE_TABLE_SIZE    0x28
#define ODST_MAP_FILE_INDEX_OFFSET  0x2C
#define ODST_MAP_OFFSET_MASK0       0x4CC
// The loaded map's header, copied to memory at map load (RVA 0xA65F6).
#define OFFSET_ODST_MAP_HEADER      0x20EF068
#define ODST_MAP_INTERNAL_NAME      0xC0   // e.g. "010_jungle"

// Tag blocks are {int32 count, uint32 address, int32 unused}.
// Tag references are {u32 group, u32, u32, u32 datum}.
#define ODST_TAGREF_DATUM 0xC

// bipd
#define ODST_BIPD_JUMP_VELOCITY 0x2F0  // float

// vehi physics types (Assembly Halo3MCC vehi.xml): one block per type, only
// the vehicle's own filled in. Vehicle speed scales each type's max speeds
// and acceleration, or its engine's max angular velocity.
// Boost (unit part of vehi): time at peak power, then the cooldown.
// Boost update (RVA 0x34E3E4): vehicle +0x3F4 is the boost timer, +0x3F8 the
// cooldown (0 = none). Boosting lasts rise (+0x28C) + peak time; on release
// or timeout the cooldown is set from dead time.
#define ODST_VEHI_BOOST_PEAK_TIME 0x2B0
#define ODST_VEHI_BOOST_DEAD_TIME 0x2B8
// Seats (unit part of vehi; Assembly Halo3MCC vehi.xml): boarding needs
// "Boarding Seat" (flags bit 11) and a boarding seat index other than -1, as
// in Halo 2. The unit's seat index is at +0x24E (read at RVA 0xF4670).
#define ODST_VEHI_SEATS              0x278  // block, element 0xD4
#define ODST_VEHI_SEAT_SIZE          0xD4
#define ODST_SEAT_FLAGS              0x0
#define ODST_SEAT_FLAG_BOARDING_SEAT 0x800u
#define ODST_SEAT_BOARDING_INDEX     0x42   // int16
#define ODST_UNIT_SEAT               0x262  // int16
// EMP: unit_emp (RVA 0x34D5B0) converts the tag's EMP Disabled Time to ticks
// and stores them at unit +0x37E; 0 ticks means no shutdown.
#define ODST_VEHI_EMP_DISABLED_TIME  0x284  // float, seconds
#define ODST_UNIT_EMP_TICKS          0x392  // int16
#define ODST_VEHI_TANK     0x2E0  // element 0x58: engine max angular velocity +0x30
#define ODST_VEHI_JEEP     0x2EC  // 0x40: engine +0x18
#define ODST_VEHI_PLANE    0x2F8  // 0x4C: forward/reverse speed +0x0/+0x4, acceleration +0x8
#define ODST_VEHI_SCOUT    0x304  // 0x70: +0x8/+0xC/+0x10; engine max vehicle speeds +0x48/+0x5C
#define ODST_VEHI_FIGHTER  0x310  // 0x64: +0x14/+0x18/+0x1C
#define ODST_VEHI_MANTIS   0x328  // 0xAC: +0x14/+0x18/+0x1C
#define ODST_VEHI_VTOL     0x334  // 0x74: max left/forward acceleration +0x44/+0x48
#define ODST_VEHI_CHOPPER  0x340  // 0x70: engine +0x18
#define ODST_VEHI_GUARDIAN 0x34C  // 0x30: +0x8/+0xC/+0x10

// weap
#define ODST_WEAP_MAGAZINES          0x434  // block, element 0x80
#define ODST_WEAP_MAGAZINE_DEF_SIZE  0x80
#define ODST_MAGDEF_ROUNDS_TOTAL_MAX 0x8    // int16
#define ODST_MAGDEF_ROUNDS_LOADED_MAX 0xA   // int16
#define ODST_WEAP_FLAGS              0x19C  // flags32
#define ODST_WEAP_FLAG_NOT_FOR_PLAYER (1u << 23)  // "Cannot Be Used By Player" (turrets, vehicle guns)
// "Melee Only" (energy sword, gravity hammer): bit 18 of the weap flags per the Assembly
// ODSTMCC plugin (flags32 at 0x19C, same word as bit 23 above). Static check: the engine
// reads that bit at RVA 0x3AA0D5 (shr 0x12; and 1) and 0x5AA309 (shr 0x12; not).
#define ODST_WEAP_FLAG_MELEE_ONLY    (1u << 18)
#define ODST_WEAP_AUTOAIM_ANGLE      0x338  // float radians
#define ODST_WEAP_AUTOAIM_RANGE      0x33C
#define ODST_WEAP_AUTOAIM_FALLOFF    0x340
#define ODST_WEAP_DEVIATION_ANGLE    0x350  // how far autoaim may bend a projectile
#define ODST_WEAP_BARRELS            0x44C  // block, element 0x134
#define ODST_WEAP_BARREL_SIZE        0x134
// Barrel firing effects (Assembly Halo3MCC weap.xml): the shot's damage on
// the shooter and on the vehicle's riders, and the effect itself, whose
// accelerations can push the vehicle (as Reach's Scorpion cannon does).
#define ODST_BARREL_FIRING_EFFECTS   0x128  // block, element 0xC4
#define ODST_FIRING_EFFECT_SIZE      0xC4
#define ODST_FIRING_EFFECT_EFFECT           0x4
#define ODST_FIRING_EFFECT_SECONDARY_EFFECT 0x34
#define ODST_FIRING_EFFECT_DAMAGE           0x44
#define ODST_FIRING_EFFECT_SECONDARY_DAMAGE 0x74
#define ODST_FIRING_EFFECT_RIDER_DAMAGE     0x84
#define ODST_FIRING_EFFECT_SECONDARY_RIDER_DAMAGE 0xB4
// effe (Assembly Halo3 effe.xml, the Xbox 360 layout)
#define ODST_EFFE_EVENTS               0x2C   // block, element 0x44
#define ODST_EFFE_EVENT_SIZE           0x44
#define ODST_EFFE_EVENT_ACCELERATIONS  0x2C   // block, element 0x14
#define ODST_EFFE_ACCELERATION_SIZE    0x14
#define ODST_EFFE_ACCELERATION         0x8    // float
#define ODST_BARREL_PROJECTILE       0x98   // tag reference
// Barrel fire also reads these instead of the projectile: the secondary when
// barrel state +0x1AE > 1, the crate projectile whenever it's set, and on
// barrel 0 with weapon +0x1A4 > 0, barrel 1's projectile and secondary.
#define ODST_BARREL_SECONDARY_PROJECTILE 0xA8  // tag reference
#define ODST_BARREL_CRATE_PROJECTILE     0xC8  // tag reference

// eqip
#define ODST_EQIP_USES 0x1A4  // int16 number of uses; -1 unlimited, 0 doesn't count (RVA 0x384199)

// matg (game globals tag)
#define ODST_MATG_GRENADES           0x42C  // block, element 0x34; +0x0 int16 maximum count
#define ODST_MATG_GRENADE_DEF_SIZE   0x34
#define ODST_MATG_PLAYER_INFORMATION 0x444  // block; +0x0 walk, +0x4 run fwd, +0x8 back, +0xC side, +0x10 accel
#define ODST_MATG_PLAYER_INFO_SIZE   0xC4

// obje (every object tag)
#define ODST_OBJE_BOUNDING_RADIUS  0x4
#define ODST_OBJE_DEFAULT_VARIANT  0x30  // string id
#define ODST_OBJE_MODEL            0x40  // datum of the hlmt (tag reference at 0x34)
// hlmt: model variants, name string id first (Assembly ODSTMCC hlmt.xml)
#define ODST_HLMT_VARIANTS         0x64  // block, element 0x48
#define ODST_HLMT_VARIANT_SIZE     0x48
// char: parent and unit datums (references at 0x4 and 0x14)
#define ODST_CHAR_PARENT           0x10
#define ODST_CHAR_UNIT             0x20

// scnr palettes (element 0x10, datum at +0xC), squads and designer zones
// (Assembly ODSTMCC scnr.xml; designer zones at +0x724, 0xC8 each, checked in
// code at RVA 0x213BF5, and the squad count +0x3B8 at RVA 0x600124)
#define ODST_PALETTE_ENTRY_SIZE      0x10
#define ODST_PALETTE_ENTRY_DATUM     0xC
#define ODST_SCNR_SCENERY_PALETTE    0xDC
#define ODST_SCNR_BIPED_PALETTE      0xF4
#define ODST_SCNR_VEHICLE_PALETTE    0x10C
#define ODST_SCNR_EQUIPMENT_PALETTE  0x124
#define ODST_SCNR_WEAPON_PALETTE     0x13C
#define ODST_SCNR_MACHINE_PALETTE    0x160
#define ODST_SCNR_CONTROL_PALETTE    0x1A8
#define ODST_SCNR_CHARACTER_PALETTE  0x3E8
#define ODST_SCNR_CRATE_PALETTE      0x608
#define ODST_SCNR_SQUADS             0x3B8  // element 0x6C
#define ODST_SQUAD_SIZE              0x6C
#define ODST_SQUAD_SINGLES           0x3C   // single locations, element 0x90
#define ODST_SINGLE_SIZE             0x90
#define ODST_SINGLE_CELL             0x10   // int16 designer cell index
#define ODST_SINGLE_CHARACTER        0x32   // int16 palette indices, -1 = the cell's
#define ODST_SINGLE_WEAPON           0x34
#define ODST_SINGLE_SECONDARY        0x36
#define ODST_SQUAD_CELLS             0x54   // designer cells, element 0x84
#define ODST_CELL_SIZE               0x84
#define ODST_CELL_CHARACTERS         0x14   // blocks of {.., +0xC int16 palette index, +0xE int16 chance}, 0x10 each
#define ODST_CELL_WEAPONS            0x20
#define ODST_CELL_SECONDARIES        0x2C
#define ODST_CELL_ENTRY_SIZE         0x10
#define ODST_CELL_ENTRY_INDEX        0xC
#define ODST_CELL_ENTRY_CHANCE       0xE
#define ODST_SCNR_ZONE_SETS          0x74   // element 0x28
#define ODST_SCNR_ZONE_SET_SIZE      0x28
#define ODST_ZONE_SET_FORBIDDEN      0x14   // "runtime unloaded designer zones"
#define ODST_SCNR_DESIGNER_ZONES     0x724  // element 0xC8
#define ODST_SCNR_DESIGNER_ZONE_SIZE 0xC8
#define ODST_DESIGNER_ZONE_BIPEDS    0x8    // blocks of int16 palette indices
#define ODST_DESIGNER_ZONE_VEHICLES  0x14
#define ODST_DESIGNER_ZONE_WEAPONS   0x20
#define ODST_DESIGNER_ZONE_EQUIPMENT 0x2C
#define ODST_DESIGNER_ZONE_SCENERY   0x38
#define ODST_DESIGNER_ZONE_MACHINES  0x44
#define ODST_DESIGNER_ZONE_CRATES    0x80
#define ODST_DESIGNER_ZONE_CHARACTERS 0xB0

// scnr
#define ODST_SCNR_SOFT_CEILINGS      0x268  // block, element 0xC; +0x0 flags16, +0x2 runtime flags16
#define ODST_SCNR_SOFT_CEILING_SIZE  0xC
#define ODST_SCNR_KILL_TRIGGERS      0x4C4  // block of int16 trigger volume indices
#define ODST_SOFT_CEILING_IGNORE_ALL 0xF    // bipeds | vehicles | camera | huge vehicles

#define ODST_TAG_WEAP 0x77656170  // 'weap'
#define ODST_TAG_PROJ 0x70726F6A  // 'proj'
#define ODST_TAG_MATG 0x6D617467  // 'matg'
#define ODST_TAG_SCNR 0x73636E72  // 'scnr' (RVA 0xA6BEE)
#define ODST_TAG_CHAR 0x63686172  // 'char'
#define ODST_TAG_VEHI 0x76656869  // 'vehi'
#define ODST_TAG_EQIP 0x65716970  // 'eqip'
#define ODST_TAG_SCEN 0x7363656E  // 'scen'
#define ODST_TAG_MACH 0x6D616368  // 'mach'
#define ODST_TAG_CTRL 0x6374726C  // 'ctrl'
#define ODST_TAG_BLOC 0x626C6F63  // 'bloc' (crates)
#define ODST_TAG_BIPD 0x62697064  // 'bipd'

// String ids, as Halo 3's (checked offline on h100.map: 10 namespaces).
#define ODST_MAP_STRING_COUNT       0x30
#define ODST_MAP_STRING_DATA        0x34
#define ODST_MAP_STRING_DATA_SIZE   0x38
#define ODST_MAP_STRING_INDEX       0x3C
#define ODST_MAP_STRING_NAMESPACE_COUNT 0x40
#define ODST_MAP_STRING_NAMESPACES  0x44
