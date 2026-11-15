// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's ME2 renderer, as
// src/halcyon/le1/mevr_bridge.h does for LE1. Declared without the OpenXR SDK headers, because xr_types.h defines the
// same names; the implementation is in src/plugin/HalcyonLe2Bridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

namespace ME2VR::MevrBridge
{
// The statically linked OpenXR loader's xrGetInstanceProcAddr (upstream loads openxr_loader.dll, which our ASI doesn't
// ship). Our controller actions must come from the same loader instance.
void* StaticXrGetInstanceProcAddr() noexcept;

// The session functions are resolved (me2_xr.cpp's ResolveFunctions; each argument is the address of a g_fn slot): we
// may replace a slot with a wrapper that calls the real function (plugin/TestLoop: the snapshots' image copies, the
// frame's layers, the head recorded or replayed). As LE1's MevrBridge::WrapSessionFunctions.
void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept;

// The session and its spaces exist (Present thread): create and attach our controller actions.
void OnSessionCreated(void* instance, void* session) noexcept;

// After a successful xrBeginFrame (Present thread): sync the controller actions and publish their state, with poses
// located in `space` (Halcyon's LOCAL app space, re-origined at recenter: the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// From Halcyon's XInput hooks, right after the real read: merges the motion controllers into pad 0 (Touch -> Xbox
// mapping, game/GamepadMapping) before Halcyon's own pad handling (menu block, move-follows-head).
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;

// UnrealScript runs on the game thread only (research/36): the VM keeps its state in process-wide globals, so a script
// call from the Present thread races the game thread's script. True on the engine tick's thread.
bool OnGameThread() noexcept;
// Off the game thread: queues `function` on `object` with a copy of its `size`-byte parameter block for the start of the
// next engine tick, where it runs if both are still in GObjects. True if queued. Results and out parameters are never
// filled in: for setters only.
bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept;

// The OpenXR runtime this process uses is SteamVR's: XR_RUNTIME_JSON if set (our [Debug] openxrRuntime, the fake
// headset; set at attach), else the machine's active runtime.
bool XrRuntimeIsSteamVr() noexcept;

// The key is down and a window of the game is in front (LE1's MevrBridge::KeyDown): a hotkey typed in another program
// (Discord, a browser) must not recenter, switch profiles or open the menu.
bool KeyDown(int vk) noexcept;

// Halcyon's hotkey polls go through this: KeyDown in our ASI, the plain poll in an upstream build.
inline bool HotkeyDown(int vk) noexcept
{
#if defined(MEVR_ASI)
    return KeyDown(vk);
#else
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#endif
}
}
