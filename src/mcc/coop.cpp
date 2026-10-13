#include "coop.h"

#include <combaseapi.h>
#include <xinput.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "MinHook.h"

#include "core/log.h"
#include "mcc.h"
#include "offsets.h"

// MCC's input device and device manager, as far as co-op needs them (layouts
// from AlphaRing's CInputDevice.h / Cell.h).
namespace {
    struct InputDevice {
        struct Methods {
            char un[0x38];
            int64_t(__fastcall* set_state)(InputDevice*, float, void*);
            void(__fastcall* check)(InputDevice*);
        }* methods;
        char un0[2040];
        bool enableMouse;
        char un[7];
        int input_user;  // 0x808: the XInput user index
        char un1[0x110];
        XINPUT_STATE state;  // 0x91C
    };
    static_assert(offsetof(InputDevice, input_user) == 0x808, "InputDevice layout");
    static_assert(offsetof(InputDevice, state) == 0x91C, "InputDevice layout");

    struct DeviceManager {
        struct Table {
            void* pf0;
            char(__fastcall* get_status)(DeviceManager*, DWORD, void*, bool);
            char(__fastcall* update_time)(DeviceManager*, int, int64_t);
            void(__fastcall* set_vibration)(DeviceManager*, DWORD, XINPUT_VIBRATION*);
            void(__fastcall* reset_device)(DeviceManager*);
            void* pf3;
            int(__fastcall* add_new_device)(DeviceManager*, int);
            void(__fastcall* update_state)(DeviceManager*, int64_t, int64_t, bool);
        }* table;
        InputDevice* device[5];  // 0-3 controllers, 4 keyboard + mouse
        char buffer[0x90];
        HWND hwnd;          // 0xC0
        LARGE_INTEGER qpc;  // 0xC8
    };
    static_assert(offsetof(DeviceManager, hwnd) == 0xC0, "DeviceManager layout");

    using GetXboxUserIdFn = bool(__fastcall*)(void* self, int64_t* id, wchar_t* name, int size, int index);
    using GetProfileFn = void*(__fastcall*)(void* self, int64_t xid);
    using SetVibrationFn = void(__fastcall*)(void* self, DWORD user, XINPUT_VIBRATION* vibration);
    using GetIndexFn = int64_t(__fastcall*)(void* a1, int64_t xuid);
    using DeltaTimeFn = float(__fastcall*)(int64_t qpc);

    GetXboxUserIdFn g_origGetXboxUserId = nullptr;
    GetProfileFn g_origGetPlayerProfile = nullptr;
    GetProfileFn g_origGetGamepadMapping = nullptr;
    SetVibrationFn g_origSetVibration = nullptr;
    GetIndexFn g_origGetIndex = nullptr;

    Coop::Settings g_settings;
    bool g_hooked = false;
    int64_t g_playerId[4] = {};  // players 2-4 get made-up ids; player 1 keeps the real one

    DWORD(WINAPI* g_xinputGetState)(DWORD, XINPUT_STATE*) = nullptr;
    DWORD(WINAPI* g_xinputSetState)(DWORD, XINPUT_VIBRATION*) = nullptr;

    char* Exe() { return (char*)GetModuleHandleW(nullptr); }

    // The game manager is a static object in the exe; its first field is the function table.
    void* ManagerSelf() { return Exe() + OFFSET_MCC_GAME_MANAGER; }

    DeviceManager* Devices() { return *(DeviceManager**)(Exe() + OFFSET_MCC_DEVICE_MANAGER); }

    // Player `index`'s controller device, or null (none picked, or on K/M).
    InputDevice* Controller(int index) {
        DeviceManager* dm = Devices();
        if (!dm || index < 0 || index > 3) return nullptr;
        int pick = g_settings.controller[index];
        if ((index == 0 && g_settings.p1KeyMouse) || pick < 0 || pick > 3) return nullptr;
        return dm->device[pick];
    }

