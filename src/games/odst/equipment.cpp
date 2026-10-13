// ODST equipment: Give equipment, Perfect camo, and the lifetime edit that
// lets deployed equipment (bubble shield, regenerator, ...) last while Infinite
// equipment is on. Ported from Reach (reach/main_thread.cpp GiveEquipment,
// reach/cheats.cpp PerfectCamo / NoSelfDestruct); the offsets and how they
// were found are in equipment_offsets.h. Tick runs on the game thread inside
// SEH, Apply on the render thread.

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "equipment_offsets.h"
#include "internal.h"

using namespace OdstCheats;

namespace {
    constexpr int kMaxEqipTags = 64;
    constexpr int kMaxDings = kMaxLocalPlayers * ODST_UNIT_WEAPON_COUNT;

    char* g_module = nullptr;

    template <typename Fn>
    Fn Engine(int rva) { return (Fn)(g_module + rva); }

    using PlacementNewFn = void(__fastcall*)(void* placement, uint32_t tag, uint32_t owner, int64_t);
    using ObjectNewFn = uint32_t(__fastcall*)(void* placement);
    using ObjectDeleteFn = void(__fastcall*)(uint32_t object);
    using UnitAddEquipmentFn = void(__fastcall*)(uint32_t unit, uint32_t equipment);
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

    // Every 'eqip' tag of the loaded map, for the lifetime edit; rebuilt when
    // the scenario changes.
    char* g_tickScenario = nullptr;
    std::vector<uint32_t> g_eqipTags;

    Restorable<float> g_selfDestruct[kMaxEqipTags];
    Restorable<float> g_duration[kMaxEqipTags];
    Restorable<float> g_ding[kMaxDings];

    // Perfect camo: the highest level seen since the unit's camo came on.
    uint32_t g_camoUnit[kMaxLocalPlayers] = {};
    float g_camoPeak[kMaxLocalPlayers] = {};

    // --- Apply (render thread) state ---------------------------------------
    std::vector<Games::TagChoice> g_list;
    char* g_listScenario = nullptr;
    int g_unnamedFrames = 0;

