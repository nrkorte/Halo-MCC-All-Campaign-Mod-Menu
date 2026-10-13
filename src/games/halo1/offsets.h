#pragma once

// halo1.dll (Halo: CE) offsets for MCC 1.3528.0.0, found statically. "RVA" =
// address in halo1.dll relative to its base. Script functions were found
// from the HaloScript function table (RVA 0x18960F0; each definition holds
// its own name at +0x8 and its evaluator at +0x18). Tag layouts are from
// Assembly's Halo1/Halo1MCC plugins.
//
// Unlike Halo 3 and Reach, CE keeps its game state in plain globals (no
// TLS). Object fields are the classic 32-bit CE layout minus 0x44 (health
// 0xE0 -> 0x9C, damage flags 0x106 -> 0xC2, parent 0x11C -> 0xD8).

// --- Hooks --------------------------------------------------------------
// Game tick: the main update loop (RVA 0xAB0D10) calls it once per tick,
// then increments the tick count (RVA 0xAB0E7B).
#define OFFSET_HALO1_GAME_TICK 0xAAF7D0
// Checkpoints. game_save_totally_unsafe writes 1 to the word at 0x2B23704
// (RVA 0xB1E1A6), an immediate save request; game_revert calls 0xAC360C()
// (RVA 0xB1E1DC).
#define OFFSET_HALO1_GAME_SAVE_NOW_FLAG 0x2B23704
#define OFFSET_HALO1_GAME_REVERT        0xAC360C
// unit_exit_vehicle's core (unit, bool): unit_exit_vehicle (0xB06EC8) calls
// it after checking the unit has a parent (+0xD8) and a seat (+0x2D0).
#define OFFSET_HALO1_UNIT_EXIT_VEHICLE 0xB07C84
// Takes a unit out of its seat (writes the seat index +0x2D0 at 0xB0810B).
// Four register args: unit, then three bools. Logged live (2026-10-02): the
// unit update 0xBAF258 ejects a seated unit every tick while its vehicle's up
// vector z (+0x44) is below 0, calling this at 0xBAF475 (returns to
// 0xBAF47A); your own exit is a control flag (+0x1D8 bit 0x40) handled
// through 0xB07C84 instead.
#define OFFSET_HALO1_UNIT_DETACH_SEAT 0xB07E54
#define HALO1_FLIP_EJECT_RETURN       0xBAF47A

// --- Engine functions (call on the game thread only) ---------------------
// From the starting-loadout code (player_add_equipment, RVA 0xAD0232) and
// cheat_all_weapons (RVA 0xB21F8E).
// object_placement_data_new(placement*, tag datum, owner object)
#define OFFSET_HALO1_OBJECT_PLACEMENT_NEW 0xB35EBC
// object datum object_new(placement*)
#define OFFSET_HALO1_OBJECT_NEW           0xB35F80
// bool unit_add_weapon(unit, weapon, int16 mode); the loadout passes 2 for
// the first weapon (selects it) and 0 for the second.
#define OFFSET_HALO1_UNIT_ADD_WEAPON      0xB08F3C
#define HALO1_ADD_WEAPON_SELECT           2
// object_delete(object datum)
#define OFFSET_HALO1_OBJECT_DELETE        0xB367C4
// object_set_position(object, position*, forward*, up*): what
// object_teleport ends with (RVA 0xC57DFF); null forward/up keep the facing.
#define OFFSET_HALO1_OBJECT_SET_POSITION  0xB359E8
#define HALO1_PLACEMENT_DATA_SIZE         0x400  // oversized buffer
// Placement fields (the actor spawner 0xC0218C fills them in before
// object_new; the classic CE layout + 4).
#define HALO1_PLACEMENT_POSITION          0x1C   // float[3]
#define HALO1_PLACEMENT_FORWARD           0x38   // float[3]: cos(yaw), sin(yaw), 0

