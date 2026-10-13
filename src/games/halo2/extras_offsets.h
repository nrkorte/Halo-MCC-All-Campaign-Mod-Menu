#pragma once

// halo2.dll offsets for extras.cpp (friendly fire, checkpoint lock, time stop),
// MCC 1.3528.0.0. Found statically 2026-10-10 from the HaloScript function
// table and call graphs (tools/re/h2); hook prologues and the data they read
// were checked against the running game (mission 05b_deltatowers, read-only).

// --- Per-object damage (Disable friendly fire) --------------------------------
// void damage_object(damage_data* rcx, uint32 object datum edx, int16 region r8w,
// int16 node r9w, int16 [rsp+0x28], void* [rsp+0x30]). Everything that hurts one
// object goes through it: projectile impacts (via 0x90AC30 / 0x904440 / 0x912C70),
// area damage (0x90DF10, once per victim), melee and the script damage commands
// (0x90F4B0 is its wrapper with region/node = -1, 15 callers). It walks the object
// and its parents, then calls the section damage (0x90B4F0) and the body damage
// (0x90FB40) per object. Returns nothing (rax is not set at its ret), so a hook
// can drop a hit just by not calling it. Prologue (live): 48 8B C4 / 66 44 89 48 20 /
// 66 44 89 40 18 / 89 50 10 ... (mov rax,rsp first: 8 relocatable bytes).
#define OFFSET_HALO2_DAMAGE_OBJECT 0x90EB80
// damage_data (built by 0x90C0C0 damage_data_new(data, effect tag datum): zeroes
// +4..+0x90, fills the instigator block with -1): +0 damage effect tag, +4 flags,
// then the instigator block set by 0x90BFF0 (the call at RVA 0x6A3066 passes
// data+8): +8 player datum (instigating unit's +0x140), +0xC instigating unit
// datum, +0x10 its team (int16). Static; the unit +0x140 = player datum link was
// read live (the player unit holds 0xEC700000, its player entry's datum).
#define HALO2_DAMAGE_OWNER_PLAYER 0x8
#define HALO2_DAMAGE_OWNER_UNIT   0xC
#define HALO2_DAMAGE_OWNER_TEAM   0x10
// bool teams_are_enemies(int16 a, int16 b) (RVA 0x757160; the body damage calls it
// with the victim's and the damage's team to set the friendly-fire flag 0x10).
// Campaign: not bit (a*16+b) of the 16x16 friends matrix, which is global
// 0x1650EB8 (pointer) + 0xC4 (live: player 1 friends with itself and team 2;
// Covenant 3 with itself and 7; +0xA4 is the declared allegiances). -1 = enemy.
#define OFFSET_HALO2_TEAMS_ARE_ENEMIES 0x757160

// --- Checkpoint saves (Lock checkpoints) --------------------------------------
// The save request (game_save*/the mod's button, 0x700760) only sets state 3 in
// the global at RVA 0xDFCB78; the per-tick processor 0x7008A0 waits for a safe
// moment, heals the players and calls 0x67A990, which sets the byte at RVA
// 0xE70D7E ("save now") and shows the checkpoint message (0x8220D0). The main
// loop (0x679D50, at RVA 0x679E64) sees that byte, calls the snapshot copy
// 0x6B0260(cl = also copy to the second slot, dl = also write the file) at RVA
// 0x679EAD only when the game-state byte (global 0xE80A78 -> +0x1808) is set
// (live: 1), clears the byte and shows the message. The snapshot copy is the one
// and only caller site and the only thing that writes the checkpoint (two 0x1930
// slots, the continue file's 0x3FE000 bytes), so that is what the lock skips;
// vetoing the request would leave state 3 stuck (CE's lesson). Prologue (live):
// 48 89 5C 24 08 / 48 89 74 24 10 / 57 / 48 83 EC 20 (relocatable).
#define OFFSET_HALO2_SNAPSHOT_COPY 0x6B0260

// --- Time stop ----------------------------------------------------------------
// void object_update(uint32 datum) (RVA 0x8D7630): the per-object update. It
// updates the object (animation, physics, type update 0x8E2AD0, weapons' state),
// then recurses into its children (first child +0x10 at 0x8D7892, next sibling
// +0xC), and ends with the update scheduling (0x8DBE70 clears the entry's "needs
// update" bit 4 in entry byte +2). Called from the object loops 0x8DD861,
// 0x8DD8F0 (0x8DD7F0, the tick's object update) and 0x8DD98C (0x8DD920). A skipped
// object keeps bit 4 set, so it is simply called again next tick; releasing
// needs no cleanup (unlike Reach, whose skipped objects were left flagged
// stuck; checked statically only). Prologue (live): 48 89 5C 24 08 / 48 89 6C 24 10 /
// 48 89 74 24 18 / 48 ... (relocatable).
#define OFFSET_HALO2_OBJECT_UPDATE 0x8D7630

// --- Checkpoint HUD message (Lock checkpoints) ----------------------------------
// void checkpoint_message(bool started) (RVA 0x8220D0): posts string id 0x120006AC
// (started, "Checkpoint...") or 0x100006AD (done) to each local player's HUD via
// 0x824690. Only two callers: 0x67A999 (tail jump from 0x67A990, started = 1, when the
// save request is accepted) and 0x679EBB (main loop after the snapshot step, started = 0).
// Static only. Prologue 40 53 / 48 83 EC 20 / 0F B6 D9 / E8 (relocatable, >= 6 bytes).
#define OFFSET_HALO2_CHECKPOINT_MESSAGE 0x8220D0

// --- Time stop: actors ----------------------------------------------------------
// actor_set_active(actor datum, bool) is RVA 0x6229E0 (OFFSET_HALO2_ACTOR_SET_ACTIVE in
// offsets.h). It compares the byte at actor +9 with the requested state, and sets it
// (activating first runs the check 0x622D10 and may fail; deactivating clears +9, stores
// a value at +0x10 and decrements the active-actor count at global 0x1A7xxxx +0x36A).
// Live (2026-10-10, Halo 2 running): actor +9 is 0 or 1 across the 19 actors of the map
// (dormant ones 0), so it is the active flag. Static: the engine's own actor update skips
// inactive actors (same as Reach, where this was confirmed).
#define HALO2_ACTOR_ACTIVE 0x9
