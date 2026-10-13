#pragma once

// In-game overlay: hooks the D3D11 swap chain and draws an ImGui menu.
//   Open:  LT + right stick click, or F6
//   Close: B, or F6
// While the menu is open the game gets no input.
namespace Overlay {
    bool InstallHooks();
}
