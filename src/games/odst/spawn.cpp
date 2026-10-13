// ODST spawning: a port of Halo 3's (spawn.cpp there), itself ported from
// Reach's (see NOTES.md). Unlike Halo 3, ODST makes actors from a request,
// as Reach does, and its squads arm AI through designer cells. A ray from player 1's head along their view finds
// the spot; AI are squadless actors made by the engine's own actor_spawn;
// tags only loaded in some parts of a mission get their designer zone loaded
// first.

#include "spawn.h"

#include "cheats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "games/fun.h"
#include "internal.h"
#include "tags.h"

using namespace OdstCheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kSpawnRange = 300.0f;  // world units the spawn ray reaches
    constexpr float kSpawnBackOff = 0.3f;  // back along the ray from the hit, so it isn't inside the surface
    constexpr float kSpawnLift = 0.25f;    // and up, so it doesn't clip into the ground
    constexpr float kVehicleLift = 0.5f;   // vehicles and scenery: above the higher of the hit and the backed-off point
    constexpr ULONGLONG kZoneLoadMs = 5000;  // how long a spawn waits for its designer zone
    constexpr int kMaxSpawnedAi = 512;
    constexpr ULONGLONG kRespawnMs = 5000;   // dead this long: respawned, or left as a body

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using HeadPositionFn = void(__fastcall*)(uint32_t unit, float* out);
    using CollisionTestFn = bool(__fastcall*)(uint64_t flags, bool, const float* point, const float* vector,
                                              uint32_t ignore, uint32_t ignore2, int32_t, void* result);
    using ActorRequestInitFn = void(__fastcall*)(void* request);
    using ActorSpawnFn = uint32_t(__fastcall*)(int16_t spawnPoint, void* request);
    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);
    using ActorSetTeamFn = void(__fastcall*)(uint32_t actor, int16_t team);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up, const void* location, bool);
    using SetVelocityFn = void(__fastcall*)(uint32_t object, const float* linear, const float* angular);

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // --- Lists (render thread builds, game thread reads the variant table) --

    // A Vehicles-list entry other than a plain tag: a model variant. Its id is
    // kVehicleChoiceId | index (no tag has salt 0xFFFF).
    constexpr uint32_t kVehicleChoiceId = 0xFFFF0000;
    struct VehicleChoice {
        uint32_t vehicle = 0xFFFFFFFF;
        uint32_t variant = 0;  // model variant name (string id), 0 = default
        std::string name;
    };

    std::vector<Games::TagChoice> g_characters;
    std::vector<Games::TagChoice> g_vehicles;
    std::vector<Games::TagChoice> g_objects;
    std::mutex g_listsLock;  // guards g_vehicleChoices
    std::vector<VehicleChoice> g_vehicleChoices;

    bool FindVehicleChoice(uint32_t id, VehicleChoice& out) {
        if ((id & 0xFFFF0000) != kVehicleChoiceId) return false;
        std::lock_guard<std::mutex> lock(g_listsLock);
        size_t index = id & 0xFFFF;
        if (index >= g_vehicleChoices.size()) return false;
        out = g_vehicleChoices[index];
        return true;
    }

    uint32_t PaletteDatum(char* scnr, int palette, int index) {
        char* entry = OdstTags::BlockElement(scnr, palette, ODST_PALETTE_ENTRY_SIZE, index);
        return entry ? Field<uint32_t>(entry, ODST_PALETTE_ENTRY_DATUM) : 0xFFFFFFFF;
    }

    int PaletteIndex(char* scnr, int palette, uint32_t tag) {
        int count = OdstTags::BlockCount(scnr, palette);
        for (int i = 0; i < count && i < 1024; ++i)
            if (PaletteDatum(scnr, palette, i) == tag) return i;
        return -1;
    }

    // A character with a unit somewhere up its parent chain (templates such
    // as "generic" have none and can't be spawned).
    bool Spawnable(uint32_t character) {
        __try {
            for (int depth = 0; depth < 8 && character != 0xFFFFFFFF; ++depth) {
                char* tag = OdstTags::Get(character);
                if (!tag) return false;
                if (Field<uint32_t>(tag, ODST_CHAR_UNIT) != 0xFFFFFFFF) return true;
                character = Field<uint32_t>(tag, ODST_CHAR_PARENT);
            }
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Vehicles: the mission's vehicle palette (other 'vehi' tags are turrets
    // and parts), each followed by its model's other variants ("warthog
    // (gauss)"). Wrecked, damaged and cinematic ones are left out.
    void BuildVehicleList(char* scnr) {
        constexpr const char* kSkip[] = {"default", "damaged", "wrecked", "cinematic", "prop", "filler"};
        std::vector<Games::TagChoice> list;
        std::vector<VehicleChoice> choices;
        for (auto& vehicle : OdstTags::ListGroup(ODST_TAG_VEHI)) {
            if (PaletteIndex(scnr, ODST_SCNR_VEHICLE_PALETTE, vehicle.datum) < 0) continue;
            list.push_back(vehicle);
            char* tag = OdstTags::Get(vehicle.datum);
            char* model = tag ? OdstTags::Get(Field<uint32_t>(tag, ODST_OBJE_MODEL)) : nullptr;
            uint32_t defaultName = tag ? Field<uint32_t>(tag, ODST_OBJE_DEFAULT_VARIANT) : 0;
            int count = OdstTags::BlockCount(model, ODST_HLMT_VARIANTS);
            for (int v = 0; v < count && v < 64; ++v) {
                char* variant = OdstTags::BlockElement(model, ODST_HLMT_VARIANTS, ODST_HLMT_VARIANT_SIZE, v);
                uint32_t name = variant ? Field<uint32_t>(variant, 0) : 0;
                std::string label = OdstTags::StringName(name);
                bool skip = !name || name == defaultName || label.empty();
                for (const char* s : kSkip) skip |= label.find(s) != std::string::npos;
                if (skip) continue;
                VehicleChoice c;
                c.vehicle = vehicle.datum;
                c.variant = name;
                c.name = vehicle.name + " (" + label + ")";
                list.push_back({kVehicleChoiceId | (uint32_t)choices.size(), c.name});
                choices.push_back(c);
            }
        }
        g_vehicles = list;
        std::lock_guard<std::mutex> lock(g_listsLock);
        g_vehicleChoices = choices;
    }

    // Objects: the mission's crate, equipment, machine, control and scenery
    // palettes, prefixed by kind and sorted within each. Editing helpers are
    // left out by name.
    void BuildObjectList(char* scnr) {
        struct Kind { int palette; const char* prefix; };
        constexpr Kind kKinds[] = {{ODST_SCNR_CRATE_PALETTE, "crate"},
                                   {ODST_SCNR_EQUIPMENT_PALETTE, "equipment"},
                                   {ODST_SCNR_MACHINE_PALETTE, "machine"},
                                   {ODST_SCNR_CONTROL_PALETTE, "control"},
                                   {ODST_SCNR_SCENERY_PALETTE, "scenery"}};
        constexpr const char* kHelpers[] = {"invisible", "pathfinding", "ui_object", "fx_", "standin", "kill_"};
        g_objects.clear();
        for (const Kind& kind : kKinds) {
            std::vector<Games::TagChoice> found;
            int count = OdstTags::BlockCount(scnr, kind.palette);
            for (int i = 0; i < count && i < 1024; ++i) {
                uint32_t tag = PaletteDatum(scnr, kind.palette, i);
                if (tag == 0xFFFFFFFF || !OdstTags::Get(tag)) continue;
                std::string name = OdstTags::FullName(tag);
                name = name.substr(name.find_last_of('\\') + 1);
                bool helper = name.empty();
                for (const char* h : kHelpers) helper |= name.find(h) != std::string::npos;
                bool dup = false;
                for (auto& f : found) dup |= f.datum == tag;
                if (!helper && !dup) found.push_back({tag, std::string(kind.prefix) + ": " + name});
            }
            std::sort(found.begin(), found.end(),
                      [](const Games::TagChoice& a, const Games::TagChoice& b) { return a.name < b.name; });
            g_objects.insert(g_objects.end(), found.begin(), found.end());
        }
    }

    // --- Results for the menu -------------------------------------------------

    std::mutex g_resultLock;
    uint32_t g_spawnDone = 0;
    char g_spawnMessage[96] = "";

    void SpawnResult(const char* text) {
        std::lock_guard<std::mutex> lock(g_resultLock);
        sprintf_s(g_spawnMessage, "%s", text);
        ++g_spawnDone;
        LOG("ODST: spawn: %s", text);
    }

    std::string ShortName(uint32_t tag) {
        std::string name = OdstTags::FullName(tag);
        name = name.substr(name.find_last_of('\\') + 1);
        return name.empty() ? "selection" : name;
    }

    VehicleChoice ChoiceFor(uint32_t selection) {
        VehicleChoice c;
        if (!FindVehicleChoice(selection, c)) c.vehicle = selection;
        return c;
    }

    std::string SelectionName(uint32_t selection) {
        VehicleChoice c = ChoiceFor(selection);
        return c.name.empty() ? ShortName(c.vehicle) : c.name;
    }

    // --- Designer zones (as in Reach) --------------------------------------------
    // A tag the mission only loads in some parts of the level is in one or more
    // designer zones. When a spawn fails and its zone isn't loaded, the mod
    // loads it (pending masks + flag, which the game's zone update applies)
    // and retries each frame. Zones it loaded are kept for the rest of the map.

    uint32_t g_forcedZones = 0;
    char* g_forcedScenario = nullptr;

    uint32_t& ActiveZones() { return *(uint32_t*)(g_module + OFFSET_ODST_DESIGNER_ZONES_ACTIVE); }

    uint32_t DesignerZonesOf(char* scnr, uint32_t tag) {
        int list = 0, palette = 0;
        switch (OdstTags::GroupOf(tag)) {
            case ODST_TAG_CHAR: list = ODST_DESIGNER_ZONE_CHARACTERS; palette = ODST_SCNR_CHARACTER_PALETTE; break;
            case ODST_TAG_WEAP: list = ODST_DESIGNER_ZONE_WEAPONS; palette = ODST_SCNR_WEAPON_PALETTE; break;
            case ODST_TAG_VEHI: list = ODST_DESIGNER_ZONE_VEHICLES; palette = ODST_SCNR_VEHICLE_PALETTE; break;
            case ODST_TAG_BIPD: list = ODST_DESIGNER_ZONE_BIPEDS; palette = ODST_SCNR_BIPED_PALETTE; break;
            case ODST_TAG_EQIP: list = ODST_DESIGNER_ZONE_EQUIPMENT; palette = ODST_SCNR_EQUIPMENT_PALETTE; break;
            case ODST_TAG_SCEN: list = ODST_DESIGNER_ZONE_SCENERY; palette = ODST_SCNR_SCENERY_PALETTE; break;
            case ODST_TAG_MACH: list = ODST_DESIGNER_ZONE_MACHINES; palette = ODST_SCNR_MACHINE_PALETTE; break;
            case ODST_TAG_BLOC: list = ODST_DESIGNER_ZONE_CRATES; palette = ODST_SCNR_CRATE_PALETTE; break;
            default: return 0;
        }
        int index = PaletteIndex(scnr, palette, tag);
        if (index < 0) return 0;
        uint32_t zones = 0;
        int count = OdstTags::BlockCount(scnr, ODST_SCNR_DESIGNER_ZONES);
        for (int z = 0; z < count && z < 32; ++z) {
            char* zone = OdstTags::BlockElement(scnr, ODST_SCNR_DESIGNER_ZONES, ODST_SCNR_DESIGNER_ZONE_SIZE, z);
            int entries = OdstTags::BlockCount(zone, list);
            for (int e = 0; e < entries && e < 256; ++e) {
                char* entry = OdstTags::BlockElement(zone, list, 2, e);
                if (entry && Field<int16_t>(entry, 0) == index) zones |= 1u << z;
            }
        }
        return zones;
    }

    // The zones to load for a tag that failed to spawn, or 0 if it's in no
    // designer zone or one is already loaded.
    uint32_t ZoneToLoad(char* scnr, uint32_t tag) {
        uint32_t zones = DesignerZonesOf(scnr, tag);
        if (!zones || (zones & ActiveZones())) return 0;
        return zones & (0u - zones);  // the first one is enough
    }

    void LoadZones(uint32_t zones) {
        g_forcedZones |= zones;
        uint32_t missing = zones & ~ActiveZones();
        if (!missing) return;
        uint32_t* pending = (uint32_t*)(g_module + OFFSET_ODST_DESIGNER_ZONES_PENDING);
        pending[0] &= ~missing;  // deactivate
        pending[1] |= missing;   // activate
        *(uint8_t*)(g_module + OFFSET_ODST_ZONE_UPDATE_PENDING) = 1;
        LOG("ODST: loading designer zones %08X (active %08X)", missing, ActiveZones());
    }

    // Keeps the zones the mod loaded: zone sets no longer unload them (a tag
    // edit), and they're loaded again if the active mask loses them.
    void KeepForcedZones(char* scnr) {
        if (scnr != g_forcedScenario) {
            g_forcedScenario = scnr;
            g_forcedZones = 0;
        }
        if (!g_forcedZones || !scnr) return;
        int count = OdstTags::BlockCount(scnr, ODST_SCNR_ZONE_SETS);
        for (int i = 0; i < count && i < 64; ++i)
            if (char* set = OdstTags::BlockElement(scnr, ODST_SCNR_ZONE_SETS, ODST_SCNR_ZONE_SET_SIZE, i))
                Field<uint32_t>(set, ODST_ZONE_SET_FORBIDDEN) &= ~g_forcedZones;
        if (g_forcedZones & ~ActiveZones()) {
            LOG("ODST: designer zones %08X were unloaded; loading them again", g_forcedZones & ~ActiveZones());
            LoadZones(g_forcedZones);
        }
    }

    // --- Spawning ------------------------------------------------------------------

    // AI the mod spawned, followed after death for "Enable respawn".
    struct SpawnedAi {
        uint32_t unit = 0xFFFFFFFF;
        uint32_t character = 0xFFFFFFFF;
        int team = 0;
        ULONGLONG deadAt = 0;  // when it was first seen dead, 0 = alive
    };
    SpawnedAi g_spawnedAi[kMaxSpawnedAi];

    // A spawn waiting for its designer zone to load.
    struct ZoneWait {
        bool active = false;
        uint32_t selection = 0xFFFFFFFF;
        uint32_t zone = 0;
        ULONGLONG deadline = 0;
        int team = 0;
        float position[3] = {};
        float yaw = 0.0f;
    };
    ZoneWait g_wait;
    uint32_t g_spawnSerial = 0;
    bool g_haveSerial = false;

    // Loadout rolls: GetTickCount64 doesn't change within a squad spawn, so every member
    // got the same pick; a xorshift gives each AI its own roll.
    uint32_t LoadoutRandom() {
        static uint32_t x = 0;
        if (!x) x = (uint32_t)GetTickCount64() | 1u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return x;
    }

    // When no squad of the mission arms a character, its spawn would be unarmed. Falls
    // back to weapons that suit its kind, from those the mission's weapon palette
    // holds (a squad can only name palette entries). Returns true if a weapon was set.
    bool FallbackLoadout(char* scnr, uint32_t character, uint32_t* weapon, uint32_t* secondary) {
        std::string name = ShortName(character);
        struct Rule { const char* prefix; std::vector<const char*> weapons; };
        static const Rule kRules[] = {
            {"brute_chieftain", {"gravity_hammer", "brute_shot"}},
            {"brute", {"spike_rifle", "brute_shot", "plasma_rifle", "needler", "mauler"}},
            {"grunt", {"plasma_pistol", "needler", "plasma_rifle"}},
            {"jackal_sniper", {"beam_rifle"}},
            {"jackal", {"plasma_pistol", "covenant_carbine"}},
            {"elite", {"plasma_rifle", "covenant_carbine", "needler", "energy_blade"}},
            {"marine", {"battle_rifle", "assault_rifle", "smg", "smg_silenced", "magnum"}},
            {"odst", {"smg", "smg_silenced", "magnum", "battle_rifle", "assault_rifle"}},
            {"rookie", {"smg", "smg_silenced", "magnum"}},
            {"buck", {"smg", "smg_silenced", "magnum"}},
            {"dutch", {"smg", "smg_silenced", "magnum"}},
            {"romeo", {"smg", "smg_silenced", "magnum"}},
            {"mickey", {"smg", "smg_silenced", "magnum"}},
        };
        const Rule* rule = nullptr;
        for (const Rule& r : kRules)
            if (name.rfind(r.prefix, 0) == 0) { rule = &r; break; }
        if (!rule) return false;
        std::vector<uint32_t> options;
        for (auto& w : OdstTags::ListGroup(ODST_TAG_WEAP))
            for (const char* want : rule->weapons)
                if (w.name == want && PaletteIndex(scnr, ODST_SCNR_WEAPON_PALETTE, w.datum) >= 0)
                    options.push_back(w.datum);
        if (options.empty()) return false;
        *weapon = options[LoadoutRandom() % options.size()];
        *secondary = 0xFFFFFFFF;
        LOG("ODST: no squad arms %s; gave it a fallback weapon", name.c_str());
        return true;
    }

    // ODST characters carry no loadout: squads arm their AI through designer
    // cells (weighted lists of characters and weapons) and single locations
    // (their own picks, else their cell's). A spawned character gets one of
    // the weapons the mission gives it, at random (weighted by the cells'
    // chances), or its parent's; else unarmed. Returns weapon tag datums.
    void PickLoadout(char* scnr, uint32_t character, uint32_t* weapon, uint32_t* secondary) {
        const uint32_t original = character;
        int16_t found[512][2];
        int count = 0;
        auto add = [&](int16_t w, int16_t w2, int times) {
            for (int t = 0; t < times && count < 512; ++t) {
                found[count][0] = w;
                found[count][1] = w2;
                ++count;
            }
        };
        // A cell's first entry of a list (e.g. its secondary weapon), or -1.
        auto first = [](char* cell, int list) -> int16_t {
            char* e = OdstTags::BlockElement(cell, list, ODST_CELL_ENTRY_SIZE, 0);
            return e ? Field<int16_t>(e, ODST_CELL_ENTRY_INDEX) : -1;
        };
        auto cellHas = [](char* cell, int index) {
            int n = OdstTags::BlockCount(cell, ODST_CELL_CHARACTERS);
            for (int k = 0; k < n && k < 64; ++k) {
                char* e = OdstTags::BlockElement(cell, ODST_CELL_CHARACTERS, ODST_CELL_ENTRY_SIZE, k);
                if (e && Field<int16_t>(e, ODST_CELL_ENTRY_INDEX) == index) return true;
            }
            return false;
        };
        auto cellWeapons = [&](char* cell) {
            int16_t second = first(cell, ODST_CELL_SECONDARIES);
            int n = OdstTags::BlockCount(cell, ODST_CELL_WEAPONS);
            for (int k = 0; k < n && k < 64; ++k) {
                char* e = OdstTags::BlockElement(cell, ODST_CELL_WEAPONS, ODST_CELL_ENTRY_SIZE, k);
                if (!e || Field<int16_t>(e, ODST_CELL_ENTRY_INDEX) < 0) continue;
                int chance = Field<int16_t>(e, ODST_CELL_ENTRY_CHANCE);
                add(Field<int16_t>(e, ODST_CELL_ENTRY_INDEX), second, chance > 0 && chance < 16 ? chance : 1);
            }
        };
        int squads = OdstTags::BlockCount(scnr, ODST_SCNR_SQUADS);
        for (int depth = 0; depth < 8 && count == 0 && character != 0xFFFFFFFF; ++depth) {
            int index = PaletteIndex(scnr, ODST_SCNR_CHARACTER_PALETTE, character);
            for (int i = 0; index >= 0 && i < squads && i < 1024 && count < 512; ++i) {
                char* squad = OdstTags::BlockElement(scnr, ODST_SCNR_SQUADS, ODST_SQUAD_SIZE, i);
                if (!squad) continue;
                int cells = OdstTags::BlockCount(squad, ODST_SQUAD_CELLS);
                for (int k = 0; k < cells && k < 64; ++k) {
                    char* cell = OdstTags::BlockElement(squad, ODST_SQUAD_CELLS, ODST_CELL_SIZE, k);
                    if (cell && cellHas(cell, index)) cellWeapons(cell);
                }
                int singles = OdstTags::BlockCount(squad, ODST_SQUAD_SINGLES);
                for (int k = 0; k < singles && k < 64; ++k) {
                    char* one = OdstTags::BlockElement(squad, ODST_SQUAD_SINGLES, ODST_SINGLE_SIZE, k);
                    if (!one || Field<int16_t>(one, ODST_SINGLE_CHARACTER) != index) continue;
                    if (Field<int16_t>(one, ODST_SINGLE_WEAPON) >= 0) {
                        add(Field<int16_t>(one, ODST_SINGLE_WEAPON), Field<int16_t>(one, ODST_SINGLE_SECONDARY), 1);
                    } else if (char* cell = OdstTags::BlockElement(squad, ODST_SQUAD_CELLS, ODST_CELL_SIZE,
                                                                    Field<int16_t>(one, ODST_SINGLE_CELL))) {
                        cellWeapons(cell);
                    }
                }
            }
            if (count == 0) {
                char* tag = OdstTags::Get(character);
                character = tag ? Field<uint32_t>(tag, ODST_CHAR_PARENT) : 0xFFFFFFFF;
            }
        }
        if (count == 0) {
            if (FallbackLoadout(scnr, original, weapon, secondary)) return;
            LOG("ODST: no squad in this mission arms this character; spawning it unarmed");
            return;
        }
        int pick = (int)(LoadoutRandom() % (uint32_t)count);
        *weapon = PaletteDatum(scnr, ODST_SCNR_WEAPON_PALETTE, found[pick][0]);
        if (found[pick][1] >= 0) *secondary = PaletteDatum(scnr, ODST_SCNR_WEAPON_PALETTE, found[pick][1]);
    }

    // --- Spawn team picker (Settings::spawnTeam) ---------------------------
    // 0 = their own team, 1 = Ally, 2 = Covenant, 3 = Flood (not in ODST: treated
    // as 0), 4 = Rogue, 5 = Random. Team numbers are ODST's `team` script enum
    // (default 0, player 1, human 2, covenant 3, flood 4, sentinel 5, heretic 6,
    // prophet 7, guilty 8, unused9..15): read from the name table in the DLL
    // (pointer table at RVA 0x8E1A70) and cross-checked with Halo 3's script XML.
    //
    // Allegiance globals (TLS 47): friends[a] at +0x144 (bit b = a counts b as a
    // friend, campaign only). The reset (RVA 0x17E792) zeroes the whole block and
    // only sets bit i of the +0x184 table, and game_team_is_enemy (0x17EB50) is
    // `a != b && !(table184[a] bit b)`. A team nobody lists is therefore hostile
    // to everyone, but the engine hard-codes same team = not an enemy, so two AI
    // on one rogue team would be friends. Same approach as Halo 3 (confirmed there):
    // Rogue = free-for-all, each rogue takes the least-used of the unused teams 9..15
    // (live actors per team counted off the actor array) and their matrix rows are
    // kept empty; Random = a team that has a live actor in the level right now.
    // Static analysis only (ODST isn't live); od.py/text.pkl RVAs.
    constexpr int kRogueTeams[] = {9, 10, 11, 12, 13, 14, 15};  // unused teams; nobody is allied with them
    constexpr int kActorTeam = 0x20;  // int16 (actor_set_team writes it, RVA 0x5A8CD7)
    constexpr int kRandomTeams[] = {ODST_TEAM_HUMAN, ODST_TEAM_COVENANT, 5 /* sentinel */, 9, 10};

    uint32_t g_random = 0x2545F491u;  // xorshift
    float Random01() {
        g_random ^= g_random << 13;
        g_random ^= g_random >> 17;
        g_random ^= g_random << 5;
        return (g_random >> 8) / 16777216.0f;
    }

    // Rogue teams: no friends, not even themselves, so rogues fight everyone
    // including each other. Cleared again every time because mission scripts may
    // rewrite the matrix.
    void MakeRoguesHostile(char* allegiance) {
        for (int r : kRogueTeams) {
            Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + r * 4) = 0;
            for (int t = 0; t < ODST_TEAM_COUNT; ++t)
                Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + t * 4) &= ~(1u << r);
        }
    }

    bool IsRogueTeam(int team) { return team >= kRogueTeams[0] && team <= kRogueTeams[_countof(kRogueTeams) - 1]; }

    // Live actors per team (index = team, 0..15), read off the actor array.
    void CountActorTeams(void** state, int* counts) {
        for (int t = 0; t < ODST_TEAM_COUNT; ++t) counts[t] = 0;
        auto actors = (Blam::DataArray*)state[ODST_TLS_SLOT_ACTORS];
        if (!actors) return;
        for (int i = 0; i < actors->highWater && i < 4096; ++i) {
            char* actor = actors->Entry(i);
            if (!*(uint16_t*)actor) continue;
            int16_t team = Field<int16_t>(actor, kActorTeam);
            if (team >= 0 && team < ODST_TEAM_COUNT) ++counts[team];
        }
    }

    // Rogue = every man for himself: each rogue wants a team nobody else is on, so
    // take the unused rogue team with the fewest AI on it, ties at random.
    int FreeForAllTeam(void** state) {
        int counts[ODST_TEAM_COUNT];
        CountActorTeams(state, counts);
        int best = -1, bestCount = 1 << 30, ties = 0;
        for (int t : kRogueTeams) {
            if (counts[t] < bestCount) { best = t; bestCount = counts[t]; ties = 1; }
            else if (counts[t] == bestCount && Random01() < 1.0f / ++ties) best = t;
        }
        return best;
    }

    // Random = a pick among the teams that exist in this level right now (teams that
    // have a live actor, the rogue teams the mod hands out left out); if none, the
    // classic factions.
    int RandomLevelTeam(void** state) {
        int counts[ODST_TEAM_COUNT];
        CountActorTeams(state, counts);
        int teams[ODST_TEAM_COUNT], n = 0;
        for (int t = 1; t < ODST_TEAM_COUNT; ++t)
            if (counts[t] > 0 && !IsRogueTeam(t)) teams[n++] = t;
        if (n == 0) return kRandomTeams[(int)(Random01() * _countof(kRandomTeams)) % (int)_countof(kRandomTeams)];
        return teams[(int)(Random01() * n) % n];
    }

    // The Covenant team a mission's enemies are on isn't always 3 (the levels use covenant 3,
    // prophet 7 or heretic 6, and different teams are hostile to each other, so a team 3 spawn
    // would fight the mission's own team 7 Covenant beside the player). Prefers the one of the
    // three that live AI are on and the player's team isn't allied with (most AI wins); with
    // none alive, the first of them the player isn't allied with; failing that the humans.
    int HostileCovenantTeam(void** state, char* allegiance, int playerTeam) {
        if (!allegiance) return ODST_TEAM_COVENANT;
        uint32_t friends = Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + playerTeam * 4);
        uint32_t allied = Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + 0x40 + playerTeam * 4);
        auto hostile = [&](int t) { return !((friends | allied) & (1u << t)); };
        int counts[ODST_TEAM_COUNT];
        CountActorTeams(state, counts);
        int best = -1;
        for (int t : {ODST_TEAM_COVENANT, 7, 6})
            if (hostile(t) && counts[t] > 0 && (best < 0 || counts[t] > counts[best])) best = t;
        if (best >= 0) return best;
        for (int t : {ODST_TEAM_COVENANT, 7, 6})
            if (hostile(t)) return t;
        return hostile(ODST_TEAM_HUMAN) ? ODST_TEAM_HUMAN : ODST_TEAM_COVENANT;
    }

    // The game's team number for a picker choice, or 0 to leave the AI on its own.
    // Called per spawned AI (and per respawn), so Random and Rogue vary.
    int ResolveSpawnTeam(void** state, Blam::DataArray* objects, int choice) {
        if (choice < 1 || choice > 5 || choice == 3) return 0;
        __try {
            char* allegiance = (char*)state[ODST_TLS_SLOT_ALLEGIANCE];
            int playerTeam = ODST_TEAM_PLAYER;
            auto players = (Blam::DataArray*)state[ODST_TLS_SLOT_PLAYERS];
            if (players && players->data && objects && objects->data) {
                Players p = FindPlayers(players, objects);
                if (p.count > 0) {
                    int16_t t = Field<int16_t>(p.units[0], ODST_UNIT_TEAM);
                    if (t > 0 && t < ODST_TEAM_COUNT) playerTeam = t;
                }
            }
            int team = 0;
            switch (choice) {
                case 1: {  // Ally: whichever team is friends with player 1's team; human by default
                    team = ODST_TEAM_HUMAN;
                    if (allegiance) {
                        uint32_t friends = Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + playerTeam * 4);
                        if (!(friends & (1u << ODST_TEAM_HUMAN)))
                            for (int t = ODST_TEAM_COVENANT; t < ODST_TEAM_COUNT; ++t)
                                if (t != playerTeam && t != 4 && (friends & (1u << t))) { team = t; break; }
                    }
                    return team;
                }
                case 2: return HostileCovenantTeam(state, allegiance, playerTeam);
                case 4: team = FreeForAllTeam(state); break;
                default: team = RandomLevelTeam(state); break;  // 5
            }
            if (allegiance && team >= kRogueTeams[0]) MakeRoguesHostile(allegiance);
            return team;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return choice == 1 ? ODST_TEAM_HUMAN : choice == 2 ? ODST_TEAM_COVENANT : 0;
        }
    }

    void** g_lastState = nullptr;  // the game thread's state, set by Tick

    // actor_set_team (RVA 0x5A8C84) on an active actor only rewrites the actor's team
    // and its perception props; the unit's own team (+0x16C) is written only for
    // inactive actors. A spawned AI is activated first, so write both.
    void SetTeamBoth(void** state, uint32_t actor, int16_t team) {
        Engine<ActorSetTeamFn>(OFFSET_ODST_ACTOR_SET_TEAM)(actor, team);
        if (!state) return;
        auto actors = (Blam::DataArray*)state[ODST_TLS_SLOT_ACTORS];
        auto objects = (Blam::DataArray*)state[ODST_TLS_SLOT_OBJECTS];
        uint16_t index = actor & 0xFFFF;
        if (!actors || !objects || index >= actors->highWater) return;
        char* a = actors->Entry(index);
        if (*(uint16_t*)a != (actor >> 16)) return;
        if (char* unit = Blam::ObjectFromDatum(objects, Field<uint32_t>(a, ODST_ACTOR_UNIT)))
            Field<int16_t>(unit, ODST_UNIT_TEAM) = team;
    }

    // An AI as a squadless actor, from a request built like the game's own
    // squadless spawns (0x60021C). `team` is a game team number (0 = leave as is).
    // Returns the actor or -1.
    uint32_t SpawnActor(char* scnr, uint32_t character, const float* position, float yaw, int team) {
        alignas(16) static uint8_t request[0x100];
        char* r = (char*)request;
        memset(request, 0, sizeof(request));
        Engine<ActorRequestInitFn>(OFFSET_ODST_ACTOR_REQUEST_INIT)(request);
        memcpy(r + ODST_ACTOR_REQUEST_POSITION, position, 3 * sizeof(float));
        Field<float>(r, ODST_ACTOR_REQUEST_YAW) = yaw;
        Field<float>(r, ODST_ACTOR_REQUEST_PITCH) = 0.0f;
        Field<uint32_t>(r, ODST_ACTOR_REQUEST_18) = 0;
        Field<uint16_t>(r, ODST_ACTOR_REQUEST_FLAGS) |= ODST_ACTOR_REQUEST_FLAG_FREE;
        Field<uint16_t>(r, ODST_ACTOR_REQUEST_1E) = 2;
        Field<uint16_t>(r, ODST_ACTOR_REQUEST_22) = 1;
        Field<uint32_t>(r, ODST_ACTOR_REQUEST_CHARACTER) = character;
        PickLoadout(scnr, character, &Field<uint32_t>(r, ODST_ACTOR_REQUEST_WEAPON),
                    &Field<uint32_t>(r, ODST_ACTOR_REQUEST_SECONDARY));
        uint32_t actor = Engine<ActorSpawnFn>(OFFSET_ODST_ACTOR_SPAWN)(-1, request);
        if (actor == 0xFFFFFFFF) return actor;
        Engine<ActorSetActiveFn>(OFFSET_ODST_ACTOR_SET_ACTIVE)(actor, true);
        if (team > 0) SetTeamBoth(g_lastState, actor, (int16_t)team);
        return actor;
    }

    void Follow(void** state, Blam::DataArray* objects, uint32_t actorDatum, uint32_t character, int team) {
        auto actors = (Blam::DataArray*)state[ODST_TLS_SLOT_ACTORS];
        uint16_t index = actorDatum & 0xFFFF;
        if (!actors || index >= actors->highWater) return;
        char* actor = actors->Entry(index);
        if (*(uint16_t*)actor != (actorDatum >> 16)) return;
        uint32_t unit = Field<uint32_t>(actor, ODST_ACTOR_UNIT);
        if (!Blam::ObjectFromDatum(objects, unit)) {
            LOG("ODST: spawned actor %08X has no unit; not followed", actorDatum);
            return;
        }
        SpawnedAi* slot = nullptr;  // a free slot, else the body dead longest
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF || !Blam::ObjectFromDatum(objects, ai.unit)) { slot = &ai; break; }
            if (ai.deadAt && (!slot || ai.deadAt < slot->deadAt)) slot = &ai;
        }
        if (slot) *slot = {unit, character, team, 0};
    }

    uint32_t NewObject(uint32_t tag, uint32_t variant, const float* position, float yaw) {
        static uint8_t placement[ODST_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_ODST_OBJECT_PLACEMENT_NEW)(placement, tag, 0xFFFFFFFF, 0);
        if (variant) Field<uint32_t>((char*)placement, ODST_PLACEMENT_VARIANT) = variant;
        memcpy(placement + ODST_PLACEMENT_POSITION, position, 3 * sizeof(float));
        float* forward = (float*)(placement + ODST_PLACEMENT_FORWARD);
        forward[0] = cosf(yaw);
        forward[1] = sinf(yaw);
        forward[2] = 0.0f;
        return Engine<ObjectNewFn>(OFFSET_ODST_OBJECT_NEW)(placement);
    }

    // Makes a selection at a position: an AI (followed) or an object.
    uint32_t Create(void** state, Blam::DataArray* objects, char* scnr, uint32_t selection, int team,
                    const float* position, float yaw) {
        VehicleChoice c = ChoiceFor(selection);
        if (OdstTags::GroupOf(c.vehicle) != ODST_TAG_CHAR) return NewObject(c.vehicle, c.variant, position, yaw);
        uint32_t actor = SpawnActor(scnr, c.vehicle, position, yaw, ResolveSpawnTeam(state, objects, team));
        if (actor != 0xFFFFFFFF) Follow(state, objects, actor, c.vehicle, team);
        return actor;
    }

    void Spawn(void** state, const Players& p, Blam::DataArray* objects, uint32_t selection, int team) {
        uint32_t tag = ChoiceFor(selection).vehicle;
        char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
        if (p.count == 0) { SpawnResult("No player to spawn for"); return; }
        if (!scnr || !OdstTags::Get(tag)) { SpawnResult("Selection isn't in this map"); return; }
        g_wait = {};  // replaced by this newer request

        char* control = (char*)state[ODST_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        if (!control) { SpawnResult("No player to spawn for"); return; }
        float yaw = Field<float>(control, ODST_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, ODST_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_ODST_UNIT_HEAD_POSITION)(p.datums[0], eye);
        float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};

        alignas(16) static uint8_t result[ODST_COLLISION_RESULT_SIZE];
        memset(result, 0, sizeof(result));
        uint64_t flags = *(uint64_t*)(g_module + OFFSET_ODST_COLLISION_FLAGS);
        uint32_t vehicle = Field<uint32_t>(p.units[0], ODST_OBJECT_PARENT);
        bool hit = Engine<CollisionTestFn>(OFFSET_ODST_COLLISION_TEST_VECTOR)(flags, false, eye, ray, p.datums[0],
                                                                              vehicle, -1, result);
        float fraction = Field<float>((char*)result, ODST_COLLISION_RESULT_FRACTION);
        if (!hit || !(fraction >= 0.0f && fraction <= 1.0f)) { SpawnResult("Nothing in range to spawn on"); return; }
        float at[3] = {eye[0] + ray[0] * fraction, eye[1] + ray[1] * fraction, eye[2] + ray[2] * fraction};
        float position[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                             at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        float facing = yaw;
        uint32_t group = OdstTags::GroupOf(tag);
        if (group == ODST_TAG_VEHI || group == ODST_TAG_SCEN || group == ODST_TAG_MACH ||
            group == ODST_TAG_CTRL || group == ODST_TAG_BLOC) {
            // Backed off by its size so it isn't in a wall, and kept off the floor.
            float radius = Field<float>(OdstTags::Get(tag), ODST_OBJE_BOUNDING_RADIUS);
            if (!(radius > 0.0f && radius < 100.0f)) radius = 2.0f;
            for (int k = 0; k < 3; ++k) position[k] = at[k] - dir[k] * radius;
            position[2] = (position[2] > at[2] ? position[2] : at[2]) + kVehicleLift;
        } else if (group == ODST_TAG_CHAR) {
            facing = remainderf(yaw + kPi, 2.0f * kPi);  // AI face the player
        }

        uint32_t made = Create(state, objects, scnr, selection, team, position, facing);
        LOG("ODST: spawn %08X at %.2f %.2f %.2f (hit %.2f %.2f %.2f, fraction %.3f) -> object/actor %08X", tag,
            position[0], position[1], position[2], at[0], at[1], at[2], fraction, made);
        std::string name = SelectionName(selection);
        char text[96];
        uint32_t zone = made == 0xFFFFFFFF ? ZoneToLoad(scnr, tag) : 0;
        if (zone) {
            LoadZones(zone);
            g_wait.active = true;
            g_wait.selection = selection;
            g_wait.zone = zone;
            g_wait.deadline = GetTickCount64() + kZoneLoadMs;
            g_wait.team = team;
            memcpy(g_wait.position, position, sizeof(position));
            g_wait.yaw = facing;
            sprintf_s(text, "Loading %s...", name.c_str());
        } else if (made == 0xFFFFFFFF) {
            sprintf_s(text, "%s: not loaded in this area", name.c_str());
        } else {
            sprintf_s(text, "Spawned %s", name.c_str());
        }
        SpawnResult(text);
    }

    // Retries a spawn each frame once its zone is loaded (its resources may
    // take a few more frames), until it works or times out.
    void ContinueZoneWait(void** state, Blam::DataArray* objects, char* scnr) {
        if (!g_wait.active) return;
        ULONGLONG now = GetTickCount64();
        char text[96];
        if (ActiveZones() & g_wait.zone) {
            uint32_t made = Create(state, objects, scnr, g_wait.selection, g_wait.team, g_wait.position, g_wait.yaw);
            if (made != 0xFFFFFFFF) {
                LOG("ODST: spawn %08X: made after its zone loaded -> %08X", g_wait.selection, made);
                sprintf_s(text, "Spawned %s", SelectionName(g_wait.selection).c_str());
                SpawnResult(text);
                g_wait = {};
                return;
            }
        }
        if (now > g_wait.deadline) {
            sprintf_s(text, "Couldn't spawn %s here", SelectionName(g_wait.selection).c_str());
            SpawnResult(text);
            g_wait = {};
        }
    }

    // Spawned AI after death. ODST doesn't revive them, so "Enable
    // respawn" is the mod's own, read once per death: on, an AI dead for
    // kRespawnMs is replaced by a fresh one of the same character and team
    // where it lay (the body is removed); off, it stays a body.
    void FollowSpawnedAi(void** state, const Players& p, Blam::DataArray* objects, char* scnr, bool respawn) {
        ULONGLONG now = GetTickCount64();
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            char* unit = Blam::ObjectFromDatum(objects, ai.unit);
            if (!unit) { ai = {}; continue; }
            if (Field<float>(unit, ODST_OBJECT_HEALTH) > 0.0f) { ai.deadAt = 0; continue; }
            if (!ai.deadAt) {
                ai.deadAt = now;
                LOG("ODST: spawned AI unit %08X died", ai.unit);
                continue;
            }
            if (now - ai.deadAt < kRespawnMs) continue;
            SpawnedAi old = ai;
            ai = {};
            if (!respawn || !OdstTags::Get(old.character)) {
                LOG("ODST: spawned AI unit %08X left as a body (respawn off)", old.unit);
                continue;
            }
            float position[3];
            memcpy(position, &Field<float>(unit, ODST_OBJECT_POSITION), sizeof(position));
            position[2] += kSpawnLift;
            float yaw = 0.0f;
            if (p.count > 0) {
                const float* to = &Field<float>(p.units[0], ODST_OBJECT_POSITION);
                yaw = atan2f(to[1] - position[1], to[0] - position[0]);
            }
            Engine<ObjectDeleteFn>(OFFSET_ODST_OBJECT_DELETE)(old.unit);
            uint32_t actor = SpawnActor(scnr, old.character, position, yaw,
                                        ResolveSpawnTeam(state, objects, old.team));  // old.team = the picker's choice
            if (actor != 0xFFFFFFFF) Follow(state, objects, actor, old.character, old.team);
            LOG("ODST: spawned AI respawned: old unit %08X -> actor %08X", old.unit, actor);
        }
    }
}

