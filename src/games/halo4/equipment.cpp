// Halo 4 equipment: Give equipment, Perfect camo, and the self destruct edit
// that lets deployed equipment last while Infinite equipment is on. Ported
// from Reach (reach/main_thread.cpp GiveEquipment, reach/cheats.cpp
// PerfectCamo / NoSelfDestruct) in the shape of halo3/equipment.cpp; the
// offsets and how they were found are in equipment_offsets.h. Tick runs on the
// game thread inside SEH, Apply on the render thread.

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MinHook.h"
#include "core/log.h"
#include "equipment_offsets.h"
#include "internal.h"

using namespace Halo4Cheats;

namespace {
    constexpr int kMaxEqipTags = 64;

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using UnitAddEquipmentFn = void(__fastcall*)(uint32_t unit, uint32_t equipment, bool flag);
    using UnitRemoveEquipmentFn = void(__fastcall*)(uint32_t unit);

    template <typename Fn>
    void Guard(const char* what, Fn fn) {
        __try {
            fn();
        } __except (Log::Exception(GetExceptionInformation(), what)) {
        }
    }

    // --- Tick (game thread) state ------------------------------------------
    bool g_haveSerial = false;
    uint32_t g_giveSerial = 0;

    // Every 'eqip' tag of the loaded map, for the self destruct edit; rebuilt
    // when the scenario changes.
    char* g_tickScenario = nullptr;
    std::vector<uint32_t> g_eqipTags;

    Restorable<float> g_mineDestruct[kMaxEqipTags];    // ability Proximity Mine block's Self Destruct Time
    Restorable<float> g_objectDestruct[kMaxEqipTags];  // the tag's own Self-Destruct Time

    int g_camoLogs = 0;
    Restorable<float> g_ding[kMaxLocalPlayers * HALO4_UNIT_WEAPON_COUNT];  // weapons' Active Camo Ding

    // The camo update (0x5EC2AC) snaps the level to a speed based target inside
    // every game tick (live log: a pinned 1.0 came back as 0.90 each tick), so
    // pinning before the tick does nothing. The detour pins right after it.
    using CamoUpdateFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
    CamoUpdateFn g_origCamoUpdate = nullptr;
    void* g_camoHook = nullptr;
    volatile bool g_pinCamo = false;
    uint32_t g_playerDatums[kMaxLocalPlayers] = {};
    int g_playerCount = 0;
    Array g_objects;

    // --- Apply (render thread) state ---------------------------------------
    std::vector<Games::TagChoice> g_list;
    char* g_listScenario = nullptr;
    int g_unnamedFrames = 0;

    std::string ShortName(uint32_t tag) {
        std::string name = Halo4Tags::FullName(tag);
        if (name.empty()) {
            char text[16];
            sprintf_s(text, "%08X", tag);
            return text;
        }
        size_t slash = name.find_last_of('\\');
        return slash != std::string::npos ? name.substr(slash + 1) : name;
    }

    void NotLoaded(uint32_t tag) {
        char text[128];
        sprintf_s(text, "%s: not loaded in this area", ShortName(tag).c_str());
        Halo4Internal::Toast(text);
    }

    // Sets each wanted float (tag fields) to `value`, and restores the ones
    // no longer wanted. Same shape as Reach's SetValues.
    template <size_t N>
    void SetValues(Restorable<float> (&patches)[N], float* const* wanted, int wantedCount, float value) {
        for (auto& patch : patches) {
            if (!patch.addr) continue;
            bool keep = false;
            for (int i = 0; i < wantedCount; ++i) keep |= wanted[i] == patch.addr;
            if (!keep) patch.Restore();
        }
        for (int i = 0; i < wantedCount; ++i) {
            Restorable<float>* slot = nullptr;
            for (auto& patch : patches)
                if (patch.addr == wanted[i]) { slot = &patch; break; }
            for (auto& patch : patches)
                if (!slot && !patch.addr) slot = &patch;
            if (slot) slot->Set(wanted[i], value);
        }
    }

    template <size_t N>
    void RestoreAll(Restorable<float> (&patches)[N]) {
        for (auto& patch : patches) patch.Restore();
    }

