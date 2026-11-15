// SPDX-License-Identifier: MIT
#pragma once

// What the host's modules need from the VR renderer, whichever game's it is (docs/le2-continuation-plan.md M2.2): LE1's
// Halcyon renderer (src/halcyon/le1; plugin/RendererLe1.cpp forwards to MELEVR::MevrBridge and its siblings) or Halcyon's
// ME2 renderer (src/halcyon/le2; plugin/RendererLe2.cpp forwards to ME2VR::). One code path for both games: a host module
// that asks the renderer something asks it here, never through a renderer's own headers, so the same module compiles into
// either game's host. The other direction, the renderer telling the host (a frame begun, the session's functions
// resolved), stays in each renderer's bridge (plugin/HalcyonBridge.cpp, plugin/HalcyonLe2Bridge.cpp), which calls the
// same game-independent host functions (testloop::WrapSessionFunctions, testloop::OnFrameBegun ...).
//
// Grown as modules move onto it; research/le2-renderer-integration.md "The renderer interface" lists what is here, what
// each game answers and what is still LE1-only.
namespace mevr::host::renderer {

// The renderer's own developer menu is open: Halcyon's ImGui menu (LE1: Ctrl or Shift + \; ME2: its menu key, Insert
// upstream). Not our main menu (plugin/MainMenu). Any thread.
bool OwnMenuOpen() noexcept;

} // namespace mevr::host::renderer
