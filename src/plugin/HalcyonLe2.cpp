// SPDX-License-Identifier: MIT
#include "plugin/HalcyonLe2.hpp"

#include "core/CallQueue.hpp"
#include "core/FixedText.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"
#include "engine/SettingsFile.hpp"
#include "halcyon/le2/d3d_capture.h"
#include "halcyon/le2/dxgi_proxy.h"
#include "halcyon/le2/logger.h"
#include "halcyon/le2/mevr_bridge.h"
#include "plugin/CrashBreadcrumbs.hpp"

#include <MinHook.h>

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace mevr::host::halcyon_le2 {
namespace {

// ---- [Debug] openxrRuntime ------------------------------------------------------------------------------------------

std::wstring gRuntimeManifest;  // what XR_RUNTIME_JSON names ("" when unset), read at attach

// [Debug] openxrRuntime = <runtime manifest .json>: the statically linked loader takes XR_RUNTIME_JSON over the machine's
// active runtime (HKLM, admin to change), as the LE1 host does (plugin/HalcyonBridge). The fake headset's SteamVR null
// driver needs SteamVR's manifest while the active runtime may be another's; tools/fake-headset.ps1 sets the key.
void PinRuntime() {
    std::filesystem::path const settings = engine::SettingsFile();
    wchar_t manifest[1024] = {};
    if (!settings.empty() &&
        GetPrivateProfileStringW(L"Debug", L"openxrRuntime", L"", manifest, 1024, settings.c_str()) > 0 && manifest[0] != 0) {
        if (GetFileAttributesW(manifest) != INVALID_FILE_ATTRIBUTES) {
            SetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest);
            log::Info("openxr runtime: this process uses the manifest [Debug] openxrRuntime names: {}",
                ini::Utf8(std::filesystem::path(manifest)));
        } else {
            log::Warn("openxr runtime: [Debug] openxrRuntime names a file that isn't there, the active runtime stays: {}",
                ini::Utf8(std::filesystem::path(manifest)));
        }
    }
    wchar_t current[1024] = {};
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", current, 1024) > 0) {
        gRuntimeManifest = current;
    }
}

bool NamesSteamVr(std::wstring path) {
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return path.find(L"steamvr") != std::wstring::npos || path.find(L"steamxr") != std::wstring::npos;
}

// ---- ProcessEvent off the game thread, kept for it (research/36) -------------------------------------------------

constexpr std::size_t kCapacity = 64;
call_queue::Queue<kCapacity> gQueue;

// LE2's UObject: the GObjects index at +0x08 and the class at +0x50 (LExSDKv2's LE2 Core_classes.hpp, the same as LE1's,
// research/34 section 2) [verify against LE2's code when its reflection layout is read, docs/le2-from-le1.md 3.2].
constexpr std::size_t kObjectIndex = 0x08;
constexpr std::size_t kObjectClass = 0x50;

struct ObjectArray {
    void** data;
    std::int32_t count;
    std::int32_t max;
};

std::uintptr_t ModuleBase() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

// In GObjects at its own index: an object the engine destroyed leaves the array (its slot cleared or reused).
bool Listed(void const* object) {
    auto const* const objects = reinterpret_cast<ObjectArray const*>(ModuleBase() + engine::addresses::kGObjects);
    std::int32_t const index = *reinterpret_cast<std::int32_t const*>(static_cast<std::uint8_t const*>(object) + kObjectIndex);
    return index >= 0 && index < objects->count && objects->data != nullptr && objects->data[index] == object;
}

void const* ClassOf(void const* object) {
    return *reinterpret_cast<void const* const*>(static_cast<std::uint8_t const*>(object) + kObjectClass);
}