// --- Spawning ----------------------------------------------------------------
// collision_test_vector(flags, point*, vector*, ignore object, result*):
// 48 callers; autoaim (RVA 0xB67DB0) passes the flags below and the
// shooter's ultimate parent as the ignored object. Result: fraction (float,
// 1 = no hit) at +0x14, hit point at +0x18 (RVA 0xB17CE4, 0xB67DB5).
#define OFFSET_HALO1_COLLISION_TEST_VECTOR 0xB913FC
#define HALO1_COLLISION_FLAGS_AUTOAIM      0x1000E9u
#define HALO1_COLLISION_RESULT_FRACTION    0x14
#define HALO1_COLLISION_RESULT_POINT       0x18
#define HALO1_COLLISION_RESULT_SIZE        0x100  // oversized buffer
// object_get_ultimate_parent(object) -> datum (follows +0xD8).
#define OFFSET_HALO1_OBJECT_ULTIMATE_PARENT 0xB38BA8
// unit_get_head_position(unit, float[3] out): the "head" marker (used by
// objects_can_see_object, RVA 0xC5734C).
#define OFFSET_HALO1_UNIT_HEAD_POSITION    0xB04C78
// actor_spawn(actor variant, encounter, squad, spawn point*, bool use major
// variant, int16 region permutation) -> actor datum or -1. Creates the
// variant's unit with its weapon (0xC01DD0) and attaches the AI. The
// encounter spawns (RVA 0xBE28D1) and a vehicle's built-in crew (RVA
// 0xBCE6B2) use it; the crew call passes encounter -1, squad -1 and a free
// actor comes out. Spawn point: position float[3] +0, facing yaw float +0xC,
// +0x1A int16 -1 (as the crew call sets it), the rest 0.
#define OFFSET_HALO1_ACTOR_SPAWN           0xC0218C
#define HALO1_SPAWN_POINT_SIZE             0x20
#define HALO1_SPAWN_POINT_YAW              0xC
#define HALO1_SPAWN_POINT_1A               0x1A
// actor_set_team(actor, int16 team): writes the team (+0x74) of the actor's
// unit or swarm (what ai_set_team does per actor, RVA 0xBFFC0C).
#define OFFSET_HALO1_ACTOR_SET_TEAM        0xC029A0

// --- Built-in cheats (script globals that the engine reads) ---------------
#define OFFSET_HALO1_CHEAT_DEATHLESS      0x1C421A8  // read at RVA 0x8AD45
#define OFFSET_HALO1_CHEAT_JETPACK        0x1C421A9  // unit physics, RVA 0xBB4D10
#define OFFSET_HALO1_CHEAT_INFINITE_AMMO  0x1C421AA  // RVA 0xAFCAFC
#define OFFSET_HALO1_CHEAT_OMNIPOTENT     0x1C421AF  // damage, RVA 0xB9F3E5
#define OFFSET_HALO1_CHEAT_BOTTOMLESS     0x1C421B1  // weapon code, RVA 0xB7914D

// --- Code patches (from the user's Cheat Engine table; checked unique) ----
// These affect every unit's weapons and grenades, not just the player's.
#define HALO1_AOB_AI_CANT_FIRE  "0F 2F 83 58 05 00 00"                 // RVA 0xC44E9D; 7 x NOP
#define HALO1_AOB_AI_INVISIBLE  "F3 45 0F 10 47 18"                    // RVA 0xCC9621; 6 x NOP (takes effect on mission restart)
#define HALO1_AOB_GRENADES      "FE 8C 1A FC 02 00 00"                 // RVA 0xB0A330; 7 x NOP (the decrement)
#define HALO1_AOB_FULL_AUTO     "66 41 21 87 2C 02 00 00"              // RVA 0xB799DB; 8 x NOP
#define HALO1_AOB_NO_SPREAD     "F3 0F 11 84 1F 44 02 00 00 76 0B"     // RVA 0xB75B16; first 9 x NOP
#define HALO1_AOB_NO_OVERHEAT   "F3 0F 11 00 EB 0B"                    // RVA 0xB79559; 6 x NOP (heat store and the jump past the reload)
#define HALO1_AOB_NO_RECOIL     "41 0F 10 00 0F 11 82 84 00 00 00"     // RVA 0xBACD4D; 11 x NOP
// Third person: the camera picks mode 4 (third person) when the held
// weapon's tag has "Uses 3rd Person Camera" (weap +0x308 bit 17). The je
// that skips it becomes NOPs.
#define HALO1_AOB_THIRD_PERSON    "8B 88 08 03 00 00 C1 E9 11 41 84 CD 74 0A"  // RVA 0xB15110
#define HALO1_THIRD_PERSON_OFFSET 12                                          // the je, RVA 0xB1511C

