// SPDX-License-Identifier: MIT
// The renderer interface (plugin/Renderer.hpp) on LE2: Halcyon's ME2 renderer (src/halcyon/le2), forwarding to its own
// functions or to its seams (src/halcyon/le2/mevr_bridge.h). Where ME2's renderer has nothing that answers yet, the
// function says so in its comment.

#include "plugin/Renderer.hpp"

#include "halcyon/le2/me2_menu.h"

namespace mevr::host::renderer {

bool OwnMenuOpen() noexcept {
    return ME2VR::Menu::IsOpen();
}

} // namespace mevr::host::renderer