    template <size_t N>
    void ForgetAll(Restorable<float> (&patches)[N]) {
        for (auto& patch : patches) patch = {};
    }

    // --- Give equipment ------------------------------------------------------
    // Replaces player 1's armor ability with the chosen one, the way
    // unit_set_equipment / the starting loadout hand one out: make the object
    // like a weapon (owner = the unit), remove the current ability, attach the
    // new one (0x5FC26C). The new one is made first, so a tag the game can't
    // make costs nothing.
    void GiveEquipment(const Players& p, const Array& objects, uint32_t tag) {
        if (p.count == 0) { LOG("Halo 4: give equipment: no player unit"); return; }
        if (!Halo4Tags::Get(tag) || Halo4Tags::GroupOf(tag) != HALO4_TAG_EQIP) {
            LOG("Halo 4: give equipment %08X: tag not loaded", tag);
            NotLoaded(tag);
            return;
        }
        char* unit = p.units[0];
        uint32_t unitDatum = p.datums[0];

        static uint8_t placement[HALO4_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_HALO4_OBJECT_PLACEMENT_NEW)(placement, tag, unitDatum, 0);
        uint32_t equipment = Engine<ObjectNewFn>(OFFSET_HALO4_OBJECT_NEW)(placement);
        if (equipment == 0xFFFFFFFF) {
            LOG("Halo 4: give equipment %08X: the game couldn't create it here", tag);
            NotLoaded(tag);
            return;
        }

        uint32_t held = Field<uint32_t>(unit, HALO4_UNIT_EQUIPMENT);
        if (held != 0xFFFFFFFF) Engine<UnitRemoveEquipmentFn>(OFFSET_HALO4_UNIT_REMOVE_EQUIPMENT)(unitDatum);
        if (char* object = ObjectOfType(objects, equipment, HALO4_OBJECT_TYPE_EQUIPMENT))
            Field<float>(object, HALO4_EQUIPMENT_ENERGY) = 1.0f;
        Engine<UnitAddEquipmentFn>(OFFSET_HALO4_UNIT_ADD_EQUIPMENT)(unitDatum, equipment, false);

        uint32_t now = Field<uint32_t>(unit, HALO4_UNIT_EQUIPMENT);
        LOG("Halo 4: give equipment %s (object %08X, replaced %08X): unit now holds %08X", ShortName(tag).c_str(),
            equipment, held, now);
        if (now != equipment) {  // the game didn't take it: don't leave an orphan lying about
            Engine<ObjectDeleteFn>(OFFSET_HALO4_OBJECT_DELETE)(equipment);
            Halo4Internal::Toast("Couldn't give that equipment");
        }
    }

    // --- Deployed equipment lifetime -------------------------------------------
    // Equipment that removes itself after a set time (Reach's drop shield bubble
    // died after 15 s) loses that time while Infinite equipment is on: the
    // Proximity Mine block's Self Destruct Time of every ability, and the tag's
    // own Self-Destruct Time when it's above 0. 0 means never destroy. Both are
    // restored when it's off. None of m60_rescue's eqip tags has either set, so
    // this is a safety net for the maps that do.
    void RefreshEqipTags() {
        char* scenario = Halo4Tags::Scenario();
        if (scenario == g_tickScenario) return;
        g_tickScenario = scenario;
        g_eqipTags.clear();
        if (!scenario) return;
        for (auto& choice : Halo4Tags::ListGroup(HALO4_TAG_EQIP)) g_eqipTags.push_back(choice.datum);
        LOG("Halo 4: equipment: %zu eqip tags in this map", g_eqipTags.size());
    }

