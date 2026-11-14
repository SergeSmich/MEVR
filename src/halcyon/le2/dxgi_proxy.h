// SPDX-License-Identifier: GPL-3.0-only
// Modified for ME VR Attempt (GPL-3.0) since 2026-11-14: changes are marked [MEVR].
// Upstream: jrush64/MassEffect2-VR @ 4784e90 (Copyright (C) 2026 Halcyon).
#pragma once

#include <Windows.h>

namespace ME2VR::Dxgi
{
bool EnsureLoaded() noexcept;
FARPROC Resolve(const char* name) noexcept;
HRESULT MissingExport(const char* name) noexcept;
// [MEVR] in our ASI: hooks the system dxgi.dll's CreateDXGIFactory/1/2 (the proxy exports are compiled out).
void InstallRealFactoryHooks() noexcept;
}
