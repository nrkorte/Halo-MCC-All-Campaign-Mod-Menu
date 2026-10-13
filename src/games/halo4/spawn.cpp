// Halo 4 spawning: a port of Halo 3's (halo3/spawn.cpp), itself ported from
// Reach's (see NOTES.md). A ray from player 1's head along their view finds
// the spot (Halo 4's ray test takes a query struct); AI are squadless actors
// made from a request, as in Reach, and arm themselves with their
// character's weapons. Tags only loaded in some parts of a mission get their
// designer zone loaded first (64-bit masks in Halo 4).

#include "spawn.h"

#include "cheats.h"
#include "internal.h"
#include "spawn_offsets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "games/fun.h"
#include "tags.h"

using namespace Halo4Cheats;

namespace {
    constexpr float kPi = 3.14159265f;
    constexpr float kSpawnRange = 300.0f;  // world units the spawn ray reaches
    constexpr float kSpawnBackOff = 0.3f;  // back along the ray from the hit, so it isn't inside the surface
    constexpr float kSpawnLift = 0.25f;    // and up, so it doesn't clip into the ground
    constexpr float kVehicleLift = 0.5f;   // vehicles and scenery: above the higher of the hit and the backed-off point
    constexpr int kMaxSpawnedAi = 512;  // big squads are all tracked
    constexpr ULONGLONG kRespawnMs = 5000;  // dead this long: respawned, or left as a body
    constexpr ULONGLONG kZoneLoadMs = 5000;  // how long a spawn waits for its designer zone

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using HeadPositionFn = void(__fastcall*)(uint32_t unit, float* out);
    using CollisionTestFn = bool(__fastcall*)(void* query, void* result);
    using ActorRequestInitFn = void(__fastcall*)(void* request);
    using ActorSpawnFn = uint32_t(__fastcall*)(int16_t spawnPoint, void* request);
    using ActorSetActiveFn = void(__fastcall*)(uint32_t actor, bool active);
    using ActorSetTeamFn = void(__fastcall*)(uint32_t actor, int32_t team);
    using SetPositionFn = void(__fastcall*)(uint32_t object, const float* position, const float* forward,
                                            const float* up, int64_t, bool);
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
        char* entry = Halo4Tags::BlockElement(scnr, palette, HALO4_PALETTE_ENTRY_SIZE, index);
        return entry ? Field<uint32_t>(entry, HALO4_PALETTE_ENTRY_DATUM) : 0xFFFFFFFF;
    }

    bool InPalette(char* scnr, int palette, uint32_t tag) {
        int count = Halo4Tags::BlockCount(scnr, palette);
        for (int i = 0; i < count && i < 1024; ++i)
            if (PaletteDatum(scnr, palette, i) == tag) return true;
        return false;
    }

    // A character with a unit somewhere up its parent chain (templates such as
    // "generic" and "_aggressive" presets have none and can't be spawned).
    bool Spawnable(uint32_t character) {
        __try {
            for (int depth = 0; depth < 8 && character != 0xFFFFFFFF; ++depth) {
                char* tag = Halo4Tags::Get(character);
                if (!tag) return false;
                if (Field<uint32_t>(tag, HALO4_CHAR_UNIT) != 0xFFFFFFFF) return true;
                character = Field<uint32_t>(tag, HALO4_CHAR_PARENT);
            }
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Vehicles: the mission's vehicle palette (other 'vehi' tags are turrets
    // and parts), each followed by its model's other variants ("phantom
    // (no_chin_gun)"). Damaged and cinematic ones are left out.
    void BuildVehicleList(char* scnr) {
        constexpr const char* kSkip[] = {"default", "damaged", "wrecked", "cinematic", "prop", "filler"};
        std::vector<Games::TagChoice> list;
        std::vector<VehicleChoice> choices;
        for (auto& vehicle : Halo4Tags::ListGroup(HALO4_TAG_VEHI)) {
            if (!InPalette(scnr, HALO4_SCNR_VEHICLE_PALETTE, vehicle.datum)) continue;
            list.push_back(vehicle);
            char* tag = Halo4Tags::Get(vehicle.datum);
            char* model = tag ? Halo4Tags::Get(Field<uint32_t>(tag, HALO4_OBJE_MODEL + HALO4_TAGREF_DATUM)) : nullptr;
            uint32_t defaultName = tag ? Field<uint32_t>(tag, HALO4_OBJE_DEFAULT_VARIANT) : 0;
            int count = Halo4Tags::BlockCount(model, HALO4_HLMT_VARIANTS);
            for (int v = 0; v < count && v < 64; ++v) {
                char* variant = Halo4Tags::BlockElement(model, HALO4_HLMT_VARIANTS, HALO4_HLMT_VARIANT_SIZE, v);
                uint32_t name = variant ? Field<uint32_t>(variant, 0) : 0;
                std::string label = Halo4Tags::StringName(name);
                bool skip = !name || name == defaultName || label.empty() || count == 1;
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
        constexpr Kind kKinds[] = {{HALO4_SCNR_CRATE_PALETTE, "crate"},
                                   {HALO4_SCNR_EQUIPMENT_PALETTE, "equipment"},
                                   {HALO4_SCNR_MACHINE_PALETTE, "machine"},
                                   {HALO4_SCNR_CONTROL_PALETTE, "control"},
                                   {HALO4_SCNR_SCENERY_PALETTE, "scenery"}};
        constexpr const char* kHelpers[] = {"invisible", "pathfinding", "ui_object", "fx_", "standin", "kill_",
                                            "marker", "skybox"};
        g_objects.clear();
        for (const Kind& kind : kKinds) {
            std::vector<Games::TagChoice> found;
            int count = Halo4Tags::BlockCount(scnr, kind.palette);
            for (int i = 0; i < count && i < 1024; ++i) {
                uint32_t tag = PaletteDatum(scnr, kind.palette, i);
                if (tag == 0xFFFFFFFF || !Halo4Tags::Get(tag)) continue;
                std::string name = Halo4Tags::FullName(tag);
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
        LOG("Halo 4: spawn: %s", text);
    }

    std::string ShortName(uint32_t tag) {
        std::string name = Halo4Tags::FullName(tag);
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

    // --- Designer zones (as in Reach, 64-bit) -------------------------------------
    // A tag the mission only loads in some parts of the level is in one or more
    // designer zones. When a spawn fails and its zone isn't loaded, the mod
    // loads it (pending masks + flag, which the game's zone update applies)
    // and retries each tick. Zones it loaded are loaded again if the active
    // mask loses them (a zone set switch), for the rest of the map.

    uint64_t g_forcedZones = 0;
    char* g_forcedScenario = nullptr;

    uint64_t& ActiveZones() { return *(uint64_t*)(g_module + OFFSET_HALO4_DESIGNER_ZONES_ACTIVE); }

    int PaletteIndex(char* scnr, int palette, uint32_t tag) {
        int count = Halo4Tags::BlockCount(scnr, palette);
        for (int i = 0; i < count && i < 1024; ++i)
            if (PaletteDatum(scnr, palette, i) == tag) return i;
        return -1;
    }

    uint64_t DesignerZonesOf(char* scnr, uint32_t tag) {
        int list = 0, palette = 0;
        switch (Halo4Tags::GroupOf(tag)) {
            case HALO4_TAG_CHAR: list = HALO4_DESIGNER_ZONE_CHARACTERS; palette = HALO4_SCNR_CHARACTER_PALETTE; break;
            case HALO4_TAG_WEAP: list = HALO4_DESIGNER_ZONE_WEAPONS; palette = HALO4_SCNR_WEAPON_PALETTE; break;
            case HALO4_TAG_VEHI: list = HALO4_DESIGNER_ZONE_VEHICLES; palette = HALO4_SCNR_VEHICLE_PALETTE; break;
            case HALO4_TAG_BIPD: list = HALO4_DESIGNER_ZONE_BIPEDS; palette = HALO4_SCNR_BIPED_PALETTE; break;
            case HALO4_TAG_EQIP: list = HALO4_DESIGNER_ZONE_EQUIPMENT; palette = HALO4_SCNR_EQUIPMENT_PALETTE; break;
            case HALO4_TAG_SCEN: list = HALO4_DESIGNER_ZONE_SCENERY; palette = HALO4_SCNR_SCENERY_PALETTE; break;
            case HALO4_TAG_MACH: list = HALO4_DESIGNER_ZONE_MACHINES; palette = HALO4_SCNR_MACHINE_PALETTE; break;
            case HALO4_TAG_CTRL: list = HALO4_DESIGNER_ZONE_CONTROLS; palette = HALO4_SCNR_CONTROL_PALETTE; break;
            case HALO4_TAG_BLOC: list = HALO4_DESIGNER_ZONE_CRATES; palette = HALO4_SCNR_CRATE_PALETTE; break;
            default: break;
        }
        int index = palette ? PaletteIndex(scnr, palette, tag) : -1;
        uint64_t zones = 0;
        int count = Halo4Tags::BlockCount(scnr, HALO4_SCNR_DESIGNER_ZONES);
        for (int z = 0; z < count && z < 64; ++z) {
            char* zone = Halo4Tags::BlockElement(scnr, HALO4_SCNR_DESIGNER_ZONES, HALO4_SCNR_DESIGNER_ZONE_SIZE, z);
            if (!zone) continue;
            bool in = false;
            int tags = Halo4Tags::BlockCount(zone, HALO4_DESIGNER_ZONE_TAGS);  // listed directly
            for (int e = 0; e < tags && e < 1024 && !in; ++e) {
                char* entry = Halo4Tags::BlockElement(zone, HALO4_DESIGNER_ZONE_TAGS, HALO4_DESIGNER_ZONE_TAG_SIZE, e);
                in = entry && Field<uint32_t>(entry, HALO4_PALETTE_ENTRY_DATUM) == tag;
            }
            int entries = index >= 0 ? Halo4Tags::BlockCount(zone, list) : 0;  // by palette index
            for (int e = 0; e < entries && e < 256 && !in; ++e) {
                char* entry = Halo4Tags::BlockElement(zone, list, 2, e);
                in = entry && Field<int16_t>(entry, 0) == index;
            }
            if (in) zones |= 1ull << z;
        }
        return zones;
    }

    // The zone to load for a tag that failed to spawn, or 0 if it's in no
    // designer zone or one is already loaded.
    uint64_t ZoneToLoad(char* scnr, uint32_t tag) {
        uint64_t zones = DesignerZonesOf(scnr, tag);
        if (!zones || (zones & ActiveZones())) return 0;
        return zones & (0ull - zones);  // the first one is enough
    }

    void LoadZones(uint64_t zones) {
        g_forcedZones |= zones;
        uint64_t missing = zones & ~ActiveZones();
        if (!missing) return;
        uint64_t* pending = (uint64_t*)(g_module + OFFSET_HALO4_DESIGNER_ZONES_PENDING);
        pending[0] &= ~missing;  // deactivate
        pending[1] |= missing;   // activate
        *(uint8_t*)(g_module + OFFSET_HALO4_ZONE_UPDATE_PENDING) = 1;
        LOG("Halo 4: loading designer zones %016llX (active %016llX)", missing, ActiveZones());
    }

    void KeepForcedZones(char* scnr) {
        if (scnr != g_forcedScenario) {
            g_forcedScenario = scnr;
            g_forcedZones = 0;
        }
        if (!g_forcedZones || !scnr) return;
        uint64_t* pending = (uint64_t*)(g_module + OFFSET_HALO4_DESIGNER_ZONES_PENDING);
        if ((g_forcedZones & ~ActiveZones()) && !(pending[1] & g_forcedZones)) {
            LOG("Halo 4: designer zones %016llX were unloaded; loading them again", g_forcedZones & ~ActiveZones());
            LoadZones(g_forcedZones);
        }
    }

    // A spawn waiting for its designer zone to load.
    struct ZoneWait {
        bool active = false;
        uint32_t selection = 0xFFFFFFFF;
        uint64_t zone = 0;
        ULONGLONG deadline = 0;
        int team = 0;
        float position[3] = {};
        float yaw = 0.0f;
    };
    ZoneWait g_wait;

    // --- Spawning ------------------------------------------------------------------

    // AI the mod spawned, followed after death for "Enable respawn".
    struct SpawnedAi {
        uint32_t unit = 0xFFFFFFFF;
        uint32_t character = 0xFFFFFFFF;
        int team = 0;       // the spawn picker's choice it was made with (a respawn picks again)
        int resolved = -1;  // the engine team it was given, -1 = its own
        ULONGLONG deadAt = 0;  // when it was first seen dead, 0 = alive
    };
    SpawnedAi g_spawnedAi[kMaxSpawnedAi];
    uint32_t g_spawnSerial = 0;
    bool g_haveSerial = false;

    // --- Team picker (Settings::spawnTeam) ------------------------------------
    // 0 their own team, 1 Ally, 2 Covenant (the enemy), 3 Flood (not in
    // Halo 4: own team), 4 Rogue, 5 Random. Teams: see offsets.h. A team is
    // always friends with itself (the hostility test at 0x207AFC); two teams
    // are friends only through the allegiance masks. Reach's table has only
    // player/human allied; Halo 4's table is unread (static only), so
    // everything below reads the masks instead of assuming them.
    uint32_t g_random = 0x2545F491u;  // xorshift, seeded in Halo4Spawn::Tick
    float Random01() {
        g_random ^= g_random << 13;
        g_random ^= g_random >> 17;
        g_random ^= g_random << 5;
        return (g_random >> 8) / 16777216.0f;
    }

    // Teams used for Rogue: ones whose allegiance row is empty (they list
    // nobody, nobody lists them), so they are enemies of everyone. A team's
    // AI are always friends with each other, so rogues rotate over several
    // teams, each time the one the fewest AI the mod still follows are on.
    constexpr int kRogueTeams[] = {HALO4_TEAM_BRUTE, HALO4_TEAM_MULE, HALO4_TEAM_SPARE, HALO4_TEAM_COVENANT_PLAYER};
    uint32_t g_rogueMask = 0;  // rogue teams handed out this map: kept hostile each tick

    uint32_t* AllegianceMasks(void** state, int offset) {
        char* table = state ? (char*)state[HALO4_TLS_SLOT_ALLEGIANCE] : nullptr;
        return table ? &Field<uint32_t>(table, offset) : nullptr;
    }

    // True if nobody is allied with the team (its own bit aside) in either mask set.
    bool TeamIsFree(void** state, int team) {
        for (int offset : {HALO4_ALLEGIANCE_MASKS, HALO4_ALLEGIANCE_FRIENDS}) {
            uint32_t* masks = AllegianceMasks(state, offset);
            if (!masks) return true;
            if (masks[team] & ~(1u << team)) return false;
            for (int t = 0; t < HALO4_TEAM_COUNT; ++t)
                if (t != team && (masks[t] & (1u << team))) return false;
        }
        return true;
    }

    // Rogue teams list nobody and nobody lists them, written again every tick
    // in case a mission script allies them.
    void MakeRoguesHostile(void** state) {
        for (int offset : {HALO4_ALLEGIANCE_MASKS, HALO4_ALLEGIANCE_FRIENDS}) {
            uint32_t* masks = AllegianceMasks(state, offset);
            if (!masks) continue;
            for (int r : kRogueTeams) {
                if (!(g_rogueMask & (1u << r))) continue;
                masks[r] = 0;
                for (int t = 0; t < HALO4_TEAM_COUNT; ++t) masks[t] &= ~(1u << r);
            }
        }
    }

    int PlayerTeamOf(void** state) {
        Array players{(char*)state[HALO4_TLS_SLOT_PLAYERS]};
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        if (!players.Valid() || !objects.Valid()) return HALO4_TEAM_PLAYER;
        Players p = FindPlayers(players, objects);
        return p.count > 0 ? Field<int8_t>(p.units[0], HALO4_UNIT_TEAM) : HALO4_TEAM_PLAYER;
    }

    // The rogue team the fewest living followed AI are on (free ones first).
    int PickRogueTeam(void** state) {
        int best = HALO4_TEAM_SPARE, bestUse = 1 << 30;
        bool bestFree = false;
        for (int r : kRogueTeams) {
            bool free = TeamIsFree(state, r);
            int use = 0;
            for (auto& ai : g_spawnedAi)
                if (ai.unit != 0xFFFFFFFF && !ai.deadAt && ai.resolved == r) ++use;
            // Random tie-break so equal teams don't always go in table order.
            bool better = free != bestFree ? free : (use < bestUse || (use == bestUse && Random01() < 0.5f));
            if (better) { best = r; bestUse = use; bestFree = free; }
        }
        return best;
    }

    // The engine team for a picker choice, or -1 to leave the AI on its own.
    // Rogue and Random roll per call, so a respawn rolls again.
    int ResolveSpawnTeam(void** state, int choice) {
        int mine = PlayerTeamOf(state);
        switch (choice) {
        case 1: {  // Ally: human, or whatever else player 1's team is friends with
            uint32_t* masks = AllegianceMasks(state, HALO4_ALLEGIANCE_MASKS);
            if (!masks || mine == HALO4_TEAM_HUMAN || (masks[mine] & (1u << HALO4_TEAM_HUMAN))) return HALO4_TEAM_HUMAN;
            for (int t = 2; t < HALO4_TEAM_COUNT; ++t)
                if (t != mine && (masks[mine] & (1u << t))) return t;
            return HALO4_TEAM_HUMAN;
        }
        case 2: {  // the enemy: the Covenant, unless player 1's team is friends with them
            uint32_t* masks = AllegianceMasks(state, HALO4_ALLEGIANCE_MASKS);
            bool friendly = masks && mine != HALO4_TEAM_COVENANT && (masks[mine] & (1u << HALO4_TEAM_COVENANT));
            return friendly ? HALO4_TEAM_FORERUNNER : HALO4_TEAM_COVENANT;
        }
        case 4: break;
        case 5: {  // human, covenant, forerunner (Prometheans) or rogue
            constexpr int kRandom[] = {HALO4_TEAM_HUMAN, HALO4_TEAM_COVENANT, HALO4_TEAM_FORERUNNER, -2};
            int pick = kRandom[(int)(Random01() * _countof(kRandom)) % (int)_countof(kRandom)];
            if (pick != -2) return pick;
            break;
        }
        default: return -1;  // 0 their own, 3 Flood (not in Halo 4)
        }
        int team = PickRogueTeam(state);
        g_rogueMask |= 1u << team;
        MakeRoguesHostile(state);
        return team;
    }

    // actor_set_team (0x72E820) writes the actor's team always, but the unit's own
    // team (+0x498) only when actor flag byte +0x89 bit 1 is clear; a spawned AI
    // is activated first, so (as in Halo 3) the unit may keep its starting team
    // and others keep treating it as a friend. Write both (the unit check is
    // harmless if the engine already did it).
    void** g_lastState = nullptr;
    void SetTeamBoth(void** state, uint32_t actor, int16_t team) {
        Engine<ActorSetTeamFn>(OFFSET_HALO4_ACTOR_SET_TEAM)(actor, team);
        if (!state) return;
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        Array objects{(char*)state[HALO4_TLS_SLOT_OBJECTS]};
        if (!actors.Valid() || !objects.Valid()) return;
        char* a = actors.Get(actor);
        if (!a) return;
        if (char* unit = ObjectFromDatum(objects, Field<uint32_t>(a, HALO4_ACTOR_UNIT)))
            Field<int8_t>(unit, HALO4_UNIT_TEAM) = (int8_t)team;
    }

    // An AI as a squadless actor, from a request built like the game's own
    // squadless spawns (0x7C9BA4). Returns the actor or -1. `team` is the
    // picker's choice; the team it resolved to is written to `resolved`.
    uint32_t SpawnActor(void** state, uint32_t character, const float* position, float yaw, int team,
                        int* resolved = nullptr) {
        alignas(16) static uint8_t request[0x100];
        char* r = (char*)request;
        memset(request, 0, sizeof(request));
        Engine<ActorRequestInitFn>(OFFSET_HALO4_ACTOR_REQUEST_INIT)(request);
        memcpy(r + HALO4_ACTOR_REQUEST_POSITION, position, 3 * sizeof(float));
        Field<int32_t>(r, HALO4_ACTOR_REQUEST_C) = -1;
        Field<int32_t>(r, HALO4_ACTOR_REQUEST_C + 4) = -1;
        Field<float>(r, HALO4_ACTOR_REQUEST_YAW) = yaw;
        Field<float>(r, HALO4_ACTOR_REQUEST_PITCH) = 0.0f;
        Field<uint32_t>(r, HALO4_ACTOR_REQUEST_1C) = 0;
        Field<uint16_t>(r, HALO4_ACTOR_REQUEST_FLAGS) |= HALO4_ACTOR_REQUEST_FLAG_FREE;
        Field<uint16_t>(r, HALO4_ACTOR_REQUEST_22) = 2;
        Field<uint16_t>(r, HALO4_ACTOR_REQUEST_26) = 1;
        Field<uint32_t>(r, HALO4_ACTOR_REQUEST_CHARACTER) = character;
        uint32_t actor = Engine<ActorSpawnFn>(OFFSET_HALO4_ACTOR_SPAWN)(-1, request);
        if (resolved) *resolved = -1;
        if (actor == 0xFFFFFFFF) return actor;
        Engine<ActorSetActiveFn>(OFFSET_HALO4_ACTOR_SET_ACTIVE)(actor, true);
        int engineTeam = ResolveSpawnTeam(state, team);
        if (engineTeam >= 0) {
            g_lastState = state;
            SetTeamBoth(state, actor, (int16_t)engineTeam);
            if (resolved) *resolved = engineTeam;
        }
        return actor;
    }

    void Follow(void** state, const Array& objects, uint32_t actorDatum, uint32_t character, int team, int resolved) {
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        char* actor = actors.header ? actors.Get(actorDatum) : nullptr;
        uint32_t unit = actor ? Field<uint32_t>(actor, HALO4_ACTOR_UNIT) : 0xFFFFFFFF;
        if (!ObjectFromDatum(objects, unit)) {
            LOG("Halo 4: spawned actor %08X has no unit; not followed", actorDatum);
            return;
        }
        SpawnedAi* slot = nullptr;  // a free slot, else the body dead longest
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF || !ObjectFromDatum(objects, ai.unit)) { slot = &ai; break; }
            if (ai.deadAt && (!slot || ai.deadAt < slot->deadAt)) slot = &ai;
        }
        if (slot) *slot = {unit, character, team, resolved, 0};
    }

    uint32_t NewObject(uint32_t tag, uint32_t variant, const float* position, float yaw) {
        static uint8_t placement[HALO4_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO4_OBJECT_PLACEMENT_NEW)(placement, tag, 0xFFFFFFFF, 0);
        if (variant) Field<uint32_t>((char*)placement, HALO4_PLACEMENT_VARIANT) = variant;
        memcpy(placement + HALO4_PLACEMENT_POSITION, position, 3 * sizeof(float));
        float* forward = (float*)(placement + HALO4_PLACEMENT_FORWARD);
        forward[0] = cosf(yaw);
        forward[1] = sinf(yaw);
        forward[2] = 0.0f;
        return Engine<ObjectNewFn>(OFFSET_HALO4_OBJECT_NEW)(placement);
    }

    // Makes a selection at a position: an AI (followed) or an object.
    uint32_t Create(void** state, const Array& objects, uint32_t selection, int team, const float* position,
                    float yaw) {
        VehicleChoice c = ChoiceFor(selection);
        if (Halo4Tags::GroupOf(c.vehicle) != HALO4_TAG_CHAR) return NewObject(c.vehicle, c.variant, position, yaw);
        int resolved = -1;
        uint32_t actor = SpawnActor(state, c.vehicle, position, yaw, team, &resolved);
        if (actor != 0xFFFFFFFF) Follow(state, objects, actor, c.vehicle, team, resolved);
        return actor;
    }

    void Spawn(void** state, const Players& p, const Array& objects, uint32_t selection, int team) {
        uint32_t tag = ChoiceFor(selection).vehicle;
        char* scnr = Halo4Tags::Scenario();
        if (p.count == 0) { SpawnResult("No player to spawn for"); return; }
        if (!scnr || !Halo4Tags::Get(tag)) { SpawnResult("Selection isn't in this map"); return; }
        g_wait = {};  // replaced by this newer request

        char* control = (char*)state[HALO4_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        if (!control) { SpawnResult("No player to spawn for"); return; }
        float yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
        float dir[3] = {cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)};
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_HALO4_UNIT_HEAD_POSITION)(p.datums[0], eye);
        float end[3] = {eye[0] + dir[0] * kSpawnRange, eye[1] + dir[1] * kSpawnRange, eye[2] + dir[2] * kSpawnRange};

        alignas(16) static uint8_t query[HALO4_QUERY_SIZE];
        alignas(16) static uint8_t result[HALO4_COLLISION_RESULT_SIZE];
        memset(query, 0, sizeof(query));
        memset(result, 0, sizeof(result));
        char* q = (char*)query;
        Field<int32_t>(q, HALO4_QUERY_KIND) = HALO4_COLLISION_KIND;
        Field<uint64_t>(q, HALO4_QUERY_FLAGS) = *(uint64_t*)(g_module + OFFSET_HALO4_COLLISION_FLAGS);
        memcpy(q + HALO4_QUERY_START, eye, sizeof(eye));
        memcpy(q + HALO4_QUERY_END, end, sizeof(end));
        int ignored = 0;
        Field<uint32_t>(q, HALO4_QUERY_IGNORE + 4 * ignored++) = p.datums[0];
        uint32_t vehicle = Field<uint32_t>(p.units[0], HALO4_OBJECT_PARENT);
        if (vehicle != 0xFFFFFFFF) Field<uint32_t>(q, HALO4_QUERY_IGNORE + 4 * ignored++) = vehicle;
        Field<uint8_t>(q, HALO4_QUERY_IGNORE_COUNT) = (uint8_t)ignored;
        bool hit = Engine<CollisionTestFn>(OFFSET_HALO4_COLLISION_TEST)(query, result);
        float fraction = Field<float>((char*)result, HALO4_COLLISION_RESULT_FRACTION);
        if (!hit || !(fraction >= 0.0f && fraction < 1.0f)) { SpawnResult("Nothing in range to spawn on"); return; }
        float at[3];
        for (int k = 0; k < 3; ++k) at[k] = eye[k] + (end[k] - eye[k]) * fraction;
        float position[3] = {at[0] - dir[0] * kSpawnBackOff, at[1] - dir[1] * kSpawnBackOff,
                             at[2] - dir[2] * kSpawnBackOff + kSpawnLift};
        float facing = yaw;
        uint32_t group = Halo4Tags::GroupOf(tag);
        if (group == HALO4_TAG_VEHI || group == HALO4_TAG_SCEN || group == HALO4_TAG_MACH ||
            group == HALO4_TAG_CTRL || group == HALO4_TAG_BLOC) {
            // Backed off by its size so it isn't in a wall, and kept off the floor.
            float radius = Field<float>(Halo4Tags::Get(tag), HALO4_OBJE_BOUNDING_RADIUS);
            if (!(radius > 0.0f && radius < 100.0f)) radius = 2.0f;
            for (int k = 0; k < 3; ++k) position[k] = at[k] - dir[k] * radius;
            position[2] = (position[2] > at[2] ? position[2] : at[2]) + kVehicleLift;
        } else if (group == HALO4_TAG_CHAR) {
            facing = remainderf(yaw + kPi, 2.0f * kPi);  // AI face the player
            if (facing < 0.0f) facing += 2.0f * kPi;
        }

        uint32_t made = Create(state, objects, selection, team, position, facing);
        LOG("Halo 4: spawn %08X at %.2f %.2f %.2f (hit %.2f %.2f %.2f, fraction %.3f) -> object/actor %08X", tag,
            position[0], position[1], position[2], at[0], at[1], at[2], fraction, made);
        char text[96];
        std::string name = SelectionName(selection);
        uint64_t zone = made == 0xFFFFFFFF ? ZoneToLoad(scnr, tag) : 0;
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

    // Retries a spawn each tick once its zone is loaded (its resources may
    // take a few more ticks), until it works or times out.
    void ContinueZoneWait(void** state, const Array& objects) {
        if (!g_wait.active) return;
        char text[96];
        if (ActiveZones() & g_wait.zone) {
            uint32_t made = Create(state, objects, g_wait.selection, g_wait.team, g_wait.position, g_wait.yaw);
            if (made != 0xFFFFFFFF) {
                LOG("Halo 4: spawn %08X: made after its zone loaded -> %08X", g_wait.selection, made);
                sprintf_s(text, "Spawned %s", SelectionName(g_wait.selection).c_str());
                SpawnResult(text);
                g_wait = {};
                return;
            }
        }
        if (GetTickCount64() > g_wait.deadline) {
            sprintf_s(text, "Couldn't spawn %s here", SelectionName(g_wait.selection).c_str());
            SpawnResult(text);
            g_wait = {};
        }
    }

    // Spawned AI after death. "Enable respawn" is the mod's own, read once per
    // death: on, an AI dead for kRespawnMs is replaced by a fresh one of the
    // same character and team where it lay (the body is removed); off, it
    // stays a body.
    void FollowSpawnedAi(void** state, const Players& p, const Array& objects, bool respawn) {
        ULONGLONG now = GetTickCount64();
        for (auto& ai : g_spawnedAi) {
            if (ai.unit == 0xFFFFFFFF) continue;
            char* unit = ObjectFromDatum(objects, ai.unit);
            if (!unit) { ai = {}; continue; }
            float* health = Health(unit);
            if (!health || *health > 0.0f) { ai.deadAt = 0; continue; }
            if (!ai.deadAt) {
                ai.deadAt = now;
                LOG("Halo 4: spawned AI unit %08X died", ai.unit);
                continue;
            }
            if (now - ai.deadAt < kRespawnMs) continue;
            SpawnedAi old = ai;
            ai = {};
            if (!respawn || !Halo4Tags::Get(old.character)) {
                LOG("Halo 4: spawned AI unit %08X left as a body (respawn off)", old.unit);
                continue;
            }
            float position[3];
            memcpy(position, &Field<float>(unit, HALO4_OBJECT_POSITION), sizeof(position));
            position[2] += kSpawnLift;
            float yaw = 0.0f;
            if (p.count > 0) {
                const float* to = &Field<float>(p.units[0], HALO4_OBJECT_POSITION);
                yaw = atan2f(to[1] - position[1], to[0] - position[0]);
                if (yaw < 0.0f) yaw += 2.0f * kPi;
            }
            Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(old.unit);
            int resolved = -1;
            uint32_t actor = SpawnActor(state, old.character, position, yaw, old.team, &resolved);
            if (actor != 0xFFFFFFFF) Follow(state, objects, actor, old.character, old.team, resolved);
            LOG("Halo 4: spawned AI respawned: old unit %08X -> actor %08X", old.unit, actor);
        }
    }
}

namespace {
    // The engine's ray from `from` along `vec`, ignoring player 1 and their
    // vehicle if asked. Writes the hit point.
    bool CastRay(const Players& p, const float* from, const float* vec, bool ignorePlayer, float* hit) {
        alignas(16) static uint8_t query[HALO4_QUERY_SIZE];
        alignas(16) static uint8_t result[HALO4_COLLISION_RESULT_SIZE];
        memset(query, 0, sizeof(query));
        memset(result, 0, sizeof(result));
        char* q = (char*)query;
        float end[3] = {from[0] + vec[0], from[1] + vec[1], from[2] + vec[2]};
        Field<int32_t>(q, HALO4_QUERY_KIND) = HALO4_COLLISION_KIND;
        Field<uint64_t>(q, HALO4_QUERY_FLAGS) = *(uint64_t*)(g_module + OFFSET_HALO4_COLLISION_FLAGS);
        memcpy(q + HALO4_QUERY_START, from, 3 * sizeof(float));
        memcpy(q + HALO4_QUERY_END, end, sizeof(end));
        int ignored = 0;
        if (ignorePlayer && p.count > 0) {
            Field<uint32_t>(q, HALO4_QUERY_IGNORE + 4 * ignored++) = p.datums[0];
            uint32_t vehicle = Field<uint32_t>(p.units[0], HALO4_OBJECT_PARENT);
            if (vehicle != 0xFFFFFFFF) Field<uint32_t>(q, HALO4_QUERY_IGNORE + 4 * ignored++) = vehicle;
        }
        Field<uint8_t>(q, HALO4_QUERY_IGNORE_COUNT) = (uint8_t)ignored;
        if (!Engine<CollisionTestFn>(OFFSET_HALO4_COLLISION_TEST)(query, result)) return false;
        float fraction = Field<float>((char*)result, HALO4_COLLISION_RESULT_FRACTION);
        if (!(fraction >= 0.0f && fraction < 1.0f)) return false;
        for (int k = 0; k < 3; ++k) hit[k] = from[k] + vec[k] * fraction;
        return true;
    }

    // The engine work behind the Fun, Chaos and Skulls pages (games/fun.h).
    class FunTitle : public Fun::Title {
    public:
        FunTitle(void** state, const Players& p, const Array& objects) : m_state(state), m_p(p), m_objects(objects) {}
        const char* Name() override { return "Halo 4"; }
        uint32_t Player() override { return m_p.count > 0 ? m_p.datums[0] : 0xFFFFFFFF; }
        uint32_t PlayerVehicle() override {
            if (m_p.count == 0) return 0xFFFFFFFF;
            uint32_t top = 0xFFFFFFFF, at = m_p.datums[0];
            for (int depth = 0; depth < 5; ++depth) {
                char* object = ObjectFromDatum(m_objects, at);
                uint32_t parent = object ? Field<uint32_t>(object, HALO4_OBJECT_PARENT) : 0xFFFFFFFF;
                if (!ObjectOfType(m_objects, parent, HALO4_OBJECT_TYPE_VEHICLE)) break;
                top = at = parent;
            }
            return top;
        }
        bool View(float* eye, float* dir) override {
            char* control = (char*)m_state[HALO4_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
            if (!control || m_p.count == 0) return false;
            float yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
            float pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
            dir[0] = cosf(yaw) * cosf(pitch);
            dir[1] = sinf(yaw) * cosf(pitch);
            dir[2] = sinf(pitch);
            Engine<HeadPositionFn>(OFFSET_HALO4_UNIT_HEAD_POSITION)(m_p.datums[0], eye);
            return true;
        }
        bool Ray(const float* from, const float* vec, bool ignorePlayer, float* hit) override {
            return CastRay(m_p, from, vec, ignorePlayer, hit);
        }
        bool Position(uint32_t object, float* out) override {
            char* o = ObjectFromDatum(m_objects, object);
            if (o) memcpy(out, &Field<float>(o, HALO4_OBJECT_POSITION), 3 * sizeof(float));
            return o != nullptr;
        }
        float Radius(uint32_t object) override {
            char* o = ObjectFromDatum(m_objects, object);
            char* tag = o ? Halo4Tags::Get(Field<uint32_t>(o, HALO4_OBJECT_TAG_DATUM)) : nullptr;
            return tag ? Field<float>(tag, HALO4_OBJE_BOUNDING_RADIUS) : 0.0f;
        }
        void Teleport(uint32_t object, const float* position) override {
            Engine<SetPositionFn>(OFFSET_HALO4_OBJECT_SET_POSITION)(object, position, nullptr, nullptr, 0, false);
            const float still[3] = {};
            Engine<SetVelocityFn>(OFFSET_HALO4_OBJECT_SET_VELOCITY)(object, still, still);
            if (m_p.count > 0 && object == m_p.datums[0]) Halo4Cheats::MoveNoclipAnchor(position);
        }
        void AddVelocity(uint32_t object, const float* add) override {
            char* o = ObjectFromDatum(m_objects, object);
            if (!o) return;
            const float* now = &Field<float>(o, HALO4_OBJECT_VELOCITY);
            float velocity[3] = {now[0] + add[0], now[1] + add[1], now[2] + add[2]};
            Engine<SetVelocityFn>(OFFSET_HALO4_OBJECT_SET_VELOCITY)(object, velocity, nullptr);
        }
        uint32_t Create(uint32_t selection, int team, const float* position, float yaw) override {
            uint32_t made = ::Create(m_state, m_objects, selection, team, position, yaw);
            if (made == 0xFFFFFFFF || Halo4Tags::GroupOf(ChoiceFor(selection).vehicle) != HALO4_TAG_CHAR) return made;
            Array actors{(char*)m_state[HALO4_TLS_SLOT_ACTORS]};  // an actor: its unit
            char* actor = actors.header ? actors.Get(made) : nullptr;
            return actor ? Field<uint32_t>(actor, HALO4_ACTOR_UNIT) : 0xFFFFFFFF;
        }
        void PrepareFor(uint32_t selection) override {
            if (uint64_t zone = ZoneToLoad(Halo4Tags::Scenario(), ChoiceFor(selection).vehicle)) LoadZones(zone);
        }
        bool InUseByPlayer(uint32_t object) override {
            for (int i = 0; i < m_p.count; ++i) {
                if (Field<uint32_t>(m_p.units[i], HALO4_OBJECT_PARENT) == object) return true;
                for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT; ++w)
                    if (Field<uint32_t>(m_p.units[i], HALO4_UNIT_WEAPONS + w * 4) == object) return true;
            }
            return false;
        }
        void Delete(uint32_t object) override { Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(object); }
        void Message(const char* text) override { SpawnResult(text); }
        bool ReadSkulls(uint64_t& mask) override {
            char* globals = (char*)m_state[HALO4_TLS_SLOT_GAME_GLOBALS];
            if (!globals) return false;
            mask = Field<uint64_t>(globals, HALO4_GAME_GLOBALS_SKULLS);
            return true;
        }
        void WriteSkulls(uint64_t mask) override {
            uint64_t& skulls = Field<uint64_t>((char*)m_state[HALO4_TLS_SLOT_GAME_GLOBALS], HALO4_GAME_GLOBALS_SKULLS);
            skulls = mask;
            Engine<void(__fastcall*)(uint64_t*)>(OFFSET_HALO4_SKULLS_CHANGED)(&skulls);  // as skull_enable does
        }
        uint64_t SkullMask() override { return (1ull << HALO4_SKULL_COUNT) - 1; }

    private:
        void** m_state;
        const Players& m_p;
        const Array& m_objects;
    };
}

void Halo4Spawn::FunTick(void** state, const Players& p, const Array& objects, const Games::Settings& s, float dt) {
    if (!g_module) return;
    FunTitle title(state, p, objects);
    Fun::Tick(title, s, dt);
}

void Halo4Spawn::SetModule(HMODULE halo4) { g_module = (char*)halo4; }

void Halo4Spawn::Reset() {
    Fun::Reset();
    for (auto& ai : g_spawnedAi) ai = {};
    g_rogueMask = 0;
    g_lastState = nullptr;
    g_wait = {};
    g_forcedZones = 0;
    g_forcedScenario = nullptr;
    g_characters.clear();
    g_vehicles.clear();
    g_objects.clear();
    std::lock_guard<std::mutex> lock(g_listsLock);
    g_vehicleChoices.clear();
}

void Halo4Spawn::RefreshLists(char* scnr) {
    g_characters.clear();
    for (auto& character : Halo4Tags::ListGroup(HALO4_TAG_CHAR))
        if (Spawnable(character.datum)) g_characters.push_back(character);
    Guard("Halo 4 vehicle list", [&] { BuildVehicleList(scnr); });
    Guard("Halo 4 object list", [&] { BuildObjectList(scnr); });
    LOG("Halo 4: spawn lists: %zu characters, %zu vehicles, %zu objects", g_characters.size(), g_vehicles.size(),
        g_objects.size());
}

void Halo4Spawn::FillStatus(Games::Status& status) {
    status.spawnAi = &g_characters;
    status.spawnVehicles = &g_vehicles;
    status.spawnObjects = &g_objects;
    std::lock_guard<std::mutex> lock(g_resultLock);
    status.spawnDone = g_spawnDone;
    sprintf_s(status.spawnMessage, "%s", g_spawnMessage);
}

void Halo4Spawn::Tick(void** state, const Players& p, const Array& objects, const Games::Settings& s) {
    if (!g_module) return;
    g_lastState = state;
    if (!g_haveSerial) {  // don't run a key press from before this session
        g_spawnSerial = s.spawnSerial;
        g_haveSerial = true;
    }
    g_random ^= (uint32_t)GetTickCount64();  // a new roll each tick
    if (!g_random) g_random = 1;
    Guard("Halo 4 spawn zones", [&] { KeepForcedZones(Halo4Tags::Scenario()); });
    if (s.spawnSerial != g_spawnSerial) {
        g_spawnSerial = s.spawnSerial;
        if (s.spawnEnabled && s.spawnSquad >= 0)
            Guard("Halo 4 squad", [&] { Halo4Upgrade::SpawnSquad(state, p, objects, s.spawnSquad, s.spawnTeam); });
        else if (s.spawnEnabled && s.spawnTag != 0xFFFFFFFF)
            Guard("Halo 4 spawn", [&] { Spawn(state, p, objects, s.spawnTag, s.spawnTeam); });
    }
    Guard("Halo 4 spawn zone wait", [&] { ContinueZoneWait(state, objects); });
    Guard("Halo 4 spawned AI", [&] { FollowSpawnedAi(state, p, objects, s.spawnRespawn); });
    if (g_rogueMask) Guard("Halo 4 rogue teams", [&] { MakeRoguesHostile(state); });
}

// --- Shared with the feature files (internal.h) ---------------------------------
// Each is called untested from other files, so each is defensive: null checks,
// and the engine calls and raw reads run inside SEH.

char* Halo4Internal::Module() { return g_module; }

uint32_t Halo4Internal::SpawnActor(void** state, const float* position, float yaw, uint32_t character, int team) {
    uint32_t actor = 0xFFFFFFFF;
    if (!g_module || !state || !position) return actor;
    Guard("Halo 4 SpawnActor", [&] {
        if (!Halo4Tags::Scenario() || !Halo4Tags::Get(character)) return;
        actor = ::SpawnActor(state, character, position, yaw, team);
    });
    return actor;
}

int Halo4Internal::ResolveSpawnTeam(void** state, int choice) {
    int team = -1;
    if (!g_module || !state) return team;
    Guard("Halo 4 ResolveSpawnTeam", [&] { team = ::ResolveSpawnTeam(state, choice); });
    return team;
}

void Halo4Internal::SetActorTeam(uint32_t actor, int16_t team) {
    if (!g_module || actor == 0xFFFFFFFF) return;
    Guard("Halo 4 SetActorTeam", [&] { SetTeamBoth(g_lastState, actor, team); });
}

void Halo4Internal::Follow(void** state, const Array& objects, uint32_t actor, uint32_t character, int team) {
    if (!g_module || !state) return;
    Guard("Halo 4 Follow", [&] {
        // The engine team the actor ended up on (own team / Flood: -1), for the rogue team balancing.
        int resolved = -1;
        Array actors{(char*)state[HALO4_TLS_SLOT_ACTORS]};
        char* a = actors.Valid() ? actors.Get(actor) : nullptr;
        if (a && team != 0 && team != 3) resolved = Field<int8_t>(a, HALO4_ACTOR_TEAM);
        ::Follow(state, objects, actor, character, team, resolved);
    });
}

void Halo4Internal::Refollow(void** state, const Array& objects, uint32_t oldUnit, uint32_t newActor,
                             uint32_t newCharacter) {
    if (!g_module || !state) return;
    Guard("Halo 4 Refollow", [&] {
        for (auto& ai : g_spawnedAi) {
            if (ai.unit != oldUnit || oldUnit == 0xFFFFFFFF) continue;
            int team = ai.team;
            ai = {};
            Halo4Internal::Follow(state, objects, newActor, newCharacter, team);
            return;
        }
    });
}

int Halo4Internal::FollowedUnits(uint32_t* out, int max) {
    int n = 0;
    if (!out) return 0;
    for (auto& ai : g_spawnedAi)
        if (ai.unit != 0xFFFFFFFF && n < max) out[n++] = ai.unit;
    return n;
}

void Halo4Internal::ForgetFollowed() {
    for (auto& ai : g_spawnedAi) ai = {};
    g_wait = {};
}

void Halo4Internal::Toast(const char* text) { SpawnResult(text ? text : ""); }

void Halo4Internal::DeleteObject(uint32_t object) {
    if (!g_module || object == 0xFFFFFFFF) return;
    Guard("Halo 4 DeleteObject", [&] { Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(object); });
}

// The engine's hostility test (RVA 0x207AFC): -1 on either side is hostile, the
// same team never is, otherwise bit b of team a's mask (allegiance table, TLS slot 51 +0x168).
bool Halo4Internal::TeamsFriends(void** state, int a, int b) {
    if (a < 0 || b < 0 || a >= HALO4_TEAM_COUNT || b >= HALO4_TEAM_COUNT) return false;
    if (a == b) return true;
    bool friends = false;
    if (!state) return false;
    Guard("Halo 4 TeamsFriends", [&] {
        uint32_t* masks = AllegianceMasks(state, HALO4_ALLEGIANCE_MASKS);
        friends = masks && ((masks[a] >> b) & 1);
    });
    return friends;
}

bool Halo4Internal::AimPoint(void** state, const Players& p, float* at, float* dir, float* facing) {
    if (!g_module || !state || p.count == 0 || !at || !dir || !facing) return false;
    bool ok = false;
    Guard("Halo 4 AimPoint", [&] {
        char* control = (char*)state[HALO4_TLS_SLOT_PLAYER_CONTROL];  // entry 0 = local player 1
        if (!control) return;
        float yaw = Field<float>(control, HALO4_PLAYER_CONTROL_YAW);
        float pitch = Field<float>(control, HALO4_PLAYER_CONTROL_PITCH);
        dir[0] = cosf(yaw) * cosf(pitch);
        dir[1] = sinf(yaw) * cosf(pitch);
        dir[2] = sinf(pitch);
        float eye[3];
        Engine<HeadPositionFn>(OFFSET_HALO4_UNIT_HEAD_POSITION)(p.datums[0], eye);
        float ray[3] = {dir[0] * kSpawnRange, dir[1] * kSpawnRange, dir[2] * kSpawnRange};
        if (!CastRay(p, eye, ray, true, at)) return;
        *facing = remainderf(yaw + kPi, 2.0f * kPi);
        if (*facing < 0.0f) *facing += 2.0f * kPi;
        ok = true;
    });
    return ok;
}

const std::vector<Games::TagChoice>& Halo4Internal::Characters() { return g_characters; }

bool Halo4Internal::LoadZoneFor(uint32_t tag) {
    uint64_t zone = 0;
    if (!g_module) return false;
    Guard("Halo 4 LoadZoneFor", [&] {
        char* scnr = Halo4Tags::Scenario();
        zone = scnr ? ZoneToLoad(scnr, tag) : 0;
        if (zone) LoadZones(zone);
    });
    return zone != 0;
}