    // Player 1's real account id (asked from the game itself).
    int64_t PlayerOneId() {
        int64_t id = 0;
        return g_origGetXboxUserId && g_origGetXboxUserId(ManagerSelf(), &id, nullptr, 0, 0) ? id : 0;
    }

    void LoadXInput() {
        const char* dlls[] = {"XINPUT1_3.dll", "XINPUT1_4.dll", "XINPUT9_1_0.dll"};
        for (const char* dll : dlls) {
            HMODULE m = GetModuleHandleA(dll);
            if (!m) continue;
            g_xinputGetState = (decltype(g_xinputGetState))GetProcAddress(m, "XInputGetState");
            g_xinputSetState = (decltype(g_xinputSetState))GetProcAddress(m, "XInputSetState");
            if (g_xinputGetState) return;
        }
    }

    bool XInputState(DWORD user, XINPUT_STATE* state) {
        if (!state) return false;
        memset(state, 0, sizeof(*state));
        return MCC::ReadController(user, state) == ERROR_SUCCESS;
    }

    // --- Hooks --------------------------------------------------------------

    // Maps a player id to a local player index; the real account is player 1.
    int64_t __fastcall GetIndexDetour(void* a1, int64_t xuid) {
        if (!Coop::Active()) return g_origGetIndex(a1, xuid);
        for (int i = 1; i < 4; ++i)
            if (g_playerId[i] == xuid) return i;
        return 0;
    }

    // Players 2-4 are made-up local players; player 1 stays the real account.
    bool __fastcall GetXboxUserIdDetour(void* self, int64_t* id, wchar_t* name, int size, int index) {
        if (!Coop::Active() || index == 0) return g_origGetXboxUserId(self, id, name, size, index);
        static int s_logged = 0;
        if (s_logged < 40)
            LOG("Co-op: game asked for player %d's identity (%d)%s", index + 1, ++s_logged,
                index < 0 || index >= g_settings.players || index > 3 ? ": not playing" : "");
        if (index < 0 || index >= g_settings.players || index > 3) return false;
        if (id) *id = g_playerId[index];
        if (name && size >= 4) swprintf(name, (size_t)(size >> 1), L"Player %d", index + 1);
        return true;
    }

    void __fastcall SetVibrationDetour(void* self, DWORD user, XINPUT_VIBRATION* vibration) {
        if (!Coop::Active()) {
            g_origSetVibration(self, user, vibration);
            return;
        }
        if ((int)user >= g_settings.players || !vibration) return;
        InputDevice* device = Controller((int)user);
        if (device && g_xinputSetState) g_xinputSetState((DWORD)device->input_user, vibration);
    }

    // Everyone plays with player 1's settings and button layout.
    int g_profileCalls = 0;

    void* __fastcall GetPlayerProfileDetour(void* self, int64_t xid) {
        if (!Coop::Active()) return g_origGetPlayerProfile(self, xid);
        if (g_profileCalls < 12) LOG("Co-op: profile requested for %llx -> player 1's (%d)", (long long)xid, ++g_profileCalls);
        return g_origGetPlayerProfile(self, PlayerOneId());
    }

    int g_mappingCalls = 0;

    void* __fastcall GetGamepadMappingDetour(void* self, int64_t xid) {
        if (!Coop::Active()) return g_origGetGamepadMapping(self, xid);
        if (g_mappingCalls < 12) LOG("Co-op: button layout requested for %llx -> player 1's (%d)", (long long)xid, ++g_mappingCalls);
        return g_origGetGamepadMapping(self, PlayerOneId());
    }

    bool Hook(void* target, void* detour, void** original, const char* name) {
        auto exe = Exe();
        auto nt = (IMAGE_NT_HEADERS*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
        if (!target || (char*)target < exe || (char*)target >= exe + nt->OptionalHeader.SizeOfImage) {
            LOG("Co-op: %s not found (%p)", name, target);
            return false;
        }
        if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
            LOG("Co-op: failed to hook %s at %p", name, target);
            return false;
        }
        return true;
    }

    // --- Code patches -------------------------------------------------------

