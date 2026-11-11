// SPDX-License-Identifier: MIT
#include "plugin/SteamVrSmoothingRequest.hpp"
#include "core/Log.hpp"

#include <Windows.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace mevr::host::steamvr_smoothing {
namespace {
std::mutex gMutex;
std::string gStatus;
std::optional<bool> gPending;
bool gWorking = false;

std::string Run(bool off) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&StatusText), &self);
    wchar_t path[32768]{};
    DWORD const length = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return "Не удалось найти папку ASI.";
    auto const setup = std::filesystem::path(path).parent_path() / L"MEVR-Setup.exe";
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader = nullptr, writer = nullptr;
    if (!CreatePipe(&reader, &writer, &security, 0)) return "Не удалось прочитать результат MEVR Setup.";
    SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    // Inherit only the output pipe, never the game's other inheritable handles.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    bool const initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) != FALSE;
    bool const updated = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &writer, sizeof writer, nullptr, nullptr);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = writer;
    startup.StartupInfo.hStdError = writer;
    std::wstring command = L"\"" + setup.wstring() + L"\" --steamvr-smoothing " + (off ? L"off" : L"default");
    PROCESS_INFORMATION process{};
    bool const started = updated && CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        setup.parent_path().c_str(), &startup.StartupInfo, &process);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(writer);
    if (!started) { CloseHandle(reader); return "Не удалось запустить ASI\\MEVR-Setup.exe; запустите Setup после выхода из игры."; }
    CloseHandle(process.hThread);
    DWORD const waited = WaitForSingleObject(process.hProcess, 30000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 3);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        CloseHandle(reader);
        return "MEVR Setup не ответил; сглаживание не подтверждено. Попробуйте ещё раз.";
    }
    DWORD code = 3;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    char buffer[2048]{};
    DWORD count = 0;
    ReadFile(reader, buffer, static_cast<DWORD>(sizeof buffer - 1), &count, nullptr);
    CloseHandle(reader);
    std::string line(buffer, count);
    if (auto const end = line.find_first_of("\r\n"); end != std::string::npos) line.resize(end);
    // The player reads the note; the log keeps Setup's own line.
    mevr::log::Info("steamvr smoothing: MEVR Setup --steamvr-smoothing {} exited {}: {}", off ? "off" : "default", code, line);
    if (code == 0) {
        return off ? "Выключено для этой игры. SteamVR применил изменение." : "Возвращено к вашей настройке SteamVR.";
    }
    if (line.find("not running") != std::string::npos) {
        return "SteamVR не запущен, ничего не изменилось. Повторите в VR.";
    }
    return "Не удалось изменить в SteamVR. Повторите; причина в журнале.";
}
} // namespace

void Request(bool off) {
    std::lock_guard const lock(gMutex);
    gPending = off;
    gStatus = "Прошу SteamVR изменить сглаживание движения...";
    if (gWorking) return;
    gWorking = true;
    try { std::thread([] {
        for (;;) {
            bool next = false;
            {
                std::lock_guard const guard(gMutex);
                if (!gPending) { gWorking = false; return; }
                next = *gPending;
                gPending.reset();
            }
            std::string result;
            try { result = Run(next); }
            catch (...) { result = "MEVR Setup завершился с ошибкой; сглаживание не подтверждено. Попробуйте ещё раз."; }
            std::lock_guard const guard(gMutex);
            if (!gPending) gStatus = std::move(result);
        }
    }).detach(); }
    catch (...) {
        gWorking = false;
        gPending.reset();
        gStatus = "Не удалось запустить фоновый запрос; сглаживание не подтверждено. Попробуйте ещё раз.";
    }
}

std::string StatusText() {
    std::lock_guard const lock(gMutex);
    return gStatus;
}
} // namespace mevr::host::steamvr_smoothing
