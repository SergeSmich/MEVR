// SPDX-License-Identifier: MIT
#pragma once

// The host's side of the renderer's OpenXR session, for either game's renderer (docs/le2-continuation-plan.md M2.3): our
// controller actions on Halcyon's session (core/xr/Input), their vibration (core/vr/Haptics: the mod's pulses and
// continuous buzz, HostServices v9/v10), and the head's height over the floor (core/xr/Floor, STAGE or LOCAL_FLOOR,
// HostServices v10). LE1's bridge (plugin/HalcyonBridge.cpp) and ME2's (plugin/HalcyonLe2Bridge.cpp) call the same
// functions from their seams; until 2026-11-14 this lived inside LE1's bridge, and ME2's had a copy of the input part.

#include "core/vr/Controllers.hpp"
#include "core/vr/Floor.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <string>

namespace mevr::host::xr_host {

// ---- The renderer's frame thread --------------------------------------------------------------------------------------
// The renderer's session exists (before its frames): our actions created and attached on it (a session we had before is
// let go first), a floor space of its own, vibration from scratch. False if the actions failed: no motion controllers
// (logged), though a replay can still stand in for them (plugin/TestLoop).
bool OnSessionCreated(XrInstance instance, XrSession session) noexcept;
// After the renderer's xrBeginFrame: our actions synced with poses in `base` (the renderer's app space, which its
// recenter replaces), pending vibrations applied, the floor measured. `state` is the live sample (inactive without
// actions). Does nothing for another session than the last one created.
void OnFrameBegun(XrSession session, XrSpace base, XrTime time, vr::controllers::State& state) noexcept;
// Our actions exist on the current session.
bool InputReady() noexcept;
// The session has input focus (the runtime's answer at the last sync).
bool InputFocused() noexcept;
// The interaction profile the runtime bound for a hand (0 left, 1 right), "" if none or no actions.
std::string InteractionProfile(int hand) noexcept;
// The renderer re-origined its app space at the head: the floor's recenter count goes up (vr::floor::State::recenters).
void NoteRecentered() noexcept;

// ---- Any thread ---------------------------------------------------------------------------------------------------------
// A pulse on a hand (0 left, 1 right; frequency 0: the runtime's), applied at the next frame.
void RequestHaptic(int hand, float seconds, float amplitude, float frequencyHz = 0.0f) noexcept;
// A continuous buzz renewed each call, ended `seconds` after the last one.
void RequestContinuousHaptic(int hand, float amplitude, float frequencyHz, float seconds) noexcept;
// Every pending and continuous vibration stopped (the mod went away).
void ForgetHaptics() noexcept;
// The latest floor sample.
void Floor(vr::floor::State* out) noexcept;

} // namespace mevr::host::xr_host
