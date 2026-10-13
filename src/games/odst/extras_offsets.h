#pragma once

// halo3odst.dll offsets for extras.cpp (friendly fire, time stop, "invulnerable AI
// can die"), MCC 1.3528.0.0. Ported from Halo 3's extras_offsets.h on 2026-10-11:
// every RVA was re-found in ODST by masked byte patterns (tools/re/odst port.py,
// checked by hand with od.py) and compared with the disassembly; nothing was copied.
// "LIVE" = read from the running game (ODST was running a campaign map, read-only);
// the rest is static. Nothing here could be fired/tested live.

// --- Per-victim damage (Disable friendly fire) ----------------------------------
// void damage_object(damage_data* rcx, uint32 victim datum edx, int16 region r8w,
// int16 node r9w, int16 [rsp+0x28], void* [rsp+0x30] at entry). Port of Halo 3's
// 0x36FE00 (port.py: 12-insn match); 21 call sites (projectile impact 0x3A4197 ...).
// Prologue (static, from the image): 48 8B C4 / 66 44 89 48 20 / 66 44 89 40 18 /
// 89 50 10 / 55 53 56 57 ... (mov rax,rsp first; the first 13 bytes relocatable).
// Reads the data's instigator team byte (+0x10, 0x3B6F12: cmp byte [rbx+0x10],-1;
// 0x3B6F2B movsx edx,byte [rbx+0x10] -> team test 0x17EC48 vs victim unit +0x16C).
#define OFFSET_ODST_DAMAGE_OBJECT 0x3B68A8
// damage_data layout (static, verified by reading both writers): damage_data_new
// 0x3B5664 zeroes +4.. and sets +8/+0xC/+0x10 from constants (-1); the owner setter
// 0x3C4AB4(unit datum ecx, data+8 rdx) writes [+8] = player datum (unit +0x170),
// [+0xC] = instigating unit datum, byte [+0x10] = the unit's team (+0x16C).
#define ODST_DAMAGE_OWNER_PLAYER 0x8
#define ODST_DAMAGE_OWNER_UNIT   0xC
#define ODST_DAMAGE_OWNER_TEAM   0x10   // int8, -1 = none

// --- Time stop ------------------------------------------------------------------
// uint8 object_update(uint32 datum) (Halo 3 0x347DFC). Object table entries are 0x18
// bytes: +2 flags byte. It returns at once unless entry flag 0x02 is set (0x3836A2),
// clears flag 0x04, runs the type updates, recurses into the children (first child
// +0xC, next sibling +8, 0x3837AD..0x3837C6), returns "still needs updating" in bl.
// Callers 0x37B9C2 (update list, flags&0xB == 3), 0x37BA5C, 0x37BD43 ignore the
// result (the child recursion ors it). A skip never writes the flags, so the object is
// simply updated again later. Static. Prologue: 53 / 55 / 57 / 41 54 / 41 55 /
// 41 56 / 41 57 / 48 83 EC 20 (all relocatable).
#define OFFSET_ODST_OBJECT_UPDATE 0x383654
// void object_post_update(uint32 datum) (Halo 3 0x3483A4); called when entry flags&0xC
// == 4 (0x37B9DB, 0x37BD52 -> 0x383C34 is the post-update proper) and from 0x383F13
// (children). Static. Prologue: 48 89 5C 24 08 / 55 / 56 / 57 / 41 54-57 / 48 83 EC 50.
#define OFFSET_ODST_OBJECT_POST_UPDATE 0x383C34
// void fire_marked_barrels(uint32 weapon rcx, rdx, r8, r9b, [rsp+0x28]) (Halo 3
// 0x35EC84): for each barrel whose state word (weapon +0x1A8 + 0x34*i + 4 = +0x1AC,
// bit 2 "fired this tick") is marked calls barrel fire 0x3AF230 (the one fun_extras
// hooks) and clears the mark (0x3A4EE2/0x3A4F02), then clears weapon flag bit 0
// (+0x168, 0x3A4F1E). Callers 0x3A4E32 (pass over all weapons) and 0xA058B. Static.
// Prologue: 48 89 5C 24 08 / 48 89 6C 24 10 / 48 89 74 24 18 / 57 / 41 54-57
// (relocatable). Not the same address as fun_extras' barrel hook.
#define OFFSET_ODST_FIRE_MARKED_BARRELS 0x3A4E4C
#define ODST_WEAPON_MARKED_BARREL_FLAG 0x2   // word at barrel state +4

// --- Time stop: actors ----------------------------------------------------------
// actor_set_active = OFFSET_ODST_ACTOR_SET_ACTIVE (offsets.h, 0x5A8DEC). Actors are 0xAB0
// bytes (TLS slot 1); the active flag is the byte at actor +9 (0x5A8E34 cmp byte
// [rbx+9], dl; 0x5A8E58 sets it). Static; LIVE: actor datums of allies/enemies resolve
// to units whose +0x160 holds that datum.
#define ODST_ACTOR_ACTIVE 0x9

// --- Invulnerable AI can die ----------------------------------------------------
// object_cannot_die core 0x3B7ADC (Halo 3 0x370E6C): bts/btr bit 0x10 of the dword at
// object +0x104 unless bit 2 (0x4, dead) is set -> ODST_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE
// (0x10000, offsets.h) confirmed statically.
#define ODST_OBJECT_FLAG_DEAD 0x4u

// "Protected unit" table in the AI globals (TLS slot 7): 8 entries of 0x10 bytes from
// +0x404: {unit datum, second dword, word flags (+8, bit0/1), word timer (+0xA), dword
// float (+0xC)}; free = {-1, -1, 0, 0}. LIVE: all 8 entries free on a map with no
// protected ally (0x404..0x484); static: the init at 0x5AF1C0..0x5AF28A writes exactly
// that for 8 entries; the adder 0x5A78A8 (actor datum, second dword) takes the first free
// entry, stores the actor's unit datum (+0x14) and sets unit +0x10A bit 0; the per-tick
// 0x5AFE94 walks the 8 entries and, for a unit that is dead (+0x104 bit 2), calls 0x5EF9DC
// with the second dword (the revive); 0x5B2C40 clears an entry (-1, -1) by unit datum.
#define ODST_REVIVE_TABLE  0x404
#define ODST_REVIVE_STRIDE 0x10
#define ODST_REVIVE_COUNT  8
