#pragma once

// halo3.dll offsets for extras.cpp (friendly fire, time stop, "invulnerable AI
// can die"), MCC 1.3528.0.0. Found statically 2026-10-10 from the HaloScript
// function table (tools/re: hsfunc.py defn(opcode) -> evaluator at definition
// +0x20), call graphs and grep_asm.py; hook prologues and the data they read
// were checked against the running game (070_waste, read-only).

// --- Per-victim damage (Disable friendly fire) ----------------------------------
// void damage_object(damage_data* rcx, uint32 victim datum edx, int16 region r8w,
// int16 node r9w, int16 [rsp+0x28], void* [rsp+0x30] at entry). Found from
// damage_new/damage_object_effect (script opcodes 0x40/0x43; evaluators 0x1E2944 /
// 0x1E29FC -> 0x259D90 / 0x259F78): they build a damage_data with damage_data_new
// (0x36EBA0, zeroes +4..+0xA4 and fills the instigator block with -1) and call
// 0x36FE00 with region/node -1. 21 call sites (projectile impacts, area damage per
// victim, melee, script damage, 0x35BE99/0x35C990/0x377185 ...). It reads the
// damage data's instigator team (+0x10) to set the engine's own friendly-fire flag
// (0x370410..0x37043E: victim team vs data team through 0x199B84), then applies
// section/body damage. Returns nothing used (callers ignore rax), so a hook can drop
// a hit by not calling it. Prologue (LIVE, 070_waste): 48 8B C4 / 66 44 89 48 20 /
// 66 44 89 40 18 / 89 50 10 / 55 ... (mov rax,rsp first, 13 relocatable bytes; same
// shape as Halo 2's 0x90EB80, which MinHook handled fine).
#define OFFSET_HALO3_DAMAGE_OBJECT 0x36FE00
// damage_data layout (static): +0 damage effect tag datum, +4 flags, +8 instigating
// player datum, +0xC instigating unit datum (0x371784 compares it with the victim:
// "self damage"; unit +0x178 = player index), +0x10 instigator team (int8, -1 = none;
// 0x370410 compares it with -1 and 0x370512 passes it to the team test), +0x14/+0x18
// dword -1 (other owner info). damage_data_new initialises +8/+0xC to -1 -1 and +0x10
// to -1. Same block as Halo 2's (+8 player, +0xC unit, +0x10 team) except the team
// is a byte here. The Reach layout (+0xC unit, +8 player) is also the same. NOT
// verified live (cannot fire a shot in a read-only session): the first 12 blocked
// hits are logged so a test confirms it.
#define HALO3_DAMAGE_OWNER_PLAYER 0x8
#define HALO3_DAMAGE_OWNER_UNIT   0xC
#define HALO3_DAMAGE_OWNER_TEAM   0x10   // int8

