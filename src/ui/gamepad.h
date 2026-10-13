#pragma once

#include <windows.h>
#include <xinput.h>

#include <cstdint>

struct ImGuiIO;

// Reads controllers straight from XInput (so it still works while game input
// is blocked). Inputs from all connected pads are combined.
namespace Gamepad {
    void Init();
    void Poll();  // once per frame

    bool Pressed(WORD button);       // XINPUT_GAMEPAD_* went down this frame
    bool AnyHeld();                  // any button or trigger is down
    bool Held(WORD button);          // XINPUT_GAMEPAD_* is down
    bool RightTriggerHeld();         // fire

    // Buttons as a mask: bits 0-15 = XINPUT_GAMEPAD_*, 16 = left trigger,
    // 17 = right trigger (Menu::Binding's controller inputs).
    uint32_t HeldMask();
    uint32_t PressedMask();          // went down this frame

    // Right stick with deadzone removed, -1..1 (y up = forward).
    void RightStick(float& x, float& y);

    // Menu navigation: D-pad moves, the right stick scrolls, `selectMask` (X by default)
    // activates. Back and close are handled by the overlay.
    void FeedImGui(ImGuiIO& io, bool menuOpen, uint32_t selectMask);
}