// --- Globals ----------------------------------------------------------------
#define OFFSET_HALO1_OBJECTS        0x1C42248  // data array* (RVA 0xB389AE)
#define OFFSET_HALO1_OBJECT_HEAP    0x2D9CDF8  // char*; object = heap + 0x34 + entry offset (RVA 0xB389CE)
#define OFFSET_HALO1_PLAYERS        0x1C40480  // data array* (players, RVA 0xC56FB3)
#define OFFSET_HALO1_ENCOUNTERS     0x29E0810  // data array*, 0x58 each (ai_set_deaf, RVA 0xBE2B08)
#define OFFSET_HALO1_AI_GLOBALS     0x29E07E8  // byte*: bit 1 = AI on, bit 5 = AI throw grenades (ai_grenades, RVA 0xB1BA6E)
#define OFFSET_HALO1_GAME_SPEED     0x1B7B780  // float game_speed_value; the update loop copies it into game time every frame (RVA 0xAB0D6F)
#define OFFSET_HALO1_GRAVITY        0x2EA87A8  // float* (physics_set_gravity, RVA 0xB19EA6)
#define OFFSET_HALO1_SHOW_HUD       0x2EA3270  // byte* (show_hud, RVA 0xB1E869)
#define OFFSET_HALO1_SCENARIO       0x1C55C78  // char* scnr data (object_teleport, RVA 0xC57BFF)
// Player control, one per local player (0x58 each): the look angles the
// game turns input into (found live; RVA 0xA9793A writes +0x1C).
#define OFFSET_HALO1_PLAYER_CONTROL 0x2D8FE70  // char*
#define HALO1_PLAYER_CONTROL_SIZE   0x58
#define HALO1_PLAYER_CONTROL_YAW    0x1C  // float radians
#define HALO1_PLAYER_CONTROL_PITCH  0x20

#define HALO1_AI_GLOBALS_GRENADES 0x20u
#define HALO1_ENCOUNTER_FLAGS     0xC   // byte: 0x40 deaf, 0x20 blind
#define HALO1_ENCOUNTER_DEAF      0x40u

// data arrays (classic CE layout, RVA 0xBBB8D0)
#define HALO1_ARRAY_ENTRY_SIZE  0x22  // int16
#define HALO1_ARRAY_COUNT       0x2E  // int16, entries in use are below this
#define HALO1_ARRAY_DATA_OFFSET 0x34  // int32, relative to the array header
// object table entry (0xC each): u16 salt, byte type at +3, int32 data offset at +8
#define HALO1_OBJECT_ENTRY_TYPE   0x3
#define HALO1_OBJECT_ENTRY_OFFSET 0x8
#define HALO1_OBJECT_HEAP_HEADER  0x34

// --- Players ----------------------------------------------------------------
#define HALO1_PLAYER_UNIT 0x64  // datum (players, RVA 0xC56FD0)

// --- Objects ----------------------------------------------------------------
#define HALO1_OBJECT_TAG_DATUM    0x0
#define HALO1_OBJECT_POSITION     0x18  // classic 0x5C
#define HALO1_OBJECT_VELOCITY     0x24  // classic 0x68
#define HALO1_OBJECT_CENTER       0x5C  // float[3] bounding sphere centre (classic 0xA0; checked live, ~0.35 above the feet)
#define HALO1_OBJECT_TEAM         0x74  // int16 (RVA 0xBA6CA0)
#define HALO1_OBJECT_HEALTH       0x9C  // float, normalized (unit_get_health, RVA 0xC58727)
#define HALO1_OBJECT_SHIELD       0xA0  // (unit_get_shield, RVA 0xC5875B)
#define HALO1_OBJECT_DAMAGE_FLAGS 0xC2  // word
#define HALO1_DAMAGE_FLAG_DEAD                0x4u    // RVA 0xC58719
#define HALO1_DAMAGE_FLAG_KILL                0x20u   // unit_kill, RVA 0xB1A867
#define HALO1_DAMAGE_FLAG_CANNOT_TAKE_DAMAGE  0x800u  // object_cannot_take_damage, RVA 0xB9FAF7
#define HALO1_OBJECT_PARENT       0xD8  // classic 0x11C (RVA 0xC57CEA)

