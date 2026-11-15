// SPDX-License-Identifier: MIT
// Our side of the seams in Halcyon's ME2 renderer (src/halcyon/le2/mevr_bridge.h): the OpenXR loader, the session's
// functions for the test loop, our controller actions, their vibration and the floor on Halcyon's session (plugin/XrHost,
// shared with LE1's bridge), the view each frame for the mod (core/vr/HostView), and the motion controllers as Xbox pad 0.
// As LE1's (plugin/HalcyonBridge.cpp). The game-thread queue and the runtime test are in plugin/HalcyonLe2.cpp. Separate
// from that file: the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "halcyon/le2/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "game/GamepadMapping.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/XrHost.hpp"

#include <openxr/openxr.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace ME2VR::MevrBridge {
namespace {

bool gToldFirstSync = false;  // Present thread
bool gLoggedView = false;     // Present thread

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

} // namespace

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept {
    (void)waitSwapchainImage;  // the frame-breakdown probe's (plugin/PerfProbe): LE1's host only so far
    (void)waitFrame;
    (void)beginFrame;
    mevr::host::testloop::WrapSessionFunctions(acquireSwapchainImage, releaseSwapchainImage, endFrame, locateViews);
    mevr::log::Info("test loop: wrapped Halcyon's ME2 xrAcquireSwapchainImage, xrReleaseSwapchainImage, xrEndFrame and "
                    "xrLocateViews (snapshots, recording, replay)");
}

void OnSessionCreated(void* instance, void* session) noexcept {
    mevr::host::testloop::OnSessionCreated(static_cast<XrSession>(session));
    if (mevr::host::xr_host::OnSessionCreated(static_cast<XrInstance>(instance), static_cast<XrSession>(session))) {
        mevr::log::Info("halcyon le2 bridge: our controller actions are attached to Halcyon's session");
    }
    gToldFirstSync = false;
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (session == nullptr) {
        return;
    }
    mevr::vr::controllers::State state;
    mevr::host::xr_host::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), state);
    bool const inputReady = mevr::host::xr_host::InputReady();
    // The test loop records the live sample, or replaces it with a recording's (plugin/TestLoop), as on LE1.
    mevr::host::testloop::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), mevr::host::xr_host::InputFocused(), state);
    if (!inputReady && !state.active) {
        return;  // no controllers, and no replay standing in for them
    }
    mevr::vr::controllers::Publish(state);
    if (inputReady && !gToldFirstSync) {
        gToldFirstSync = true;
        mevr::log::Info("halcyon le2 bridge: first controller sync (session {}focused)",
            mevr::host::xr_host::InputFocused() ? "" : "not ");
    }
}

void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU, float const headQ[4],
    float const headP[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU, float unitsPerMeter,
    bool floorWorld) noexcept {
    (void)floorWorld;  // the floor lock (plugin/FloorLock) isn't in the LE2 host yet: the view goes out uncorrected
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {headQ[0], headQ[1], headQ[2], headQ[3]};
    view.head.position = {headP[0], headP[1], headP[2]};
    // As LE1's: Halcyon's head look yaw is + when the view turns left (its DriveHeadLook takes yawSign -1 of the head's
    // left-positive yaw); ours (UE rotators) + right. Pitch is + up in both. [verify] on ME2 with a replayed turn.
    view.lookYaw = -lookYawUU;
    view.lookPitch = lookPitchUU;
    view.offsetForward = offsetForwardUU;
    view.offsetRight = offsetRightUU;
    view.offsetUp = offsetUpUU;
    view.unitsPerMeter = unitsPerMeter;
    mevr::vr::host_view::Publish(view);
    if (worldView && !gLoggedView) {
        gLoggedView = true;
        mevr::log::Info("halcyon le2 bridge: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU, offsetUpUU, unitsPerMeter,
            headP[0], headP[1], headP[2]);
    }
}

void OnRecentered() noexcept {
    mevr::host::xr_host::NoteRecentered();
    mevr::log::Info("halcyon le2 bridge: recentered");
}

void OnSessionStopped() noexcept {
    mevr::vr::host_view::Reset();
    mevr::log::Info("halcyon le2 bridge: the session stopped");
}

DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || state == nullptr) {
        return result;
    }
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) {
        return result;
    }
    bool const realPad = result == ERROR_SUCCESS;
    std::lock_guard lock(gPadMutex);
    XINPUT_GAMEPAD const pad = mevr::game::gamepad::Merge(realPad ? state->Gamepad : XINPUT_GAMEPAD{},
        mevr::game::gamepad::FromControllers(controllers, gLatches));
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

} // namespace ME2VR::MevrBridge
