#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "games/game.h"

// Squad presets for the Squads page: several AI characters spawned together
// around the aim point. Members are character tag short names; "a|b" means
// "a, or b if this map has no a", and "*" is every AI character in the map.
// A member the map lacks is skipped, so a squad is spawnable if any member
// is. Names are the 'ai' characters of Reach's campaign (all seven maps).
namespace Games {
    struct SquadMember {
        const char* characters;
        int count;
    };

    struct Squad {
        const char* group;
        const char* name;
        SquadMember members[10];  // count 0 ends the list
        bool large;               // roomier spacing (hunters)
        bool ce = false;          // Halo: CE's list (actor variant names)
        bool h2 = false;          // Halo 2's list (character tag names)
        bool h3 = false;          // Halo 3's list (character tag names)
        bool odst = false;        // Halo 3: ODST's list (shares most of Halo 3's rows); the rest are Reach's
        bool h4 = false;          // Halo 4's list (character tag names storm_*)
    };

    inline const Squad kSquads[] = {
        // Covenant
        {"Covenant", "Grunt squad", {{"grunt", 4}, {"grunt_major", 1}}},
        {"Covenant", "Grunt heavies", {{"grunt_heavy", 3}, {"grunt", 2}}},
        {"Covenant", "Grunt spec ops", {{"grunt_specops|grunt_ultra", 4}, {"grunt_ultra", 1}}},
        {"Covenant", "Grunts + Elites", {{"grunt", 4}, {"grunt_major", 1}, {"elite", 2}}},
        {"Covenant", "Grunts + Brutes", {{"grunt", 4}, {"brute", 2}, {"brute_captain", 1}}},
        {"Covenant", "Grunts + Jackals", {{"grunt", 4}, {"jackal", 3}}},
        {"Covenant", "Grunts + Hunters", {{"hunter", 2}, {"grunt", 4}}, true},
        {"Covenant", "Elite squad", {{"elite", 3}, {"elite_officer", 1}}},
        {"Covenant", "Elite strike team", {{"elite_general", 1}, {"elite_ultra", 2}, {"elite_specops", 2}}},
        {"Covenant", "Jetpack Elites", {{"elite_jetpack", 4}}},
        {"Covenant", "Jackal squad", {{"jackal", 4}, {"jackal_major", 1}}},
        {"Covenant", "Sniper nest", {{"jackal_sniper", 4}}},
        {"Covenant", "Skirmisher pack", {{"skirmisher", 4}, {"skirmisher_major", 1}}},
        {"Covenant", "Skirmisher elite guard",
         {{"skirmisher_commando", 2}, {"skirmisher_murmillone", 2}, {"skirmisher_champion", 1}}},
        {"Covenant", "Hunter pair", {{"hunter", 2}}, true},
        {"Covenant", "Hunters + Elites", {{"hunter", 2}, {"elite", 2}}, true},
        {"Covenant", "Brute pack", {{"brute", 4}, {"brute_captain", 1}}},
        {"Covenant", "Brute chieftains",
         {{"brute_chieftain_weapon", 2}, {"brute_chieftain_armor", 2}, {"brute_captain", 1}}},
        {"Covenant", "Brutes + Elites", {{"brute", 3}, {"elite", 3}}},
        {"Covenant", "Bugger swarm", {{"bugger", 6}, {"bugger_major", 2}, {"bugger_ultra", 1}}},
        {"Covenant", "Engineers + Elites", {{"engineer", 2}, {"elite", 2}, {"grunt", 2}}},
        {"Covenant", "Covenant war party",
         {{"grunt", 3}, {"jackal", 2}, {"elite", 2}, {"skirmisher", 2}, {"hunter", 1}}, true},
        // Human
        {"Human", "Marine squad", {{"trooper", 4}, {"trooper_sgt", 1}}},
        {"Human", "ODST squad", {{"trooper_odst", 4}, {"trooper_odst_buck", 1}}},
        {"Human", "Marines + ODSTs", {{"trooper", 3}, {"trooper_odst", 3}, {"trooper_sgt", 1}}},
        {"Human", "Spartan team",
         {{"spartan_carter", 1}, {"spartan_kat", 1}, {"spartan_jun", 1}, {"spartan_emile", 1}, {"spartan_jorge", 1}}},
        {"Human", "Spartan team + marines",
         {{"spartan_carter", 1}, {"spartan_kat", 1}, {"spartan_jun", 1}, {"spartan_emile", 1}, {"spartan_jorge", 1},
          {"trooper", 4}, {"trooper_sgt", 1}}},
        // Ridiculous
        {"Ridiculous", "Grunt swarm (22)", {{"grunt", 16}, {"grunt_major", 4}, {"grunt_heavy", 2}}},
        {"Ridiculous", "Elite army (14)",
         {{"elite", 6}, {"elite_specops", 3}, {"elite_ultra", 3}, {"elite_general", 2}}},
        {"Ridiculous", "Hunter apocalypse (8)", {{"hunter", 8}}, true},
        {"Ridiculous", "Jackal horde (20)", {{"jackal", 12}, {"jackal_major", 4}, {"jackal_sniper", 4}}},
        {"Ridiculous", "Brute rampage (13)",
         {{"brute", 8}, {"brute_captain", 3}, {"brute_chieftain_weapon", 1}, {"brute_chieftain_armor", 1}}},
        {"Ridiculous", "Marine army (20)", {{"trooper", 14}, {"trooper_sgt", 3}, {"trooper_odst", 3}}},
        {"Ridiculous", "Marines vs Elites (13)", {{"trooper", 6}, {"trooper_sgt", 1}, {"elite", 6}}},
        {"Ridiculous", "Spartan team vs the Covenant",
         {{"spartan_carter", 1}, {"spartan_kat", 1}, {"spartan_jun", 1}, {"spartan_emile", 1}, {"spartan_jorge", 1},
          {"elite", 4}, {"grunt", 4}, {"hunter", 1}}, true},
        {"Ridiculous", "Spartan army (8)", {{"spartan_generic", 8}}},
        {"Ridiculous", "Everyone (one of each AI)", {{"*", 1}}, true},
        {"Ridiculous", "Everyone, twice", {{"*", 2}}, true},
        // Halo: CE (names are actor variants)
        {"Covenant", "Grunt squad", {{"grunt minor plasma pistol|grunt minor needler", 4}, {"grunt major plasma pistol|grunt major needler", 1}}, false, true},
        {"Covenant", "Grunt spec ops", {{"grunt specops needler|grunt specops fuel rod", 4}}, false, true},
        {"Covenant", "Grunts + Elites", {{"grunt minor plasma pistol|grunt minor needler", 4}, {"grunt major plasma pistol|grunt major needler", 1}, {"elite minor plasma rifle|elite minor needler", 2}}, false, true},
        {"Covenant", "Grunts + Jackals", {{"grunt minor plasma pistol|grunt minor needler", 4}, {"jackal minor plasma pistol", 3}}, false, true},
        {"Covenant", "Grunts + Hunters", {{"hunter", 2}, {"grunt minor plasma pistol|grunt minor needler", 4}}, true, true},
        {"Covenant", "Jackal squad", {{"jackal minor plasma pistol", 4}, {"jackal major plasma pistol", 1}}, false, true},
        {"Covenant", "Elite squad", {{"elite minor plasma rifle|elite minor needler", 3}, {"elite major plasma rifle|elite major needler", 1}}, false, true},
        {"Covenant", "Elite strike team",
         {{"elite commander plasma rifle|elite major plasma rifle|elite major needler", 1}, {"elite specops plasma rifle|elite major plasma rifle|elite major needler", 2}, {"elite major plasma rifle|elite major needler", 2}}, false, true},
        {"Covenant", "Sword elites", {{"elite commander energy sword|elite major plasma rifle|elite major needler", 3}}, false, true},
        {"Covenant", "Stealth elites", {{"stealth elite plasma rifle|elite major plasma rifle|elite major needler", 3}, {"stealth elite major plasma rifle|elite major plasma rifle|elite major needler", 2}}, false, true},
        {"Covenant", "Hunter pair", {{"hunter", 2}}, true, true},
        {"Covenant", "Hunters + Elites", {{"hunter", 2}, {"elite major plasma rifle|elite major needler", 2}}, true, true},
        {"Covenant", "Covenant war party",
         {{"grunt minor plasma pistol|grunt minor needler", 3}, {"jackal minor plasma pistol", 2}, {"elite major plasma rifle|elite major needler", 2}, {"hunter", 1}}, true, true},
        {"Human", "Marine squad", {{"marine_armored assault rifle|marine assault rifle", 4}, {"marine_armored assault rifle major|marine assault rifle major", 1}}, false, true},
        {"Human", "Marine snipers", {{"marine_armored sniper rifle", 3}, {"marine_armored sniper rifle major", 1}}, false, true},
        {"Flood", "Flood ambush",
         {{"floodcombat_human assault rifle|floodcombat_human pistol", 5}, {"floodcombat elite shotgun|floodcombat elite assault rifle", 2}, {"floodcarrier", 2}}, false, true},
        {"Flood", "Flood swarm", {{"flood_infection", 10}, {"floodcarrier", 3}}, false, true},
        {"Sentinels", "Sentinel patrol", {{"sentinel", 3}, {"sentinel major", 2}}, false, true},
        {"Ridiculous", "Flood horde (20)",
         {{"floodcombat_human assault rifle|floodcombat_human pistol", 10}, {"floodcombat elite assault rifle|floodcombat elite pistol", 6}, {"floodcarrier", 4}}, false, true},
        {"Ridiculous", "Elite army (14)", {{"elite minor plasma rifle|elite minor needler", 6}, {"elite major plasma rifle|elite major needler", 5}, {"elite specops plasma rifle|elite major plasma rifle|elite major needler", 3}}, false, true},
        {"Ridiculous", "Hunter apocalypse (8)", {{"hunter", 6}, {"hunter major", 2}}, true, true},
        {"Ridiculous", "Marines vs Elites (13)",
         {{"marine_armored assault rifle|marine assault rifle", 8}, {"elite major plasma rifle|elite major needler", 5}}, false, true},
        // Halo 2 (names are the campaign's 'char' tags: objects\characters\<x>\ai\<name>; a map
        // only has some of them, a squad's missing members are skipped)
        {"Covenant", "Grunt squad", {{"grunt", 4}, {"grunt_major", 1}}, false, false, true},
        {"Covenant", "Grunt heavies", {{"grunt_heavy", 3}, {"grunt", 2}}, false, false, true},
        {"Covenant", "Grunt spec ops", {{"grunt_ultra", 4}, {"grunt_specops", 1}}, false, false, true},
        {"Covenant", "Grunts + Elites", {{"grunt", 4}, {"grunt_major", 1}, {"elite", 2}}, false, false, true},
        {"Covenant", "Grunts + Jackals", {{"grunt", 4}, {"jackal", 3}}, false, false, true},
        {"Covenant", "Grunts + Brutes", {{"grunt", 4}, {"brute", 2}, {"brute_captain", 1}}, false, false, true},
        {"Covenant", "Grunts + Hunters", {{"hunter", 2}, {"grunt", 4}}, true, false, true},
        {"Covenant", "Jackal squad", {{"jackal", 4}, {"jackal_major", 1}}, false, false, true},
        {"Covenant", "Sniper nest", {{"jackal_sniper", 4}}, false, false, true},
        {"Covenant", "Elite squad", {{"elite", 3}, {"elite_major", 1}}, false, false, true},
        {"Covenant", "Elite strike team", {{"elite_zealot", 1}, {"elite_ultra", 2}, {"elite_specops", 2}}, false, false, true},
        {"Covenant", "Stealth Elites", {{"elite_stealth", 3}, {"elite_stealth_major", 2}}, false, false, true},
        {"Covenant", "Elite rangers", {{"elite_ranger", 4}}, false, false, true},
        {"Covenant", "Honor guard", {{"elite_honor_guard", 4}, {"elite_zealot", 1}}, false, false, true},
        {"Covenant", "Hunter pair", {{"hunter", 2}}, true, false, true},
        {"Covenant", "Hunters + Elites", {{"hunter", 2}, {"elite", 2}}, true, false, true},
        {"Covenant", "Brute pack", {{"brute", 4}, {"brute_major", 1}, {"brute_captain", 1}}, false, false, true},
        {"Covenant", "Brute honor guard", {{"brute_honor_guard", 4}, {"brute_captain", 1}}, false, false, true},
        {"Covenant", "Brutes + Elites", {{"brute", 3}, {"elite", 3}}, false, false, true},
        {"Covenant", "Bugger swarm", {{"bugger", 8}}, false, false, true},
        {"Covenant", "Heretics", {{"heretic", 4}, {"heretic_major", 1}, {"heretic_grunt", 2}}, false, false, true},
        {"Covenant", "Covenant war party",
         {{"grunt", 3}, {"jackal", 2}, {"elite", 2}, {"brute", 2}, {"hunter", 1}}, true, false, true},
        {"Human", "Marine squad", {{"marine", 4}, {"marine_sgt", 1}}, false, false, true},
        {"Human", "Marine fireteam", {{"marine", 3}, {"marine_female", 2}, {"marine_sgt", 1}}, false, false, true},
        {"Human", "ODST squad", {{"marine_odst|marine", 4}, {"marine_sgt", 1}}, false, false, true},
        {"Human", "Marines + ODSTs", {{"marine", 3}, {"marine_odst", 3}, {"marine_sgt", 1}}, false, false, true},
        {"Flood", "Flood ambush",
         {{"flood_combat_human", 5}, {"floodcombat_elite", 2}, {"flood_carrier", 2}}, false, false, true},
        {"Flood", "Flood swarm", {{"flood_infection", 10}, {"flood_carrier", 3}}, false, false, true},
        {"Flood", "Flood elites", {{"floodcombat_elite", 3}, {"floodcombat_elite_shielded", 2}}, false, false, true},
        {"Flood", "Flood juggernaut", {{"flood_juggernaut", 1}, {"flood_combat_human", 3}, {"flood_carrier", 2}}, true, false, true},
        {"Sentinels", "Sentinel patrol", {{"sentinel_aggressor", 3}, {"sentinel_aggressor_major", 2}}, false, false, true},
        {"Sentinels", "Sentinel strike force",
         {{"sentinel_aggressor_eliminator", 2}, {"sentinel_aggressor_major", 2}, {"sentinel_enforcer", 1}}, true, false, true},
        {"Ridiculous", "Grunt swarm (22)", {{"grunt", 16}, {"grunt_major", 4}, {"grunt_heavy", 2}}, false, false, true},
        {"Ridiculous", "Elite army (14)",
         {{"elite", 6}, {"elite_major", 3}, {"elite_ultra", 2}, {"elite_specops", 2}, {"elite_zealot", 1}}, false, false, true},
        {"Ridiculous", "Hunter apocalypse (8)", {{"hunter", 8}}, true, false, true},
        {"Ridiculous", "Jackal horde (20)", {{"jackal", 12}, {"jackal_major", 4}, {"jackal_sniper", 4}}, false, false, true},
        {"Ridiculous", "Brute rampage (13)",
         {{"brute", 8}, {"brute_major", 3}, {"brute_captain", 2}}, false, false, true},
        {"Ridiculous", "Marine army (20)", {{"marine", 14}, {"marine_sgt", 3}, {"marine_odst", 3}}, false, false, true},
        {"Ridiculous", "Marines vs Elites (13)", {{"marine", 6}, {"marine_sgt", 1}, {"elite", 6}}, false, false, true},
        {"Ridiculous", "Flood horde (20)",
         {{"flood_combat_human", 10}, {"floodcombat_elite", 6}, {"flood_carrier", 4}}, false, false, true},
        {"Ridiculous", "Sentinel swarm (12)",
         {{"sentinel_aggressor", 8}, {"sentinel_aggressor_major", 3}, {"sentinel_enforcer", 1}}, true, false, true},
        {"Ridiculous", "Everyone (one of each AI)", {{"*", 1}}, true, false, true},
        {"Ridiculous", "Everyone, twice", {{"*", 2}}, true, false, true},
        // Halo 3 (names are the campaign's 'char' tags: objects\characters\<x>\ai\<name>; a map
        // only has some of them, a squad's missing members are skipped)
        {"Covenant", "Grunt squad", {{"grunt", 4}, {"grunt_major", 1}}, false, false, false, true, true},
        {"Covenant", "Grunts + Brutes", {{"grunt", 4}, {"brute", 2}, {"brute_captain", 1}}, false, false, false, true, true},
        {"Covenant", "Grunts + Hunters", {{"hunter", 2}, {"grunt", 4}}, true, false, false, true, true},
        {"Covenant", "Jackal squad", {{"jackal", 4}, {"jackal_major", 1}}, false, false, false, true, true},
        {"Covenant", "Sniper nest", {{"jackal_sniper", 4}}, false, false, false, true, true},
        {"Covenant", "Elite squad", {{"elite", 3}, {"elite_major", 1}}, false, false, false, true, true},
        {"Covenant", "Elite spec ops", {{"elite_specops", 3}, {"elite_specops_commander", 1}}, false, false, false, true, true},
        {"Covenant", "Hunter pair", {{"hunter", 2}}, true, false, false, true, true},
        {"Covenant", "Brute pack", {{"brute", 4}, {"brute_major", 1}, {"brute_captain", 1}}, false, false, false, true, true},
        {"Covenant", "Chieftain's guard",
         {{"brute_chieftain_weapon|brute_chieftain_armor", 1}, {"brute_bodyguard", 4}}, false, false, false, true, true},
        {"Covenant", "Covenant war party",
         {{"grunt", 3}, {"jackal", 2}, {"elite", 2}, {"brute", 2}, {"hunter", 1}}, true, false, false, true, true},
        {"Human", "Marine squad", {{"marine", 4}, {"marine_sgt", 1}}, false, false, false, true, true},
        {"Human", "ODST squad", {{"marine_odst|marine", 4}, {"marine_odst_sgt|marine_sgt", 1}}, false, false, false, true, true},
        {"Flood", "Flood ambush",
         {{"flood_combat_human", 5}, {"floodcombat_elite", 2}, {"flood_carrier", 2}}, false, false, false, true},
        {"Flood", "Flood swarm", {{"flood_infection", 10}, {"flood_carrier", 3}}, false, false, false, true},
        {"Sentinels", "Sentinel patrol", {{"sentinel_aggressor", 3}, {"sentinel_aggressor_major", 2}}, false, false, false, true},
        {"Ridiculous", "Grunt swarm (22)", {{"grunt", 16}, {"grunt_major", 4}, {"grunt_heavy", 2}}, false, false, false, true, true},
        {"Ridiculous", "Marine army (20)", {{"marine", 14}, {"marine_sgt", 3}, {"marine_odst", 3}}, false, false, false, true, true},
        {"Ridiculous", "Everyone (one of each AI)", {{"*", 1}}, true, false, false, true, true},
        {"Ridiculous", "Everyone, twice", {{"*", 2}}, true, false, false, true, true},
        // Halo 3: ODST: the rows above that also set the 9th field (odst) are shared with Halo 3; these
        // are ODST's own (campaign characters objects\characters\<x>\ai\<name>; missing members are skipped)
        {"Human", "Buck's squad (Buck, Dutch, Romeo, Mickey)",
         {{"marine_odst_buck", 1}, {"marine_odst_dutch", 1}, {"marine_odst_romeo", 1}, {"marine_odst_mickey", 1}},
         false, false, false, false, true},
        {"Covenant", "Engineers + Brutes", {{"engineer", 2}, {"brute", 2}, {"brute_captain", 1}}, false, false, false, false, true},
        {"Covenant", "Bugger swarm", {{"bugger", 6}, {"bugger_major", 2}, {"bugger_ultra", 1}, {"bugger_captain", 1}}, false, false, false, false, true},
        {"Covenant", "Brute jump packs", {{"brute_jumppack", 3}, {"brute_jumppack_major", 1}, {"brute_jumppack_ultra", 1}}, false, false, false, false, true},
        {"Covenant", "Brute chieftains",
         {{"brute_chieftain_weapon", 2}, {"brute_chieftain_armor", 2}, {"brute_captain", 1}}, false, false, false, false, true},
        // Halo 4 (names are the campaign's 'char' tags: objects\characters\<x>\ai\storm_*; a map
        // only has some of them, a squad's missing members are skipped)
        {"Covenant", "Grunt squad", {{"storm_grunt", 4}, {"storm_grunt_ultra", 1}}, false, false, false, false, false, true},
        {"Covenant", "Grunts + Elites", {{"storm_grunt", 4}, {"storm_grunt_ultra", 1}, {"storm_elite", 2}}, false, false, false, false, false, true},
        {"Covenant", "Grunts + Jackals", {{"storm_grunt", 4}, {"storm_jackal", 3}}, false, false, false, false, false, true},
        {"Covenant", "Grunts + Hunters", {{"storm_hunter", 2}, {"storm_grunt", 4}}, true, false, false, false, false, true},
        {"Covenant", "Elite squad", {{"storm_elite", 3}, {"storm_elite_officer", 1}}, false, false, false, false, false, true},
        {"Covenant", "Elite strike team", {{"storm_elite_general", 1}, {"storm_elite_zealot|storm_elite_officer", 1}, {"storm_elite_specops|storm_elite_ranger", 3}}, false, false, false, false, false, true},
        {"Covenant", "Elite rangers", {{"storm_elite_ranger", 4}}, false, false, false, false, false, true},
        {"Covenant", "Jetpack Elites", {{"storm_elite_jetpack|storm_elite_jetpack_jump", 4}}, false, false, false, false, false, true},
        {"Covenant", "Jackal squad", {{"storm_jackal", 4}, {"storm_jackal_major", 1}}, false, false, false, false, false, true},
        {"Covenant", "Sniper nest", {{"storm_jackal_sniper", 4}}, false, false, false, false, false, true},
        {"Covenant", "Jackal rangers", {{"storm_jackal_ranger", 3}, {"storm_jackal_ranger_shield", 2}}, false, false, false, false, false, true},
        {"Covenant", "Hunter pair", {{"storm_hunter", 2}}, true, false, false, false, false, true},
        {"Covenant", "Hunters + Elites", {{"storm_hunter", 2}, {"storm_elite", 2}}, true, false, false, false, false, true},
        {"Covenant", "Covenant war party", {{"storm_grunt", 3}, {"storm_jackal", 2}, {"storm_elite", 2}, {"storm_hunter", 1}}, true, false, false, false, false, true},
        {"Prometheans", "Crawler pack", {{"storm_pawn", 6}, {"storm_pawn_prime", 2}}, false, false, false, false, false, true},
        {"Prometheans", "Crawler snipers", {{"storm_pawn_sniper", 4}}, false, false, false, false, false, true},
        {"Prometheans", "Knight squad", {{"storm_knight", 2}, {"storm_knight_ranger", 2}}, false, false, false, false, false, true},
        {"Prometheans", "Crawlers + Knights", {{"storm_pawn", 6}, {"storm_pawn_prime", 2}, {"storm_knight", 2}}, false, false, false, false, false, true},
        {"Prometheans", "Commander's guard", {{"storm_knight_commander|storm_knight_ranger", 1}, {"storm_knight", 2}, {"storm_knight_battlewagon", 1}}, true, false, false, false, false, true},
        {"Prometheans", "Battlewagons", {{"storm_knight_battlewagon", 2}}, true, false, false, false, false, true},
        {"Prometheans", "Bishop's escort", {{"storm_bishop", 1}, {"storm_knight", 2}, {"storm_pawn", 4}}, true, false, false, false, false, true},
        {"Prometheans", "Sentinel patrol", {{"storm_sentinel", 4}}, false, false, false, false, false, true},
        {"Human", "Marine squad", {{"storm_marine", 4}, {"storm_marine_oni|storm_marine", 1}}, false, false, false, false, false, true},
        {"Human", "ONI marines", {{"storm_marine_oni", 4}}, false, false, false, false, false, true},
        {"Ridiculous", "Grunt swarm (22)", {{"storm_grunt", 16}, {"storm_grunt_ultra", 4}, {"storm_grunt_imperial", 2}}, false, false, false, false, false, true},
        {"Ridiculous", "Elite army (14)", {{"storm_elite", 6}, {"storm_elite_officer", 3}, {"storm_elite_specops|storm_elite_ranger", 3}, {"storm_elite_general", 2}}, false, false, false, false, false, true},
        {"Ridiculous", "Hunter apocalypse (8)", {{"storm_hunter", 8}}, true, false, false, false, false, true},
        {"Ridiculous", "Jackal horde (20)", {{"storm_jackal", 12}, {"storm_jackal_major", 4}, {"storm_jackal_sniper", 4}}, false, false, false, false, false, true},
        {"Ridiculous", "Crawler swarm (24)", {{"storm_pawn", 16}, {"storm_pawn_prime", 4}, {"storm_pawn_sniper", 4}}, false, false, false, false, false, true},
        {"Ridiculous", "Knight army (12)", {{"storm_knight", 6}, {"storm_knight_ranger", 3}, {"storm_knight_battlewagon|storm_knight", 2}, {"storm_knight_commander|storm_knight_ranger", 1}}, true, false, false, false, false, true},
        {"Ridiculous", "Bishop brigade (6)", {{"storm_bishop", 6}}, true, false, false, false, false, true},
        {"Ridiculous", "Marine army (20)", {{"storm_marine", 16}, {"storm_marine_oni|storm_marine", 4}}, false, false, false, false, false, true},
        {"Ridiculous", "Marines vs Elites (13)", {{"storm_marine", 6}, {"storm_marine_oni|storm_marine", 1}, {"storm_elite", 6}}, false, false, false, false, false, true},
        {"Ridiculous", "Everyone (one of each AI)", {{"*", 1}}, true, false, false, false, false, true},
        {"Ridiculous", "Everyone, twice", {{"*", 2}}, true, false, false, false, false, true},
    };
    constexpr int kSquadCount = (int)(sizeof(kSquads) / sizeof(kSquads[0]));
    constexpr int kSquadMaskBits = (int)(sizeof(Status::squadsAvailable) / sizeof(uint64_t)) * 64;
    // Halo 4's rows come after the 128th: Status::squadsAvailable needs 3 words (game.h makes it 4);
    // until then those rows read as unavailable instead of writing past the mask.