#define HALO1_OBJECT_TYPE_BIPED  0
#define HALO1_OBJECT_TYPE_VEHICLE 1
#define HALO1_OBJECT_TYPE_WEAPON 2

// --- Units ------------------------------------------------------------------
#define HALO1_UNIT_ACTOR             0x1C0  // datum, -1 if not AI (ai_braindead_by_unit, RVA 0xBFE551)
#define HALO1_UNIT_SWARM_ACTOR       0x1C8  // e.g. Flood infection forms (RVA 0xBFE55C)
#define HALO1_UNIT_FLAGS             0x1D4
#define HALO1_UNIT_FLAG_AI_DISREGARD 0x80u   // ai_disregard, RVA 0xBFE679
#define HALO1_UNIT_AIM_VECTOR        0x210  // float[3], where the unit aims (unit control, RVA 0xC091A2)
#define HALO1_UNIT_ZOOM_LEVEL        0x300  // int8, -1 = not zoomed (autoaim, RVA 0xB67BA1); +0x301 desired
#define HALO1_UNIT_DESIRED_ZOOM      0x301
#define HALO1_UNIT_CURRENT_WEAPON    0x2D3  // int8 (unit_has_weapon_readied, RVA 0xC588F8)
#define HALO1_UNIT_WEAPONS           0x2D8  // datum[4] (unit_has_weapon, RVA 0xB09223)
#define HALO1_UNIT_WEAPON_COUNT      4
#define HALO1_PLAYER_WEAPON_SLOTS    2

// Campaign teams: 1 player, 2 human (allies); 3 covenant, 4 flood, 5 sentinel.
#define HALO1_TEAM_PLAYER 1
#define HALO1_TEAM_HUMAN  2
#define HALO1_TEAM_COVENANT 3
#define HALO1_TEAM_FLOOD    4
#define HALO1_TEAM_SENTINEL 5

// --- Weapons ----------------------------------------------------------------
// Checked live on the assault rifle (600 reserve, 60 loaded).
#define HALO1_WEAPON_MAGAZINES     0x284  // per magazine: +2 int16 reserve, +6 int16 loaded
#define HALO1_WEAPON_MAGAZINE_SIZE 0x14   // stride from the trigger update (RVA 0xB757E8)
// Magazine state (int16) and its timer (int16) sit just before each magazine.
// State 3 counts down the chambering delay after a shot (the Scorpion cannon's
// 120 ticks, checked live); the trigger can't fire until it's back to 0.
#define HALO1_MAGAZINE_STATE       -0x4
#define HALO1_MAGAZINE_TIMER       -0x2
#define HALO1_MAGAZINE_CHAMBERING  3
#define HALO1_WEAPON_MAGAZINE_COUNT 2
#define HALO1_MAGAZINE_RESERVE     0x2
#define HALO1_MAGAZINE_LOADED      0x6
// Triggers, 0x2C each: byte +0 counts ticks since the last shot (0x7F when
// idle, reset to 0 by a shot; RVA 0xB79BA7).
#define HALO1_WEAPON_TRIGGERS      0x228
#define HALO1_WEAPON_TRIGGER_SIZE  0x2C
#define HALO1_WEAPON_TRIGGER_COUNT 2
#define HALO1_TRIGGER_READY        0x7F

