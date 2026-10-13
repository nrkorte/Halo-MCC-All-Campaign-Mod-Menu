#include "gamepad.h"

#include <algorithm>
#include <cstdlib>

#include "imgui.h"

#include "core/log.h"
#include "mcc/mcc.h"

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

static XInputGetStateFn g_getState = nullptr;

struct Pad {
    bool connected = false;
    ULONGLONG nextRetry = 0;  // polling an empty slot is slow, so back off
    XINPUT_GAMEPAD now{};
};

static Pad g_pads[XUSER_MAX_COUNT];
static WORD g_held = 0;
static WORD g_pressed = 0;
static BYTE g_leftTrigger = 0;
static BYTE g_rightTrigger = 0;
static SHORT g_stickX = 0;  // right stick: scrolls the menu
static SHORT g_stickY = 0;
static uint32_t g_heldMask = 0;
static uint32_t g_pressedMask = 0;

void Gamepad::Init() {
    // MCC imports xinput1_3; fall back to whatever else is available.
    const wchar_t* dlls[] = {L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* dll : dlls) {
        HMODULE module = GetModuleHandleW(dll);
        if (!module) module = LoadLibraryW(dll);
        if (!module) continue;
        g_getState = (XInputGetStateFn)GetProcAddress(module, "XInputGetState");
        if (g_getState) {
            LOG("Controller input via %ls", dll);
            return;
        }
    }
    LOG("XInput not found; controller menu input unavailable");
}

// Largest-magnitude value wins, so an idle second pad can't cancel the first.
static SHORT Stronger(SHORT a, SHORT b) { return abs(b) > abs(a) ? b : a; }

void Gamepad::Poll() {
    WORD held = 0;
    BYTE lt = 0, rt = 0;
    SHORT lx = 0, ly = 0;

    if (g_getState) {
        ULONGLONG now = GetTickCount64();
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            Pad& pad = g_pads[i];
            if (!pad.connected && now < pad.nextRetry) continue;

            XINPUT_STATE state{};
            pad.connected = MCC::ReadController(i, &state) == ERROR_SUCCESS;
            if (!pad.connected) {
                pad.nextRetry = now + 1000;
                continue;
            }

            pad.now = state.Gamepad;
            held |= pad.now.wButtons;
            lt = std::max(lt, pad.now.bLeftTrigger);
            rt = std::max(rt, pad.now.bRightTrigger);
            lx = Stronger(lx, pad.now.sThumbRX);
            ly = Stronger(ly, pad.now.sThumbRY);
        }
    }

    g_pressed = held & ~g_held;
    g_held = held;
    g_leftTrigger = lt;
    g_rightTrigger = rt;
    g_stickX = lx;
    g_stickY = ly;

    uint32_t mask = held;
    if (lt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) mask |= 1u << 16;
    if (rt > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) mask |= 1u << 17;
    g_pressedMask = mask & ~g_heldMask;
    g_heldMask = mask;
}

uint32_t Gamepad::HeldMask() { return g_heldMask; }
uint32_t Gamepad::PressedMask() { return g_pressedMask; }

bool Gamepad::Pressed(WORD button) {
    return (g_pressed & button) != 0;
}

bool Gamepad::AnyHeld() {
    return g_held != 0 || g_leftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
           g_rightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
}

bool Gamepad::Held(WORD button) {
    return (g_held & button) != 0;
}

bool Gamepad::RightTriggerHeld() {
    return g_rightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
}

static float Axis(SHORT value) {
    const float dead = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;
    float v = value / 32767.0f;
    if (v > dead) return (v - dead) / (1.0f - dead);
    if (v < -dead) return (v + dead) / (1.0f - dead);
    return 0.0f;
}

void Gamepad::RightStick(float& x, float& y) {
    x = Axis(g_stickX);
    y = Axis(g_stickY);
}

void Gamepad::FeedImGui(ImGuiIO& io, bool menuOpen, uint32_t selectMask) {
    WORD b = menuOpen ? g_held : 0;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (b & XINPUT_GAMEPAD_DPAD_UP) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, menuOpen && (g_heldMask & selectMask) != 0);  // ImGui's "activate"

    // The right stick scrolls the menu (ImGui scrolls with its "left stick"
    // keys, so it is fed in as those). The left stick does nothing here.
    auto axis = [&](ImGuiKey key, SHORT value, int sign) {
        float v = menuOpen ? (float)value * sign / 32767.0f : 0.0f;
        const float dead = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;
        v = v > dead ? (v - dead) / (1.0f - dead) : 0.0f;
        io.AddKeyAnalogEvent(key, v > 0.1f, v);
    };
    axis(ImGuiKey_GamepadLStickUp, g_stickY, +1);
    axis(ImGuiKey_GamepadLStickDown, g_stickY, -1);
    axis(ImGuiKey_GamepadLStickLeft, g_stickX, -1);
    axis(ImGuiKey_GamepadLStickRight, g_stickX, +1);
}