    struct PatchDef {
        uint32_t offset;  // from the title DLL's base
        const char* bytes;
        uint8_t size;
        bool blackBar;  // a screen-border fix rather than a join patch
    };

    const char kBar1[] = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x3F\x01";
    const char kBar2[] = "\x00\x00\x00\x00\x00\x00\x00\x3F\x00\x00\x80\x3F\x00\x00\x80\x3F\x01";
    const char kNops6[] = "\x90\x90\x90\x90\x90\x90";
    const char kXorRet[] = "\x31\xC0\xC3\x90";

    // Offsets are for MCC 1.3528.0.0 (from AlphaRing's lib/game/inc/1.3528.0.0).
    const PatchDef kHalo1[] = {{0x67492, "\xEB\x18", 2, false}, {0x427978, "\xEB", 1, false}, {0x85428, kNops6, 6, false}};
    const PatchDef kHalo2[] = {{0x6A6C80, "\x31\xC0\xB0\x01\xC3\x90", 6, false},
                               {0x8940CA, "\x04", 1, false},
                               {0x894127, "\x04", 1, false},
                               {0x5153E, "\x83\xF8\x01\x74\x04", 5, false}};
    const PatchDef kHalo3[] = {{0x11DF8, kXorRet, 4, false},
                               {0x8AE150, kBar1, 17, true},
                               {0x8AE164, kBar2, 17, true},
                               {0x8AE1A0, kBar1, 17, true}};
    // AlphaRing ships Halo 4's black-bar patches switched off, so they're left out here.
    const PatchDef kHalo4[] = {{0x56671C, kXorRet, 4, false}, {0x4E311B, "\xEB", 1, false}, {0x3F5D7, kNops6, 6, false}};
    const PatchDef kOdst[] = {{0x1258C, kXorRet, 4, false},
                              {0x8F1FB0, kBar1, 17, true},
                              {0x8F1FC4, kBar2, 17, true},
                              {0x8F2000, kBar1, 17, true}};
    const PatchDef kReach[] = {{0x397354, kXorRet, 4, false},
                               {0x394A2C, "\xEB", 1, false},
                               {0xB43CE0, kBar1, 17, true},
                               {0xB43CF4, kBar2, 17, true},
                               {0xB43D30, kBar1, 17, true}};

    struct Title {
        const wchar_t* dll;
        const char* name;
        const PatchDef* patches;
        size_t count;
    };
    const Title kTitles[] = {
        {L"halo1.dll", "Halo: CE", kHalo1, _countof(kHalo1)},
        {L"halo2.dll", "Halo 2", kHalo2, _countof(kHalo2)},
        {L"halo3.dll", "Halo 3", kHalo3, _countof(kHalo3)},
        {L"halo4.dll", "Halo 4", kHalo4, _countof(kHalo4)},
        {L"halo3odst.dll", "Halo 3: ODST", kOdst, _countof(kOdst)},
        {L"haloreach.dll", "Halo: Reach", kReach, _countof(kReach)},
    };

    struct LivePatch {
        const PatchDef* def;
        uint8_t* address;
        uint8_t original[17];
        bool applied;
    };
    struct LoadedTitle {
        HMODULE module;
        const Title* title;
        std::vector<LivePatch> patches;
    };

    std::mutex g_mutex;
    std::vector<LoadedTitle> g_loaded;

    int ReadWord(const char* address) {
        __try {
            return *(const uint16_t*)address;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return -2;
        }
    }

    bool Write(uint8_t* dst, const void* src, size_t size) {
        DWORD old;
        if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(dst, src, size);
        VirtualProtect(dst, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), dst, size);
        return true;
    }

    void SetApplied(LivePatch& p, bool on) {
        if (p.applied == on) return;
        if (Write(p.address, on ? (const void*)p.def->bytes : (const void*)p.original, p.def->size)) p.applied = on;
    }
}  // namespace

Coop::Settings& Coop::Get() { return g_settings; }

void Coop::Reset() {
    g_settings = Settings();
    LOG("Co-op: settings reset");
}