// --- Tags -------------------------------------------------------------------
// Tag entries hold 32-bit addresses in the map's virtual space; pointer =
// address - virtual base + real base (tag_get, RVA 0xA9B648).
#define OFFSET_HALO1_TAG_INSTANCES  0x1C34FB0  // entry*, 0x20 each
#define OFFSET_HALO1_TAG_HEADER     0x2B22F50  // header*; tag count at +0xC
#define OFFSET_HALO1_TAG_VIRTUAL    0x2EA3410  // int64
#define OFFSET_HALO1_TAG_REAL       0x2D9CE10  // char*
#define HALO1_TAG_HEADER_COUNT      0xC
#define HALO1_TAG_ENTRY_SIZE        0x20
#define HALO1_TAG_ENTRY_GROUP       0x0   // magic
#define HALO1_TAG_ENTRY_DATUM       0xC
#define HALO1_TAG_ENTRY_NAME        0x10  // address of the path string
#define HALO1_TAG_ENTRY_DATA        0x14  // address of the data

// Tag blocks are {int32 count, int32 address, int32 definition}; tag
// references are {group, name, name length, datum}.
#define HALO1_TAGREF_DATUM 0xC

// obje (every object tag)
#define HALO1_OBJE_BOUNDING_RADIUS 0x4  // float

// actv (actor variant; checked live on c20)
#define HALO1_ACTV_UNIT   0x20  // datum of the unit (bipd) it spawns as

// bipd
#define HALO1_BIPD_JUMP_VELOCITY 0x3B4  // float (Cheat Engine jump script)

// vehi (Assembly Halo1MCC vehi.xml; Halo 2's layout, 0x104 later)
#define HALO1_VEHI_MAX_FORWARD_SPEED  0x2F8
#define HALO1_VEHI_MAX_REVERSE_SPEED  0x2FC
#define HALO1_VEHI_SPEED_ACCELERATION 0x300

// weap
#define HALO1_WEAP_AUTOAIM_ANGLE     0x3E4
#define HALO1_WEAP_AUTOAIM_RANGE     0x3E8
#define HALO1_WEAP_MAGNETISM_ANGLE   0x3EC
#define HALO1_WEAP_MAGNETISM_RANGE   0x3F0
#define HALO1_WEAP_DEVIATION_ANGLE   0x3F4
#define HALO1_WEAP_TRIGGERS          0x4FC  // block, element 0x114
#define HALO1_WEAP_TRIGGER_SIZE      0x114
#define HALO1_TRIGGER_PROJECTILE     0x94   // tag reference
#define HALO1_TRIGGER_FIRING_EFFECTS 0x108  // block, element 0x84
#define HALO1_FIRING_EFFECT_SIZE     0x84
#define HALO1_FIRING_EFFECT_DAMAGE   0x54   // tag reference: damage effect applied to the shooter (camera shake)
#define HALO1_WEAP_MAGAZINES         0x4F0  // block, element 0x70
#define HALO1_WEAP_MAGAZINE_DEF_SIZE 0x70
#define HALO1_MAGDEF_ROUNDS_LOADED_MAX 0xA  // classic layout: total max +8, loaded max +0xA
#define HALO1_MAGDEF_ROUNDS_TOTAL_MAX 0x8   // the reserve can hold this many (600 on the assault rifle)

// matg
#define HALO1_MATG_PLAYER_INFORMATION 0x170  // block, element 0xF4
#define HALO1_MATG_PLAYER_INFO_SIZE   0xF4
// Speeds scaled by the speed slider: walk 0x2C; run forward/back/side/accel
// 0x34-0x40; sneak forward/back/side/accel 0x44-0x50; airborne accel 0x54.
// (0x30 is a multiplier and is left alone.)

#define HALO1_TAG_WEAP 0x77656170  // 'weap'
#define HALO1_TAG_PROJ 0x70726F6A  // 'proj'
#define HALO1_TAG_MATG 0x6D617467  // 'matg'
#define HALO1_TAG_ACTV 0x61637476  // 'actv'
#define HALO1_TAG_VEHI 0x76656869  // 'vehi'
#define HALO1_TAG_EQIP 0x65716970  // 'eqip'
#define HALO1_TAG_SCEN 0x7363656E  // 'scen'
#define HALO1_TAG_MACH 0x6D616368  // 'mach'
#define HALO1_TAG_CTRL 0x6374726C  // 'ctrl'