namespace {
    // The engine's ray from `from` along `vec`, ignoring player 1 and their
    // vehicle if asked. Writes the hit point.
    bool CastRay(const Players& p, const float* from, const float* vec, bool ignorePlayer, float* hit) {
        alignas(16) static uint8_t result[ODST_COLLISION_RESULT_SIZE];
        memset(result, 0, sizeof(result));
        uint64_t flags = *(uint64_t*)(g_module + OFFSET_ODST_COLLISION_FLAGS);
        uint32_t ignore = 0xFFFFFFFF, ignore2 = 0xFFFFFFFF;
        if (ignorePlayer && p.count > 0) {
            ignore = p.datums[0];
            ignore2 = Field<uint32_t>(p.units[0], ODST_OBJECT_PARENT);
        }
        if (!Engine<CollisionTestFn>(OFFSET_ODST_COLLISION_TEST_VECTOR)(flags, false, from, vec, ignore, ignore2, -1,
                                                                        result))
            return false;
        float fraction = Field<float>((char*)result, ODST_COLLISION_RESULT_FRACTION);
        if (!(fraction >= 0.0f && fraction <= 1.0f)) return false;
        for (int k = 0; k < 3; ++k) hit[k] = from[k] + vec[k] * fraction;
        return true;
    }

    char* ObjectOfType(Blam::DataArray* objects, uint32_t datum, int type) {
        char* object = Blam::ObjectFromDatum(objects, datum);
        return object && Field<uint8_t>(object, ODST_OBJECT_TYPE) == type ? object : nullptr;
    }