void Coop::AssignDefaultControllers() {
    // With K/M, player 1 has no controller so players 2-4 take controllers 1-3.
    for (int i = 0; i < 4; ++i) g_settings.controller[i] = g_settings.p1KeyMouse ? (i ? i - 1 : 4) : i;
}

std::string Coop::Serialize() {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "enabled=%d\nplayers=%d\np1KeyMouse=%d\nremoveBlackBars=%d\ncontroller1=%d\ncontroller2=%d\n"
             "controller3=%d\ncontroller4=%d\n",
             g_settings.enabled, g_settings.players, g_settings.p1KeyMouse, g_settings.removeBlackBars,
             g_settings.controller[0], g_settings.controller[1], g_settings.controller[2], g_settings.controller[3]);
    return buf;
}

void Coop::Parse(const std::string& text) {
    Settings s;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (name == "enabled") s.enabled = v != 0;
        else if (name == "players") s.players = v < 1 ? 1 : (v > 4 ? 4 : v);
        else if (name == "p1KeyMouse") s.p1KeyMouse = v != 0;
        else if (name == "removeBlackBars") s.removeBlackBars = v != 0;
        else if (name.size() == 11 && name.compare(0, 10, "controller") == 0 && name[10] >= '1' && name[10] <= '4')
            s.controller[name[10] - '1'] = v < 0 ? 0 : (v > 4 ? 4 : v);
    }
    g_settings = s;
}

bool Coop::InstallHooks(void** table) {
    if (g_hooked) return true;
    if (!table) return false;

    LoadXInput();

    int64_t guid[2];
    CoCreateGuid((GUID*)guid);
    int64_t base = guid[0] ^ guid[1];
    for (int i = 0; i < 4; ++i) g_playerId[i] = base + i;

    bool ok =
        Hook(table[MCC_GAME_MANAGER_GET_XBOX_USER_ID_SLOT], (void*)&GetXboxUserIdDetour, (void**)&g_origGetXboxUserId,
             "get_xbox_user_id") &&
        Hook(table[MCC_GAME_MANAGER_GET_PLAYER_PROFILE_SLOT], (void*)&GetPlayerProfileDetour,
             (void**)&g_origGetPlayerProfile, "get_player_profile") &&
        Hook(table[MCC_GAME_MANAGER_GET_GAMEPAD_MAPPING_SLOT], (void*)&GetGamepadMappingDetour,
             (void**)&g_origGetGamepadMapping, "retrieve_gamepad_mapping") &&
        Hook(table[MCC_GAME_MANAGER_SET_VIBRATION_SLOT], (void*)&SetVibrationDetour, (void**)&g_origSetVibration,
             "set_vibration") &&
        Hook(Exe() + OFFSET_MCC_GET_INDEX_BY_XUID, (void*)&GetIndexDetour, (void**)&g_origGetIndex,
             "get_index_by_xuid");

    g_hooked = ok;
    LOG("Co-op: hooks %s", ok ? "installed" : "FAILED (co-op unavailable)");
    return ok;
}

bool Coop::Active() { return g_hooked && g_settings.enabled; }

void Coop::OnModuleLoad(HMODULE module) {
    for (const Title& t : kTitles) {
        if (GetModuleHandleW(t.dll) != module) continue;

        LoadedTitle loaded{module, &t, {}};
        for (size_t i = 0; i < t.count; ++i) {
            LivePatch p{};
            p.def = &t.patches[i];
            p.address = (uint8_t*)module + p.def->offset;
            memcpy(p.original, p.address, p.def->size);
            loaded.patches.push_back(p);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < g_loaded.size(); ++i)  // reloaded: the old image's patches are gone with it
            if (g_loaded[i].title == &t) {
                g_loaded.erase(g_loaded.begin() + i);
                break;
            }
        g_loaded.push_back(std::move(loaded));
        LOG("Co-op: %s loaded, %d patches ready", t.name, (int)t.count);
        return;
    }
}

void Coop::OnModuleUnload(HMODULE module) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_loaded.size(); ++i)
        if (g_loaded[i].module == module) {
            g_loaded.erase(g_loaded.begin() + i);
            return;
        }
}