// --- Damage, allegiance and teams (found statically 2026-10-09) -------------
// object_cause_damage(damage_data*, victim datum, 4 more args): every hit,
// melee, grenade and explosion ends here (20 call sites). damage_data: +0x0C
// instigator player datum, +0x10 instigator unit datum (-1 = environment or
// script), +0x14 int16 instigator team. Clean 5-byte prologue.
#define OFFSET_HALO1_CAUSE_DAMAGE      0xB9EA28
#define HALO1_DAMAGE_DATA_INSTIGATOR_UNIT 0x10
// Betrayal: adds to the player/ally betrayal score when the player hurts or
// kills an ally (bool(team a, team b, mode, bool* out); true = it broke the
// allegiance). Returning false never lets it.
#define OFFSET_HALO1_BETRAYAL_ADD      0xBAD514
// Allegiance data (char* at this global): +0xA4 is a bit matrix of "friendly"
// pairs, bit = teamA * 10 + teamB (diagonal included).
#define OFFSET_HALO1_ALLEGIANCE        0x2DCF968
#define HALO1_ALLEGIANCE_FRIENDLY      0xA4
// The player's team (players entry +0x4C, int16): copied to the unit's team
// (+0x74) only when a unit is attached to the player, not every tick.
#define HALO1_PLAYER_TEAM              0x4C

// --- Time stop and scale (found statically 2026-10-09) -----------------------
// bool object_update(datum): the per-object update objects_update calls for
// every active object (it recurses into children, so a unit's weapons go with
// it). Always returns true and no caller reads it, so a skip returns 1. Clean
// prologue; no entry flag is left stuck by skipping.
#define OFFSET_HALO1_OBJECT_UPDATE     0xB3A06C
// object_set_scale's core (object datum, float scale, uint16 ticks): writes
// the scale at object +0x6C at once; 0.0 there means unscaled. One object only.
#define OFFSET_HALO1_OBJECT_SET_SCALE  0xB3BF34
#define HALO1_OBJECT_SCALE             0x6C
// AI globals byte (OFFSET_HALO1_AI_GLOBALS): bit 0 = AI update on (clearing it
// stops every actor thinking and firing; units go idle).
#define HALO1_AI_GLOBALS_UPDATE        0x1u

// --- Checkpoint locking (found statically 2026-10-09) ------------------------
// Save state block at 0x2B23700 (byte +4 save pending, +7 approved). Every
// game_save* script ends up as a request there; 0xAC4FA8 is the processor
// (once per tick, after the tick) and 0xAC4F80 takes the snapshot once it's
// approved. Vetoing both (and clearing the pending byte) locks the game's own
// saves; the mod's Save button sets a short window that lets its own through.
#define OFFSET_HALO1_SAVE_PROCESS      0xAC4FA8
// The copy itself (void(object*), called from 0xACD87C inside 0xAC4F80): hooking
// only this keeps the whole save state machine intact. Vetoing 0xAC4FA8/0xAC4F80
// made the game wait on a save that never finished (it then reverted by itself).
#define OFFSET_HALO1_SAVE_SNAPSHOT     0xB8A10
#define OFFSET_HALO1_SAVE_PENDING      0x2B23704
// --- AI actors (found statically 2026-10-09) ---------------------------------
// actor_migrate(actor, encounter datum or -1, uint16 squad) and actor_erase
// (actor, bool = 0): not used yet by any cheat.
#define OFFSET_HALO1_ACTOR_MIGRATE     0xC04988
#define OFFSET_HALO1_ACTOR_ERASE       0xC03EA8