bool Describe(void* object, void const*& objectClass) {
    __try {
        if (!Listed(object)) {
            return false;
        }
        objectClass = ClassOf(object);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool StillAlive(call_queue::Call const& c) {
    __try {
        return Listed(c.object) && Listed(c.function) && ClassOf(c.object) == c.objectClass;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using ProcessEventFn = void(__fastcall*)(void*, void*, void*, void*);

// UObject::ProcessEvent at the address Halcyon's code calls (our host's hook sits on it, so every handler sees the call).
bool Run(call_queue::Call& c) {
    __try {
        reinterpret_cast<ProcessEventFn>(ModuleBase() + engine::addresses::kProcessEvent)(c.object, c.function,
            c.size > 0 ? c.parms : nullptr, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint64_t gRun = 0;
std::uint64_t gSkipped = 0;
std::uint64_t gFailed = 0;
bool gToldFirst = false;
ULONGLONG gNextStatsMs = 0;

} // namespace

void Attach() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Attach), &self);
    PinRuntime();  // before its device hook asks which runtime this is (the SteamVR device-flag fix)

    ME2VR::Log::Line("=====================================================");
    ME2VR::Log::Line("ME2VR (Halcyon) inside ME VR Attempt's LE2MEVR.asi");
    ME2VR::Log::Line(std::string("[BUILDSTAMP] compiled ") + __DATE__ + " " + __TIME__);
    ME2VR::Log::Line("=====================================================");

    MH_STATUS const status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log::Error("halcyon le2: MinHook failed to initialise ({}); VR renderer off", static_cast<int>(status));
        return;
    }
    // Upstream runs this in DllMain, before the engine reads GamerSettings.ini; our attach is later, so its render
    // resolution and depth of field may only apply from the next launch (as for LE1: plugin/Halcyon).
    ME2VR::D3DCapture::ArmResolutionSpoof();
    ME2VR::D3DCapture::InstallDisplayQueryHooks();
    // Not upstream's InstallBreakpointProbes (diagnostic, disabled at this pin anyway) nor its finders' probes.
    if (!ME2VR::Dxgi::EnsureLoaded()) {
        log::Error("halcyon le2: the system dxgi.dll didn't load; VR renderer off");
        return;
    }
    ME2VR::Dxgi::InstallRealFactoryHooks();
    log::Info("halcyon le2: renderer attached (its log: MELE2VR_Log.txt; its settings: MELE2VR.ini next to MassEffect2.exe)");
}

void Tick() {
    if (gQueue.Pushed() == 0) {
        return;
    }
    static call_queue::Call calls[kCapacity];
    std::size_t const n = gQueue.TakeAll(calls);
    for (std::size_t i = 0; i < n; ++i) {
        call_queue::Call& c = calls[i];
        if (!gToldFirst) {
            gToldFirst = true;
            char buffer[512];
            text::Appender name(buffer, sizeof(buffer));
            crash::AppendObject(name, c.function);
            log::Info("halcyon le2: a ProcessEvent from another thread ({}) was queued and runs at the next engine tick "
                      "(script must run on the game thread only; research/36)", name.View());
        }
        if (!StillAlive(c)) {
            ++gSkipped;
            continue;
        }
        if (Run(c)) {
            ++gRun;
        } else {
            ++gFailed;
        }
    }
    ULONGLONG const now = GetTickCount64();
    if (now >= gNextStatsMs) {
        gNextStatsMs = now + 60000;
        log::Info("halcyon le2: game thread calls: {} queued from other threads, {} run, {} skipped (object gone), {} faulted, "
                  "{} dropped (queue full), {} refused (parameters too big)",
            gQueue.Pushed(), gRun, gSkipped, gFailed, gQueue.Dropped(), gQueue.Refused());
    }
}

} // namespace mevr::host::halcyon_le2

namespace ME2VR::MevrBridge {

bool OnGameThread() noexcept {
    std::uint32_t const game = mevr::host::crash::gGameThread.load(std::memory_order_relaxed);
    return game != 0 && mevr::breadcrumbs::ThreadId() == game;
}

bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept {
    namespace h = mevr::host::halcyon_le2;
    void const* cls = nullptr;
    if (object == nullptr || function == nullptr || !h::Describe(object, cls)) {
        return false;
    }
    return h::gQueue.Push(object, function, cls, params, size);
}

bool XrRuntimeIsSteamVr() noexcept {
    namespace h = mevr::host::halcyon_le2;
    if (!h::gRuntimeManifest.empty()) {
        return h::NamesSteamVr(h::gRuntimeManifest);
    }
    HKEY key = nullptr;
    bool steam = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t path[1024] = {};
        DWORD bytes = sizeof(path) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type, reinterpret_cast<LPBYTE>(path), &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            steam = h::NamesSteamVr(path);
        }
        RegCloseKey(key);
    }
    return steam;
}

} // namespace ME2VR::MevrBridge