// --- Time stop ------------------------------------------------------------------
// uint8 object_update(uint32 datum) (RVA 0x347DFC): the per-object update. Object
// table entries are 0x18 bytes: +0 salt, +2 flags byte, +3 object type, +0x10 data
// pointer (live: flags 0x42 / 0x40 / 0x80, bipeds type 0). The function returns at once
// unless entry flag 0x02 ("needs update") is set (0x347E47), clears flag 0x04 (0x347E65),
// runs the animation/type updates, recurses into the children itself (first child +0xC,
// next sibling +8, at 0x347F14..0x347F4F, through the hook), and at the end clears flag
// 0x02 if nothing needs another update (0x347FC8..0x347FD5). Its return value (sil) is
// "still needs updating"; every caller (0x340702 the update list, 0x34079C, 0x340953
// the flagged pass) ignores it except the child recursion, which ors it. Skipping an
// object leaves flag 0x02 set, so it is simply updated again next tick; releasing needs
// no cleanup (unlike Reach, whose skipped objects were left stuck; here the
// semantic of the flag is "needs update" and a skip never writes it). Static only.
// Prologue (LIVE): 48 89 5C 24 08 / 48 89 6C 24 18 / 56 / 41 54 / 41 55 / 41 56 / 41 57 /
// 48 83 EC 20 (all relocatable).
#define OFFSET_HALO3_OBJECT_UPDATE 0x347DFC
// void object_post_update(uint32 datum) (RVA 0x3483A4): run for objects whose entry
// flags (&0xC) == 4 (0x34085C, 0x34071D loops) and by the unconditional iterator at
// 0x3408B0; recurses into the children in a loop of its own frame (0x3485D6..0x3485F7).
// Does the animation processing (0x347FF8, object +0x9C animation datum), the type
// specific post-update (table at 0x148 of the type's definition array, call [rax+0x98]),
// clears flag bit 2 of the object's +0xA8 and tail-jumps to 0x34A0E4. Skipped for frozen
// objects too so no animation output is produced while frozen (belt and braces: the
// update alone may be enough; if time stop shows visual trouble this hook is the one
// to drop). Returns nothing used. Prologue (LIVE): 48 89 5C 24 18 / 89 4C 24 08 / 55 / 56 /
// 57 / 41 54 / 41 55 / 41 56 / 41 57 / 48 83 EC 50 (relocatable).
#define OFFSET_HALO3_OBJECT_POST_UPDATE 0x3483A4
// void fire_marked_barrels(uint32 weapon rcx, rdx = 0, r8 = 0, r9b, dword [rsp+0x28] = -1)
// (RVA 0x35EC84): for each of the weapon's barrels whose state flag word (weapon +0x1B0 +
// 0x34*i + 4) has bit 2 ("fired this tick", set by the weapon update) calls barrel fire
// 0x3683A0 (which halo3.cpp hooks for the chaos gun / recoil push) and then clears the
// mark; at the end clears the weapon's flag bit 0 (+0x170). Called from the pass 0x35EC1C
// (0x35EC6A, after the object update loops, 0x3408C3) and from 0x91539. This is where an
// AI's shot materialises outside the object update (the Halo 2 lesson), so frozen
// weapons are vetoed here: the hook clears the marks itself (2 barrels, as the barrel state
// array) and returns, so nothing is delayed and nothing fires on release. A different address
// than halo3.cpp's barrel fire hook (0x3683A0), so the two hooks do not conflict.
// Prologue (LIVE): 48 89 5C 24 08 / 48 89 6C 24 10 / 48 89 74 24 18 / 57 / 41 54 /
// 41 55 / 41 56 / 41 57 (relocatable).
#define OFFSET_HALO3_FIRE_MARKED_BARRELS 0x35EC84
#define HALO3_WEAPON_MARKED_BARREL_FLAG 0x2   // word at barrel state +4

// --- Time stop: actors ----------------------------------------------------------
// actor_set_active(actor datum, bool) is OFFSET_HALO3_ACTOR_SET_ACTIVE (offsets.h). The
// active flag is the byte at actor +9. LIVE (070_waste, 2026-10-10): 32 of 34 actors read 1,
// the two dormant (team 3) ones read 0; actor +0x14 holds the unit datum (checked: it
// resolves to a biped whose +0x168 holds this actor's datum, i.e. +0x168 is the actor datum,
// not an index). Static: the actor update skips inactive actors (same as Reach/Halo 2).
#define HALO3_ACTOR_ACTIVE 0x9

// --- Invulnerable AI can die ----------------------------------------------------
// object_cannot_die(object, bool) (script 0x75, evaluator 0x1E3950 -> core 0x370E6C) and
// ai_cannot_die (0x14D, 0x1E70E0 -> 0x573C18 -> the same core per unit): the core sets or
// clears bit 0x10 of the dword at object +0x110 (bts/btr ecx,0x10 at 0x370EB6/0x370EBC), unless
// bit 2 (0x4, "dead") is set. So HALO3_DAMAGE_FLAG_CANNOT_DIE_FROM_DAMAGE (0x10000, offsets.h)
// is confirmed statically. (object_cannot_take_damage is bit 0x100 and is left alone.)
#define HALO3_OBJECT_FLAG_DEAD 0x4u

// AI revive table (AI globals, TLS slot 6): 7 entries of 0x10 from +0x40C, {unit datum, second dword, 0, 0},
// free = -1. Read live; the entry for the Arbiter was wiped by the game when he was revived.
#define HALO3_REVIVE_TABLE  0x40C
#define HALO3_REVIVE_STRIDE 0x10
#define HALO3_REVIVE_COUNT  7
