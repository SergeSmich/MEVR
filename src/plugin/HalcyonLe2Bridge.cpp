// SPDX-License-Identifier: MIT
// Our side of the OpenXR and pad seams in Halcyon's ME2 renderer (src/halcyon/le2/mevr_bridge.h): the OpenXR loader,
// controller actions on Halcyon's session, and the motion controllers as Xbox pad 0. As LE1's first integration
// (2026-10-08, plugin/HalcyonBridge.cpp at 9074cea8). The game-thread queue and the runtime test are in
// plugin/HalcyonLe2.cpp. Separate from that file: the OpenXR SDK's headers and Halcyon's xr_types.h define the same names.

#include "halcyon/le2/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/xr/Input.hpp"
#include "game/GamepadMapping.hpp"

#include <openxr/openxr.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace ME2VR::MevrBridge {
namespace {

mevr::xr::Input gInput;
mevr::xr::Input::Target gTarget;  // Present thread
bool gInputFailed = false;
bool gToldFirstSync = false;

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

} // namespace

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

void OnSessionCreated(void* instance, void* session) noexcept {
    if (gInput.Matches(gTarget)) {
        gInput.Release(gTarget);  // a new session (upstream makes one per process, but be safe)
    }
    gTarget.instance = static_cast<XrInstance>(instance);
    gTarget.session = static_cast<XrSession>(session);
    ++gTarget.instanceGeneration;
    ++gTarget.sessionGeneration;
    gInputFailed = !gInput.Create(gTarget);
    if (gInputFailed) {
        mevr::log::Error("halcyon le2 bridge: controller actions failed; motion controllers unavailable");
    } else {
        mevr::log::Info("halcyon le2 bridge: our controller actions are attached to Halcyon's session");
    }
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (gInputFailed || session == nullptr || static_cast<XrSession>(session) != gTarget.session) {
        return;
    }
    gTarget.base = static_cast<XrSpace>(space);  // recentering replaces it
    mevr::vr::controllers::State state;
    gInput.Sync(gTarget, static_cast<XrTime>(predictedDisplayTime), state);
    mevr::vr::controllers::Publish(state);
    if (!gToldFirstSync) {
        gToldFirstSync = true;
        mevr::log::Info("halcyon le2 bridge: first controller sync (session {}focused)", gInput.Focused() ? "" : "not ");
    }
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