    // The engine work behind the Fun, Chaos and Skulls pages (games/fun.h).
    class FunTitle : public Fun::Title {
    public:
        FunTitle(void** state, const Players& p, Blam::DataArray* objects) : m_state(state), m_p(p), m_objects(objects) {}
        const char* Name() override { return "ODST"; }
        uint32_t Player() override { return m_p.count > 0 ? m_p.datums[0] : 0xFFFFFFFF; }
        uint32_t PlayerVehicle() override {
            if (m_p.count == 0) return 0xFFFFFFFF;
            uint32_t top = 0xFFFFFFFF, at = m_p.datums[0];
            for (int depth = 0; depth < 5; ++depth) {
                char* object = Blam::ObjectFromDatum(m_objects, at);
                uint32_t parent = object ? Field<uint32_t>(object, ODST_OBJECT_PARENT) : 0xFFFFFFFF;
                if (!ObjectOfType(m_objects, parent, ODST_OBJECT_TYPE_VEHICLE)) break;
                top = at = parent;
            }
            return top;
        }
        bool View(float* eye, float* dir) override {
            char* control = (char*)m_state[ODST_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
            if (!control || m_p.count == 0) return false;
            float yaw = Field<float>(control, ODST_PLAYER_CONTROL_YAW);
            float pitch = Field<float>(control, ODST_PLAYER_CONTROL_PITCH);
            dir[0] = cosf(yaw) * cosf(pitch);
            dir[1] = sinf(yaw) * cosf(pitch);
            dir[2] = sinf(pitch);
            Engine<HeadPositionFn>(OFFSET_ODST_UNIT_HEAD_POSITION)(m_p.datums[0], eye);
            return true;
        }
        bool Ray(const float* from, const float* vec, bool ignorePlayer, float* hit) override {
            return CastRay(m_p, from, vec, ignorePlayer, hit);
        }
        bool Position(uint32_t object, float* out) override {
            char* o = Blam::ObjectFromDatum(m_objects, object);
            if (o) memcpy(out, &Field<float>(o, ODST_OBJECT_POSITION), 3 * sizeof(float));
            return o != nullptr;
        }
        float Radius(uint32_t object) override {
            char* o = Blam::ObjectFromDatum(m_objects, object);
            char* tag = o ? OdstTags::Get(Field<uint32_t>(o, ODST_OBJECT_TAG_DATUM)) : nullptr;
            return tag ? Field<float>(tag, ODST_OBJE_BOUNDING_RADIUS) : 0.0f;
        }
        void Teleport(uint32_t object, const float* position) override {
            // A 64-bit null location: a 32-bit 0 left garbage in the high half (see noclip).
            Engine<SetPositionFn>(OFFSET_ODST_OBJECT_SET_POSITION)(object, position, nullptr, nullptr, nullptr, false);
            const float still[3] = {};
            Engine<SetVelocityFn>(OFFSET_ODST_OBJECT_SET_VELOCITY)(object, still, still);
            if (m_p.count > 0 && object == m_p.datums[0]) OdstCheats::MoveNoclipAnchor(position);
        }
        void AddVelocity(uint32_t object, const float* add) override {
            char* o = Blam::ObjectFromDatum(m_objects, object);
            if (!o) return;
            const float* now = &Field<float>(o, ODST_OBJECT_VELOCITY);
            float velocity[3] = {now[0] + add[0], now[1] + add[1], now[2] + add[2]};
            Engine<SetVelocityFn>(OFFSET_ODST_OBJECT_SET_VELOCITY)(object, velocity, nullptr);
        }
        uint32_t Create(uint32_t selection, int team, const float* position, float yaw) override {
            char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
            if (!scnr) return 0xFFFFFFFF;
            uint32_t made = ::Create(m_state, m_objects, scnr, selection, team, position, yaw);
            if (made == 0xFFFFFFFF || OdstTags::GroupOf(ChoiceFor(selection).vehicle) != ODST_TAG_CHAR) return made;
            auto actors = (Blam::DataArray*)m_state[ODST_TLS_SLOT_ACTORS];  // an actor: its unit
            uint16_t index = made & 0xFFFF;
            if (!actors || index >= actors->highWater) return 0xFFFFFFFF;
            char* actor = actors->Entry(index);
            return *(uint16_t*)actor == (made >> 16) ? Field<uint32_t>(actor, ODST_ACTOR_UNIT) : 0xFFFFFFFF;
        }
        void PrepareFor(uint32_t selection) override {
            char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
            if (auto zone = scnr ? ZoneToLoad(scnr, ChoiceFor(selection).vehicle) : 0) LoadZones(zone);
        }
        bool InUseByPlayer(uint32_t object) override {
            for (int i = 0; i < m_p.count; ++i) {
                if (Field<uint32_t>(m_p.units[i], ODST_OBJECT_PARENT) == object) return true;
                for (int w = 0; w < ODST_UNIT_WEAPON_COUNT; ++w)
                    if (Field<uint32_t>(m_p.units[i], ODST_UNIT_WEAPONS + w * 4) == object) return true;
            }
            return false;
        }
        void Delete(uint32_t object) override { Engine<ObjectDeleteFn>(OFFSET_ODST_OBJECT_DELETE)(object); }
        void Message(const char* text) override { SpawnResult(text); }
        bool ReadSkulls(uint64_t& mask) override {
            char* globals = (char*)m_state[ODST_TLS_SLOT_GAME_GLOBALS];
            if (!globals) return false;
            mask = MenuSkulls(globals);
            return true;
        }
        void WriteSkulls(uint64_t mask) override {
            SetMenuSkulls((char*)m_state[ODST_TLS_SLOT_GAME_GLOBALS], mask);  // read live; no refresh call
        }
        uint64_t SkullMask() override { return (1ull << kMenuSkullCount) - 1; }

    private:
        void** m_state;
        const Players& m_p;
        Blam::DataArray* m_objects;
    };
}

void OdstSpawn::FunTick(void** state, const Players& p, Blam::DataArray* objects, const Games::Settings& s,
                         float dt) {
    if (!g_module) return;
    FunTitle title(state, p, objects);
    Fun::Tick(title, s, dt);
}

void OdstSpawn::SetModule(HMODULE odst) { g_module = (char*)odst; }

void OdstSpawn::Reset() {
    Fun::Reset();
    for (auto& ai : g_spawnedAi) ai = {};
    g_wait = {};
    g_forcedZones = 0;
    g_forcedScenario = nullptr;
    g_characters.clear();
    g_vehicles.clear();
    g_objects.clear();
    std::lock_guard<std::mutex> lock(g_listsLock);
    g_vehicleChoices.clear();
}

void OdstSpawn::RefreshLists(char* scnr) {
    g_characters.clear();
    for (auto& character : OdstTags::ListGroup(ODST_TAG_CHAR))
        if (Spawnable(character.datum)) g_characters.push_back(character);
    Guard("ODST vehicle list", [&] { BuildVehicleList(scnr); });
    Guard("ODST object list", [&] { BuildObjectList(scnr); });
    LOG("ODST: spawn lists: %zu characters, %zu vehicles, %zu objects", g_characters.size(), g_vehicles.size(),
        g_objects.size());
}

void OdstSpawn::FillStatus(Games::Status& status) {
    status.spawnAi = &g_characters;
    status.spawnVehicles = &g_vehicles;
    status.spawnObjects = &g_objects;
    std::lock_guard<std::mutex> lock(g_resultLock);
    status.spawnDone = g_spawnDone;
    sprintf_s(status.spawnMessage, "%s", g_spawnMessage);
}

void OdstSpawn::Tick(void** state, const Players& p, Blam::DataArray* objects, const Games::Settings& s) {
    g_lastState = state;
    if (!g_module) return;
    if (!g_haveSerial) {  // don't run a key press from before this session
        g_spawnSerial = s.spawnSerial;
        g_haveSerial = true;
    }
    char* scnr = nullptr;
    Guard("ODST spawn zones", [&] {
        scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
        KeepForcedZones(scnr);
    });
    if (!scnr) return;
    if (s.spawnSerial != g_spawnSerial) {
        g_spawnSerial = s.spawnSerial;
        if (s.spawnEnabled && s.spawnSquad >= 0)
            Guard("ODST squad", [&] { OdstUpgrade::SpawnSquad(state, p, objects, s.spawnSquad, s.spawnTeam); });
        else if (s.spawnEnabled && s.spawnTag != 0xFFFFFFFF)
            Guard("ODST spawn", [&] { Spawn(state, p, objects, s.spawnTag, s.spawnTeam); });
    }
    Guard("ODST spawn zone wait", [&] { ContinueZoneWait(state, objects, scnr); });
    Guard("ODST spawned AI", [&] { FollowSpawnedAi(state, p, objects, scnr, s.spawnRespawn); });
}

// --- Shared with the feature files (internal.h) ---------------------------------

char* OdstInternal::Module() { return g_module; }

uint32_t OdstInternal::SpawnActor(void** state, const float* position, float yaw, uint32_t character, int team) {
    char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
    if (!g_module || !scnr || !OdstTags::Get(character)) return 0xFFFFFFFF;
    g_lastState = state;
    // `team` is the picker choice (internal.h): resolve it per AI so Rogue/Random roll for each
    // member and Covenant means the mission's hostile Covenant team.
    auto objects = state ? (Blam::DataArray*)state[ODST_TLS_SLOT_OBJECTS] : nullptr;
    return ::SpawnActor(scnr, character, position, yaw, ::ResolveSpawnTeam(state, objects, team));
}

int OdstInternal::ResolveSpawnTeam(void** state, int choice) {
    auto objects = state ? (Blam::DataArray*)state[ODST_TLS_SLOT_OBJECTS] : nullptr;
    return ::ResolveSpawnTeam(state, objects, choice);
}

void OdstInternal::SetActorTeam(uint32_t actor, int16_t team) { SetTeamBoth(g_lastState, actor, team); }

void OdstInternal::Follow(void** state, Blam::DataArray* objects, uint32_t actor, uint32_t character, int team) {
    ::Follow(state, objects, actor, character, team);
}

void OdstInternal::Refollow(void** state, Blam::DataArray* objects, uint32_t oldUnit, uint32_t newActor,
                            uint32_t newCharacter) {
    for (auto& ai : g_spawnedAi) {
        if (ai.unit != oldUnit) continue;
        int team = ai.team;
        ai = {};
        ::Follow(state, objects, newActor, newCharacter, team);
        return;
    }
}

int OdstInternal::FollowedUnits(uint32_t* out, int max) {
    int n = 0;
    for (auto& ai : g_spawnedAi)
        if (ai.unit != 0xFFFFFFFF && n < max) out[n++] = ai.unit;
    return n;
}

void OdstInternal::ForgetFollowed() {
    for (auto& ai : g_spawnedAi) ai = {};
    g_wait = {};
}

void OdstInternal::Toast(const char* text) { SpawnResult(text); }

void OdstInternal::DeleteObject(uint32_t object) { Engine<ObjectDeleteFn>(OFFSET_ODST_OBJECT_DELETE)(object); }

bool OdstInternal::TeamsFriends(void** state, int a, int b) {
    if (a == b) return true;
    if (a < 0 || b < 0 || a >= ODST_TEAM_COUNT || b >= ODST_TEAM_COUNT) return false;
    char* allegiance = state ? (char*)state[ODST_TLS_SLOT_ALLEGIANCE] : nullptr;
    if (!allegiance) return false;
    return (Field<uint32_t>(allegiance, ODST_ALLEGIANCE_FRIENDS + a * 4) >> b) & 1;
}

bool OdstInternal::AimPoint(void** state, const Players& p, float* at, float* dir, float* facing) {
    if (!g_module || p.count == 0) return false;
    char* control = (char*)state[ODST_TLS_SLOT_PLAYER_CONTROL];
    if (!control) return false;
    float yaw = Field<float>(control, ODST_PLAYER_CONTROL_YAW);
    float pitch = Field<float>(control, ODST_PLAYER_CONTROL_PITCH);
    dir[0] = cosf(yaw) * cosf(pitch);
    dir[1] = sinf(yaw) * cosf(pitch);
    dir[2] = sinf(pitch);
    float eye[3];
    Engine<HeadPositionFn>(OFFSET_ODST_UNIT_HEAD_POSITION)(p.datums[0], eye);
    float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};
    if (!CastRay(p, eye, ray, true, at)) return false;
    *facing = remainderf(yaw + kPi, 2.0f * kPi);
    if (*facing < 0.0f) *facing += 2.0f * kPi;
    return true;
}

const std::vector<Games::TagChoice>& OdstInternal::Characters() { return g_characters; }

bool OdstInternal::LoadZoneFor(uint32_t tag) {
    char* scnr = OdstTags::GetGlobal(ODST_TAG_SCNR);
    uint32_t zone = scnr ? ZoneToLoad(scnr, tag) : 0;
    if (zone) LoadZones(zone);
    return zone != 0;
}