    std::string ShortName(uint32_t tag) {
        std::string name = OdstTags::FullName(tag);
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
        OdstInternal::Toast(text);
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
    // Replaces player 1's carried equipment with the chosen one, the way the
    // starting-loadout code hands one out (0x1AF1C4..0x1AF214): make the object
    // owned by the unit, remove the current equipment, attach the new one.
    // The new one is made first, so a tag the game can't make costs nothing.
    void GiveEquipment(const Players& p, Blam::DataArray* objects, uint32_t tag) {
        if (p.count == 0) { LOG("ODST: give equipment: no player unit"); return; }
        if (!OdstTags::Get(tag) || OdstTags::GroupOf(tag) != ODST_TAG_EQIP) {
            LOG("ODST: give equipment %08X: tag not loaded", tag);
            NotLoaded(tag);
            return;
        }
        char* unit = p.units[0];
        uint32_t unitDatum = p.datums[0];

        static uint8_t placement[ODST_PLACEMENT_DATA_SIZE];
        memset(placement, 0, sizeof(placement));
        Engine<PlacementNewFn>(OFFSET_ODST_OBJECT_PLACEMENT_NEW)(placement, tag, unitDatum, 0);
        uint32_t equipment = Engine<ObjectNewFn>(OFFSET_ODST_OBJECT_NEW)(placement);
        if (equipment == 0xFFFFFFFF) {
            LOG("ODST: give equipment %08X: the game couldn't create it here", tag);
            NotLoaded(tag);
            return;
        }

        uint32_t held = Field<uint32_t>(unit, ODST_UNIT_EQUIPMENT);
        if (held != 0xFFFFFFFF) Engine<UnitRemoveEquipmentFn>(OFFSET_ODST_UNIT_REMOVE_EQUIPMENT)(unitDatum);
        Engine<UnitAddEquipmentFn>(OFFSET_ODST_UNIT_ADD_EQUIPMENT)(unitDatum, equipment);

        uint32_t now = Field<uint32_t>(unit, ODST_UNIT_EQUIPMENT);
        LOG("ODST: give equipment %s (object %08X, replaced %08X): unit now holds %08X", ShortName(tag).c_str(),
            equipment, held, now);
        if (now != equipment) {  // the game didn't take it: don't leave an orphan lying about
            Engine<ObjectDeleteFn>(OFFSET_ODST_OBJECT_DELETE)(equipment);
            OdstInternal::Toast("Couldn't give that equipment");
        }
        (void)objects;
    }

    // --- Deployed equipment lifetime -------------------------------------------
    // The object a use creates (bubbleshield, regenerator, jammer, power drain,
    // flare, grav lift, trip mine) ends after its Self Destruct Time and after
    // its Duration. While Infinite equipment is on, the first is zeroed
    // ("never destroy") and the second made very long; both are restored when
    // it's off. Only tags with the Proximity Mine block count, which is what
    // the deployed ones have (checked live on every eqip of 070_waste).
    void RefreshEqipTags() {
        char* scenario = OdstTags::GetGlobal(ODST_TAG_SCNR);
        if (scenario == g_tickScenario) return;
        g_tickScenario = scenario;
        g_eqipTags.clear();
        if (!scenario) return;
        for (auto& choice : OdstTags::ListGroup(ODST_TAG_EQIP)) g_eqipTags.push_back(choice.datum);
        LOG("ODST: equipment: %zu eqip tags in this map", g_eqipTags.size());
    }

    void DeployedLifetime(bool on) {
        if (!on) {
            RestoreAll(g_selfDestruct);
            RestoreAll(g_duration);
            return;
        }
        RefreshEqipTags();
        float* destruct[kMaxEqipTags] = {};
        float* duration[kMaxEqipTags] = {};
        int destructCount = 0, durationCount = 0;
        for (uint32_t datum : g_eqipTags) {
            char* tag = OdstTags::Get(datum);
            if (!tag) continue;
            char* mine = OdstTags::BlockElement(tag, ODST_EQIP_PROXIMITY_MINE, ODST_EQIP_MINE_SIZE, 0);
            if (!mine) continue;
            if (destructCount < kMaxEqipTags) destruct[destructCount++] = &Field<float>(mine, ODST_EQIP_MINE_SELF_DESTRUCT);
            // Instant ones (duration 0) stay as they are.
            float& d = Field<float>(tag, ODST_EQIP_DURATION);
            bool ours = false;
            for (auto& patch : g_duration) ours |= patch.addr == &d;
            if (durationCount < kMaxEqipTags && (d > 0.0f || ours)) duration[durationCount++] = &d;
        }
        SetValues(g_selfDestruct, destruct, destructCount, 0.0f);
        SetValues(g_duration, duration, durationCount, ODST_EQIP_FOREVER);
    }

    // --- Perfect camo ------------------------------------------------------------
    // While a player's camo is on, its level never drops below the highest it
    // has reached (it still rises through the activation's transition and
    // fades out when the camo is switched off). Shots would otherwise
    // subtract the weapon's Active Camo Ding (0.25 for the battle rifle, 1.0
    // for the sniper rifle), so the player's weapons' dings are zeroed too.
    void PerfectCamo(const Players& p, Blam::DataArray* objects, bool on) {
        float* dings[kMaxDings] = {};
        int dingCount = 0;
        for (int i = 0; on && i < p.count; ++i) {
            for (int w = 0; w < ODST_UNIT_WEAPON_COUNT && dingCount < kMaxDings; ++w) {
                char* weapon = Blam::ObjectFromDatum(objects, Field<uint32_t>(p.units[i], ODST_UNIT_WEAPONS + w * 4));
                if (!weapon || Field<uint8_t>(weapon, ODST_OBJECT_TYPE) != ODST_OBJECT_TYPE_WEAPON) continue;
                char* tag = OdstTags::Get(Field<uint32_t>(weapon, ODST_OBJECT_TAG_DATUM));
                if (!tag) continue;
                float* ding = &Field<float>(tag, ODST_WEAP_CAMO_DING);
                bool known = false;
                for (int k = 0; k < dingCount; ++k) known |= dings[k] == ding;
                if (!known) dings[dingCount++] = ding;
            }
        }
        SetValues(g_ding, dings, dingCount, 0.0f);

        for (int i = 0; i < kMaxLocalPlayers; ++i) {
            if (!on || i >= p.count || !(Field<uint32_t>(p.units[i], ODST_UNIT_FLAGS) & ODST_UNIT_FLAG_CAMO_ON)) {
                g_camoPeak[i] = 0.0f;
                g_camoUnit[i] = i < p.count ? p.datums[i] : 0;
                continue;
            }
            if (g_camoUnit[i] != p.datums[i]) {  // a new unit in this slot
                g_camoUnit[i] = p.datums[i];
                g_camoPeak[i] = 0.0f;
            }
            float& level = Field<float>(p.units[i], ODST_UNIT_CAMO_LEVEL);
            if (level < g_camoPeak[i]) level = g_camoPeak[i];
            else if (level <= 1.0f) g_camoPeak[i] = level;
        }
    }

    // --- Apply: the list ---------------------------------------------------------
    bool ContainsNoCase(const std::string& text, const char* word) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
        return lower.find(word) != std::string::npos;
    }