    void NoSelfDestruct(bool on) {
        if (!on) {
            RestoreAll(g_mineDestruct);
            RestoreAll(g_objectDestruct);
            return;
        }
        RefreshEqipTags();
        float* mine[kMaxEqipTags] = {};
        float* object[kMaxEqipTags] = {};
        int mineCount = 0, objectCount = 0;
        for (uint32_t datum : g_eqipTags) {
            char* tag = Halo4Tags::Get(datum);
            if (!tag) continue;
            char* ability = Halo4Tags::BlockElement(tag, HALO4_EQIP_ABILITIES, HALO4_EQIP_ABILITY_SIZE, 0);
            char* block = ability ? Halo4Tags::BlockElement(ability, HALO4_EQIP_PROXIMITY_MINE, HALO4_EQIP_MINE_SIZE, 0)
                                  : nullptr;
            if (block && mineCount < kMaxEqipTags) mine[mineCount++] = &Field<float>(block, HALO4_EQIP_MINE_SELF_DESTRUCT);
            float& d = Field<float>(tag, HALO4_EQIP_SELF_DESTRUCT);
            bool ours = false;
            for (auto& patch : g_objectDestruct) ours |= patch.addr == &d;
            if (objectCount < kMaxEqipTags && (d > 0.0f || ours)) object[objectCount++] = &d;
        }
        SetValues(g_mineDestruct, mine, mineCount, 0.0f);
        SetValues(g_objectDestruct, object, objectCount, 0.0f);
    }

    // --- Perfect camo ------------------------------------------------------------
    // While a player's camo is on (mode +0x7D4 isn't -1) and has started (level
    // above 0), its level (1.0 = fully effective) is held at 1.0 instead of
    // falling as the player moves; switched off, the mode is -1 and it fades
    // out as normal. Done right after the camo update (the detour) and, for
    // anything reading in between, before the tick. Weapons' Active Camo Ding
    // (what a shot subtracts) is zeroed too, as in Halo 3.
    void PinCamo(char* unit) {
        if (Field<int32_t>(unit, HALO4_UNIT_CAMO_MODE) == -1) return;
        float& value = Field<float>(unit, HALO4_UNIT_CAMO_VALUE);
        if (!(value > 0.0f)) return;
        if (value < 1.0f && g_camoLogs < 3) {
            ++g_camoLogs;
            LOG("Halo 4: perfect camo: level %.2f -> 1.0 (mode %d)", value,
                Field<int32_t>(unit, HALO4_UNIT_CAMO_MODE));
        }
        value = 1.0f;
        Field<float>(unit, HALO4_UNIT_CAMO_PREVIOUS) = 1.0f;
    }

    void PinCamoOf(uint32_t datum) {
        for (int i = 0; i < g_playerCount; ++i) {
            if (g_playerDatums[i] != datum) continue;
            if (char* unit = ObjectFromDatum(g_objects, datum)) PinCamo(unit);
        }
    }

    uint64_t __fastcall CamoUpdateDetour(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
        uint64_t result = g_origCamoUpdate(a, b, c, d);
        if (g_pinCamo) {
            __try {
                PinCamoOf((uint32_t)a);
            } __except (Log::Exception(GetExceptionInformation(), "Halo 4 camo pin")) {
            }
        }
        return result;
    }

    void PerfectCamo(const Players& p, const Array& objects, bool on) {
        g_pinCamo = on;
        g_objects = objects;
        g_playerCount = p.count;
        for (int i = 0; i < p.count; ++i) g_playerDatums[i] = p.datums[i];
        float* dings[_countof(g_ding)] = {};
        int dingCount = 0;
        for (int i = 0; on && i < p.count; ++i) {
            for (int w = 0; w < HALO4_UNIT_WEAPON_COUNT && dingCount < (int)_countof(dings); ++w) {
                char* weapon = WeaponInSlot(p.units[i], objects, w);
                char* tag = weapon ? Halo4Tags::Get(Field<uint32_t>(weapon, HALO4_OBJECT_TAG_DATUM)) : nullptr;
                if (!tag) continue;
                float* ding = &Field<float>(tag, HALO4_WEAP_CAMO_DING);
                bool known = false;
                for (int k = 0; k < dingCount; ++k) known |= dings[k] == ding;
                if (!known) dings[dingCount++] = ding;
            }
        }
        SetValues(g_ding, dings, dingCount, 0.0f);
        if (on)
            for (int i = 0; i < p.count; ++i) PinCamo(p.units[i]);
    }