void Coop::ReloadSettings() {
    struct Engine {
        struct Table {
            void* fn[3];
            void*(__fastcall* set_event)(Engine*, int, void*);
        }* table;
    };
    auto engine = *(Engine**)(Exe() + OFFSET_MCC_GAME_ENGINE);
    if (!engine || !engine->table || !engine->table->set_event) return;
    __try {
        engine->table->set_event(engine, MCC_ENGINE_EVENT_LOAD_SETTING, nullptr);
        LOG("Co-op: asked the game to reload player settings");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("Co-op: reloading player settings faulted");
    }
}

void Coop::Sync() {
    // When who is playing changes, make the game re-read everyone's settings (half a second later, so the
    // players are in place first).
    static ULONGLONG s_reloadAt = 0;
    static unsigned s_signature = 0;
    unsigned sig = Active() ? 1u + (unsigned)g_settings.players * 2u + (g_settings.p1KeyMouse ? 16u : 0u) : 0u;
    for (int i = 0; i < 4; ++i) sig = sig * 5u + (unsigned)g_settings.controller[i];
    if (sig != s_signature) {
        s_signature = sig;
        s_reloadAt = GetTickCount64() + 500;
    }
    // A mission or title just started with co-op on: reload once its players are in (2 s).
    MCC::Running running = MCC::GetRunning();
    static HMODULE s_module = nullptr;
    static bool s_inGame = false;
    if (running.inGame && (!s_inGame || running.module != s_module) && Active()) s_reloadAt = GetTickCount64() + 2000;
    s_inGame = running.inGame;
    s_module = running.module;
    if (s_reloadAt && GetTickCount64() >= s_reloadAt) {
        s_reloadAt = 0;
        if (g_hooked && running.inGame) ReloadSettings();
    }

    bool on = Active();
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& l : g_loaded) {
        if (l.title->dll == kTitles[0].dll && running.inGame && running.module == l.module) {
            // Halo: CE keeps its local player count here (set when the mission starts).
            static int s_ceCount = -1;
            int count = ReadWord((char*)l.module + 0x1B7B910);
            if (count != s_ceCount) {
                s_ceCount = count;
                LOG("Co-op: Halo: CE local player count is now %d", count);
            }
        }
    }
    for (auto& l : g_loaded)
        for (auto& p : l.patches) SetApplied(p, on && (!p.def->blackBar || g_settings.removeBlackBars));
}

bool Coop::GetKeyState(void* self, DWORD index, void* input, GetKeyStateFn original) {
    static bool s_seen[4] = {};
    if (index < 4 && !s_seen[index]) {
        s_seen[index] = true;
        LOG("Co-op: the game is reading input for player %u (%s)", index + 1,
            (int)index < g_settings.players ? "playing" : "not playing");
    }
    if ((int)index >= g_settings.players) return false;  // that player isn't playing
    DeviceManager* dm = Devices();
    if (!dm || index > 3) return original(self, index, input);

    InputDevice* device;
    float deltaTime = 0;
    if (g_settings.p1KeyMouse && index == 0) {
        device = dm->device[4];
        dm->table->update_state(dm, 0, 0, false);
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        int64_t d1 = qpc.QuadPart - dm->qpc.QuadPart;
        int64_t d2 = dm->qpc.QuadPart - qpc.QuadPart;
        auto deltaFn = (DeltaTimeFn)(Exe() + OFFSET_MCC_DELTA_TIME);
        float ms = deltaFn(d1 >= d2 ? d2 : d1) * 1000.0f;
        deltaTime = ms < 0.1f ? 0.1f : (ms > 1000.0f ? 1000.0f : ms);
        dm->qpc = qpc;
    } else {
        device = Controller((int)index);
        if (!device) return true;  // no controller picked: this player does nothing
        XInputState((DWORD)device->input_user, &device->state);
    }
    if (!device) return false;

    bool result = device->methods->set_state(device, deltaTime, input) != 0;
    device->methods->check(device);
    return result;
}