    // Which squads a title lists: CE, Halo 2, Halo 3, ODST and Halo 4 have their own, the rest use Reach's.
    inline bool SquadInTitle(const Squad& squad, const char* title) {
        if (title && !strcmp(title, "Halo: CE")) return squad.ce;
        if (title && !strcmp(title, "Halo 2")) return squad.h2;
        if (title && !strcmp(title, "Halo 3")) return squad.h3;
        if (title && !strcmp(title, "Halo 3: ODST")) return squad.odst;
        if (title && !strcmp(title, "Halo 4")) return squad.h4;
        return !squad.ce && !squad.h2 && !squad.h3 && !squad.odst && !squad.h4;
    }
    inline void SetSquadAvailable(Status& status, int index) {
        if (index >= 0 && index < kSquadMaskBits) status.squadsAvailable[index >> 6] |= 1ull << (index & 63);
    }
    inline bool SquadAvailable(const Status& status, int index) {
        return index >= 0 && index < kSquadMaskBits && ((status.squadsAvailable[index >> 6] >> (index & 63)) & 1);
    }

    // Characters never part of "everyone": invisible pilots and civilians.
    inline bool SquadSkips(const std::string& name) {
        return name.rfind("null", 0) == 0 || name.rfind("civilian", 0) == 0;
    }

    // The tag datum for one member of a squad ("a|b" picks the first the
    // map has), or 0xFFFFFFFF.
    inline uint32_t SquadResolve(const std::vector<TagChoice>& characters, const char* alternatives) {
        std::string list = alternatives;
        size_t start = 0;
        while (start <= list.size()) {
            size_t bar = list.find('|', start);
            std::string name = list.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            for (const auto& c : characters)
                if (c.name == name) return c.datum;
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        return 0xFFFFFFFF;
    }

    // Every AI a squad makes, in order (a member the map lacks is skipped).
    inline std::vector<uint32_t> SquadTags(const Squad& squad, const std::vector<TagChoice>& characters) {
        std::vector<uint32_t> out;
        for (const SquadMember& m : squad.members) {
            if (m.count <= 0) break;
            if (!strcmp(m.characters, "*")) {
                for (int n = 0; n < m.count; ++n)
                    for (const auto& c : characters)
                        if (!SquadSkips(c.name)) out.push_back(c.datum);
                continue;
            }
            uint32_t tag = SquadResolve(characters, m.characters);
            if (tag == 0xFFFFFFFF) continue;
            for (int n = 0; n < m.count; ++n) out.push_back(tag);
        }
        return out;
    }
}