    // --- Apply: the list ---------------------------------------------------------
    bool ContainsNoCase(const std::string& text, const char* word) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
        return lower.find(word) != std::string::npos;
    }

    // Everything the map has except ammo, grenade and rocket tags (the names
    // Reach's list drops too) and tags whose first ability has 0 Charges (what
    // the ammo boxes and grenades read; armor abilities read -1).
    void RefreshList() {
        char* scenario = Halo4Tags::Scenario();
        if (!scenario) {
            g_list.clear();
            g_listScenario = nullptr;
            return;
        }
        if (scenario == g_listScenario) return;
        bool named = Halo4Tags::LoadNames();
        if (!named && (g_unnamedFrames++ % 30) != 0) return;  // keep trying, not every frame

        g_list.clear();
        for (auto& choice : Halo4Tags::ListGroup(HALO4_TAG_EQIP)) {
            if (ContainsNoCase(choice.name, "ammo") || ContainsNoCase(choice.name, "grenade") ||
                ContainsNoCase(choice.name, "rocket"))
                continue;
            char* tag = Halo4Tags::Get(choice.datum);
            if (!tag) continue;
            char* ability = Halo4Tags::BlockElement(tag, HALO4_EQIP_ABILITIES, HALO4_EQIP_ABILITY_SIZE, 0);
            if (ability && Field<int16_t>(ability, HALO4_EQIP_ABILITY_CHARGES) == 0) continue;
            g_list.push_back(choice);
        }
        if (named) {
            g_listScenario = scenario;
            g_unnamedFrames = 0;
        }
        LOG("Halo 4: map has %zu giveable equipment tags", g_list.size());
    }
}

void Halo4Equipment::Load(char* module) {
    g_module = module;
    void* target = module + OFFSET_HALO4_CAMO_UPDATE;
    if (MH_CreateHook(target, (void*)&CamoUpdateDetour, (void**)&g_origCamoUpdate) == MH_OK &&
        MH_EnableHook(target) == MH_OK)
        g_camoHook = target;
    else {
        MH_RemoveHook(target);
        LOG("Halo 4: failed to hook the camo update at %p", target);
    }
}

void Halo4Equipment::Unload() {
    g_pinCamo = false;
    if (g_camoHook) {
        MH_DisableHook(g_camoHook);
        MH_RemoveHook(g_camoHook);
        g_camoHook = nullptr;
    }
    Guard("Halo 4 equipment unload", [] {
        RestoreAll(g_mineDestruct);
        RestoreAll(g_objectDestruct);
        RestoreAll(g_ding);
    });
    Reset();
}

void Halo4Equipment::Reset() {
    // The tags went with the map: nothing to write back, just forget.
    ForgetAll(g_mineDestruct);
    ForgetAll(g_objectDestruct);
    ForgetAll(g_ding);
    g_pinCamo = false;
    g_playerCount = 0;
    g_tickScenario = nullptr;
    g_eqipTags.clear();
    g_listScenario = nullptr;
}

void Halo4Equipment::Tick(void** state, const Games::Settings& s, const Players& p, const Array& objects) {
    (void)state;
    if (!g_module) return;
    if (!g_haveSerial) {  // a press from before the first tick isn't ours
        g_haveSerial = true;
        g_giveSerial = s.giveEquipmentSerial;
    }
    if (s.giveEquipmentSerial != g_giveSerial) {
        g_giveSerial = s.giveEquipmentSerial;
        LOG("Halo 4: give equipment request %08X (%d player units)", s.giveEquipment, p.count);
        Guard("Halo 4 give equipment", [&] { GiveEquipment(p, objects, s.giveEquipment); });
    }
    Guard("Halo 4 equipment self destruct", [&] { NoSelfDestruct(s.infiniteEquipment); });
    if (s.perfectCamo || g_pinCamo) Guard("Halo 4 perfect camo", [&] { PerfectCamo(p, objects, s.perfectCamo); });
}

void Halo4Equipment::Apply(const Games::Settings&, Games::Status& status) {
    Guard("Halo 4 equipment list", [] { RefreshList(); });
    status.equipment = &g_list;
}
