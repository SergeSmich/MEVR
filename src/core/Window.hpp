// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

namespace mevr::window {

// The headset session gaining focus doesn't give the game window Windows focus, and UE3 ignores
// keyboard, mouse and gamepad without it. Windows only lets the foreground thread hand out
// focus, so this briefly shares its input queue. Any thread.
void BringToForeground(HWND window);

// The key is down and a window of this process (the game's, or its console) is in front: a hotkey typed in another
// program (Discord, a browser) must not recenter, open a menu or switch anything (both renderers' KeyDown seams, the
// voice key). Any thread.
bool KeyDownInGame(int vk);

} // namespace mevr::window