    // Pickups only: the tags a player can carry have Charges (+0x194) other than
    // 0; the object a use creates has 0, so it's left out, as are ammo,
    // grenade and rocket tags (the names Reach's list drops too).
    void RefreshList() {
        char* scenario = OdstTags::GetGlobal(ODST_TAG_SCNR);
        if (!scenario) {
            g_list.clear();
            g_listScenario = nullptr;
            return;
        }
        if (scenario == g_listScenario) return;
        bool named = OdstTags::LoadNames();
        if (!named && (g_unnamedFrames++ % 30) != 0) return;  // keep trying, not every frame

        g_list.clear();
        for (auto& choice : OdstTags::ListGroup(ODST_TAG_EQIP)) {
            if (ContainsNoCase(choice.name, "ammo") || ContainsNoCase(choice.name, "grenade") ||
                ContainsNoCase(choice.name, "rocket"))
                continue;
            char* tag = OdstTags::Get(choice.datum);
            if (!tag || Field<int16_t>(tag, ODST_EQIP_USES) == 0) continue;
            g_list.push_back(choice);
        }
        if (named) {
            g_listScenario = scenario;
            g_unnamedFrames = 0;
        }
        LOG("ODST: map has %zu giveable equipment tags", g_list.size());
    }
}

void OdstEquipment::Load(char* module) { g_module = module; }

void OdstEquipment::Unload() {
    Guard("ODST equipment unload", [] {
        RestoreAll(g_selfDestruct);
        RestoreAll(g_duration);
        RestoreAll(g_ding);
    });
    Reset();
}

void OdstEquipment::Reset() {
    // The tags went with the map: nothing to write back, just forget.
    ForgetAll(g_selfDestruct);
    ForgetAll(g_duration);
    ForgetAll(g_ding);
    g_tickScenario = nullptr;
    g_eqipTags.clear();
    g_listScenario = nullptr;
    for (int i = 0; i < kMaxLocalPlayers; ++i) {
        g_camoUnit[i] = 0;
        g_camoPeak[i] = 0.0f;
    }
}

void OdstEquipment::Tick(void** state, const Games::Settings& s, const Players& p, Blam::DataArray* objects) {
    (void)state;
    if (!g_module) return;
    if (!g_haveSerial) {  // a press from before the first tick isn't ours
        g_haveSerial = true;
        g_giveSerial = s.giveEquipmentSerial;
    }
    if (s.giveEquipmentSerial != g_giveSerial) {
        g_giveSerial = s.giveEquipmentSerial;
        LOG("ODST: give equipment request %08X (%d player units)", s.giveEquipment, p.count);
        Guard("ODST give equipment", [&] { GiveEquipment(p, objects, s.giveEquipment); });
    }
    Guard("ODST deployed equipment lifetime", [&] { DeployedLifetime(s.infiniteEquipment); });
    Guard("ODST perfect camo", [&] { PerfectCamo(p, objects, s.perfectCamo); });
}

void OdstEquipment::Apply(const Games::Settings&, Games::Status& status) {
    Guard("ODST equipment list", [] { RefreshList(); });
    status.equipment = &g_list;
}