// Actors: data array* at this global, 0x66C each. +0x6 flags (bit 0 = swarm
// actor, 0x4 = awake), +0x10 unit datum (-1 for dead/orphan actors; the unit's
// +0x1C0 points back), +0x28 encounter datum (-1 = free actor), +0x2E squad
// index in the encounter, +0x4C the 'actv' tag it was spawned from. The team
// is on the unit (+0x74), not the actor. Checked live on a50.
#define OFFSET_HALO1_ACTORS            0x2A06E68
#define HALO1_ACTOR_FLAGS              0x6
#define HALO1_ACTOR_FLAG_SWARM         0x1
#define HALO1_ACTOR_UNIT               0x10
#define HALO1_ACTOR_TEAM               0x32   // int16, the actor's own team (live: 2 marines, 3 covenant)
#define HALO1_ACTOR_ENCOUNTER          0x28
#define HALO1_ACTOR_SQUAD              0x2E
#define HALO1_ACTOR_VARIANT            0x4C

// --- Motion, shots and skulls (found statically 2026-10-09) --------------------
// CE runs at 30 ticks a second; velocities in objects are world units per tick.
#define HALO1_TICKS_PER_SECOND 30.0f
// biped_apply_acceleration(unit datum, float3* dv per tick, bool = 0): adds dv to
// the velocity, marks the biped airborne (+0x4D8 |= 3) and clears obj+0x10 bit
// 0x20. It halves a living biped's dv in place (pass a scratch copy, doubled).
#define OFFSET_HALO1_BIPED_APPLY_ACCEL   0xBAFA80
// Same for a vehicle: (datum, float3* dv per tick, int = -1), added unscaled.
#define OFFSET_HALO1_VEHICLE_APPLY_ACCEL 0xB43854
#define HALO1_OBJECT_FLAGS               0x10   // bit 0x20 cleared to wake an object's physics
// Biped state: +0x4D8 is recomputed every tick; bit 0 = airborne (no ground
// support), bit 1 = slipping, bit 5 = dead. +0x510 counts ticks since the last
// jump (the game's own jump needs it above 5).
#define HALO1_BIPED_STATE                0x4D8
#define HALO1_BIPED_AIRBORNE             0x1u
#define HALO1_BIPED_TICKS_SINCE_JUMP     0x510
// weapon_trigger_fire(weapon datum, int16 trigger): the central entry for
// player, AI and vehicle weapons; the projectile is made synchronously inside.
// Trigger projectile datum is at trigger +0xA0 (weap +0x4FC block, 0x114 each).
// The first hook there (0xB78DE8) saw the player's shots but the projectile is made
// in 0xB7A374(weapon, int16 trigger, r8d, r9w) (4 args; 0xB78DE8 only starts some
// shots, and the charged-shot paths 0xB7A33E/0xB7AF10 call it directly), so that
// is what's hooked: every projectile creation of every weapon.
#define OFFSET_HALO1_WEAPON_TRIGGER_FIRE 0xB7A374
// Skulls: byte per index at this address, read live (skull_is_active, 0xB220A8).
// The cheat_* globals just after are real switches too.
#define OFFSET_HALO1_SKULLS              0x1C421B8
#define OFFSET_HALO1_CHEAT_BUMP_POSSESSION 0x1C421AB
#define HALO1_SKULL_BYTES                32

// Grenade counts: int8 per type (0 frag, 1 plasma) at unit +0x2FC (the decrement the grenades patch removes; live 4/1).
#define HALO1_UNIT_GRENADES 0x2FC

// Weapon battery: float age at weapon +0x208 (0 = full, 1 = drained; the plasma pistol read 0.94 at "6%" on the HUD, checked live), heat at +0x204. (+0x23C/+0x238 are the other base the weapon update uses for non-held weapons.)
#define HALO1_WEAPON_AGE 0x208

// ai_allegiance / ai_allegiance_remove cores (int16 team a, int16 team b): they update the
// allegiance matrix AND refresh the perception props the AI has already made
// (through 0xBAD6BC -> 0xBCDE18), unlike writing a team, which only affects props
// made later. Player (1) and human (2) are allied by the campaign's own record.
#define OFFSET_HALO1_ALLEGIANCE_ADD    0xBFE060
#define OFFSET_HALO1_ALLEGIANCE_REMOVE 0xBAD484

// The "saving" / "checkpoint" HUD message (bool saving), called around every save.
#define OFFSET_HALO1_SAVE_MESSAGE 0xB135EC
