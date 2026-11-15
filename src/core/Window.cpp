// SPDX-License-Identifier: MIT
#include "core/Window.hpp"

#include "core/Log.hpp"

namespace mevr::window {

void BringToForeground(HWND window) {
    HWND const foreground = GetForegroundWindow();
    if (window == nullptr || foreground == window) {
        return;
    }
    DWORD const foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
    DWORD const thisThread = GetCurrentThreadId();
    bool const attached = foregroundThread != 0 && foregroundThread != thisThread &&
        AttachThreadInput(thisThread, foregroundThread, TRUE);
    BOOL const ok = SetForegroundWindow(window);
    if (attached) {
        AttachThreadInput(thisThread, foregroundThread, FALSE);
    }
    log::Info("game window to foreground: {}", ok ? "ok" : "refused by Windows");
}

bool KeyDownInGame(int vk) {
    if (vk <= 0 || (GetAsyncKeyState(vk) & 0x8000) == 0) {
        return false;
    }
    HWND const foreground = GetForegroundWindow();
    DWORD process = 0;
    if (foreground != nullptr) {
        GetWindowThreadProcessId(foreground, &process);
    }
    return process == GetCurrentProcessId();
}

} // namespace mevr::window
