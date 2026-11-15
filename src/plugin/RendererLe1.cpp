// SPDX-License-Identifier: MIT
// The renderer interface (plugin/Renderer.hpp) on LE1: Halcyon's LE1 renderer (src/halcyon/le1). Each function forwards
// to what the host's modules called directly before the interface existed, so LE1 behaves exactly as before.

#include "plugin/Renderer.hpp"

#include "halcyon/le1/vr_menu.h"

namespace mevr::host::renderer {

bool OwnMenuOpen() noexcept {
    return MELEVR::Menu::IsOpen();
}

} // namespace mevr::host::renderer
