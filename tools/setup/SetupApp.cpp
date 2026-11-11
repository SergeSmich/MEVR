// SPDX-License-Identifier: MIT
#include "SetupApp.hpp"

#include "XrProbe.hpp"
#include "core/Json.hpp"
#include "install/GameSettings.hpp"
#include "install/Http.hpp"
#include "install/Launch.hpp"
#include "install/Packs.hpp"
#include "install/ReportMarks.hpp"
#include "install/Updates.hpp"
#include "install/Voice.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <memory>
#include <system_error>
#include <thread>

namespace mevr::setup {
namespace {

namespace fs = std::filesystem;
namespace detect = install::detect;
namespace packs = install::packs;

std::string OnOff(bool on) {
    return on ? "Вкл" : "Выкл";
}

// Russian counts: 1 файл, 2 файла, 5 файлов (11-14 take the many form).
std::string CountText(std::size_t n, char const* one, char const* few, char const* many) {
    std::size_t const tail = n % 100;
    char const* form = tail >= 11 && tail <= 14 ? many : n % 10 == 1 ? one : n % 10 >= 2 && n % 10 <= 4 ? few : many;
    return std::to_string(n) + " " + form;
}

bool CountIsOne(std::size_t n) {
    return n % 10 == 1 && n % 100 != 11;
}

// The in-game menu's key: menuKey = 220 (VK_OEM_5) in every MELEVR.ini slot (install/Ini FixHalcyonIni).
constexpr UINT kMenuKeyVk = 220;

// A key's name on this keyboard layout (GetKeyNameText, as the in-game menu's KeyName): VK_OEM_5 is \ on a US
// keyboard and ^ on a German one. Empty if the layout has no such key.
std::string KeyName(UINT vk) {
    UINT const scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scan == 0) {
        return {};
    }
    wchar_t name[64]{};
    int const n = GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64);
    if (n <= 0) {
        return {};
    }
    return detect::Utf8(std::wstring(name, static_cast<std::size_t>(n)));
}

// "the \ key", "the ^ key", or a description when the layout has no name for it.
std::string MenuKeyText() {
    std::string const name = KeyName(kMenuKeyVk);
    return name.empty() ? std::string("клавиша меню (\\ на раскладке США)") : "клавиша " + name;
}

// The source labels as the pages show them. status_.source keeps the English values: the report and --status read them.
std::string SourceText(std::string const& source) {
    if (source == "Folder") {
        return "Папка";
    }
    if (source == "this copy's game folder") {
        return "папка этой копии";
    }
    return source;
}

// The pages' row for a game never started here (no GamerSettings.ini yet).
std::string FirstStartText(bool installed, bool pending) {
    if (installed && pending) {
        return "Настройки VR, выбранные при установке, ждут файла настроек игры. Запустите игру один раз: установщик дождётся "
               "этого файла, вы выйдете в главное меню игры, и настройки применятся сами.";
    }
    return "Без файла настроек игры настройки VR не применить, и VR запустится в окне с рамкой и сломанной картинкой. Запустите "
           "игру один раз, до или после установки: установщик дождётся файла, вы выйдете в главное меню игры, и настройки применятся "
           "сами.";
}

// The Repair page's name for a file it would put back.
std::string RepairItemName(std::string const& relative) {
    std::string const file = detect::Utf8(fs::path(detect::Wide(relative)).filename().wstring());
    if (relative == install::paths::kBink) {
        return file + " (ASI-загрузчик)";
    }
    if (relative == install::paths::kToc) {
        return file + " (запись кабины)";
    }
    return file;
}

} // namespace

App::App(Options options, Host host) : options_(std::move(options)), host_(std::move(host)) {
    version_ = install::updates::kVersion;
}

App::~App() {
    cancel_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool App::Bind(Rml::Context* context) {
    Rml::DataModelConstructor c = context->CreateDataModel("setup");
    if (!c) {
        return false;
    }
    if (auto pack = c.RegisterStruct<PackView>()) {
        pack.RegisterMember("id", &PackView::id);
        pack.RegisterMember("name", &PackView::name);
        pack.RegisterMember("author", &PackView::author);
        pack.RegisterMember("description", &PackView::description);
        pack.RegisterMember("size", &PackView::size);
        pack.RegisterMember("licence", &PackView::licence);
        pack.RegisterMember("requirements", &PackView::requirements);
        pack.RegisterMember("state", &PackView::state);
        pack.RegisterMember("file", &PackView::file);
        pack.RegisterMember("installed", &PackView::installed);
        pack.RegisterMember("large", &PackView::large);
    }
    c.RegisterArray<std::vector<PackView>>();
    if (auto line = c.RegisterStruct<LogLine>()) {
        line.RegisterMember("text", &LogLine::text);
        line.RegisterMember("error", &LogLine::error);
    }
    c.RegisterArray<std::vector<LogLine>>();
    if (auto file = c.RegisterStruct<ReportFileView>()) {
        file.RegisterMember("name", &ReportFileView::name);
        file.RegisterMember("size", &ReportFileView::size);
        file.RegisterMember("note", &ReportFileView::note);
    }
    c.RegisterArray<std::vector<ReportFileView>>();
    c.Bind("packs", &packs_);
    c.Bind("log_lines", &log_);
    c.Bind("report_files", &reportFiles_);
    struct Flag {
        char const* name;
        bool* value;
    };
    for (Flag const& f : {Flag{"game_ok", &gameOk_}, Flag{"version_ok", &versionOk_}, Flag{"xr_ok", &xrOk_}, Flag{"loader_ok", &loaderOk_},
             Flag{"installed", &installed_}, Flag{"ready", &ready_}, Flag{"can_install", &canInstall_}, Flag{"has_payload", &hasPayload_},
             Flag{"admin_needed", &adminNeeded_}, Flag{"busy", &busy_}, Flag{"waiting", &waiting_}, Flag{"has_gamer_settings", &hasGamerSettings_},
             Flag{"settings_applied", &settingsApplied_}, Flag{"textures_marked", &texturesMarked_}, Flag{"mem_ready", &memReady_},
             Flag{"headset_known", &headsetKnown_}, Flag{"update_enabled", &updateEnabled_}, Flag{"opt_settings", &optSettings_},
             Flag{"opt_vsync", &optVsync_}, Flag{"opt_blur", &optBlur_}, Flag{"opt_grain", &optGrain_}, Flag{"opt_hdr", &optHdr_},
             Flag{"keep_settings", &keepSettings_}, Flag{"confirm_open", &confirmOpen_}, Flag{"confirm_danger", &confirmDanger_},
             Flag{"upstream_found", &upstreamFound_}, Flag{"gamer_read_only", &gamerReadOnly_}, Flag{"remove_local", &removeLocal_},
             Flag{"report_snapshot", &reportSnapshot_}, Flag{"report_perf", &reportPerf_}, Flag{"report_ready", &reportReady_},
             Flag{"crash_offer", &crashOffer_}, Flag{"report_has_zip", &reportHasZip_}, Flag{"settings_pending", &settingsPending_},
             Flag{"repair_needed", &repairNeeded_}, Flag{"can_cancel", &canCancel_}, Flag{"hero_enabled", &heroEnabled_},
             Flag{"voice_installed", &voiceInstalled_}, Flag{"voice_small", &voiceSmall_}, Flag{"voice_medium", &voiceMedium_},
             Flag{"voice_llm", &voiceLlm_}, Flag{"voice_ready", &voiceReady_}, Flag{"voice_warn", &voiceWarn_},
             Flag{"voice_up_to_date", &voiceUpToDate_}}) {
        c.Bind(f.name, f.value);
    }
    struct Text {
        char const* name;
        std::string* value;
    };
    for (Text const& t : {Text{"page", &page_}, Text{"version", &version_}, Text{"chip_game", &chipGame_}, Text{"chip_xr", &chipXr_},
             Text{"chip_installed", &chipInstalled_}, Text{"install_title", &installTitle_}, Text{"install_button", &installButton_}, Text{"hero_title", &heroTitle_}, Text{"hero_text", &heroText_},
             Text{"game_title", &gameTitle_}, Text{"game_text", &gameText_}, Text{"version_title", &versionTitle_},
             Text{"version_text", &versionText_}, Text{"xr_title", &xrTitle_}, Text{"xr_text", &xrText_}, Text{"loader_title", &loaderTitle_},
             Text{"loader_text", &loaderText_}, Text{"loader_plan", &loaderPlan_}, Text{"root_text", &rootText_},
             Text{"source_text", &sourceText_}, Text{"update_text", &updateText_}, Text{"update_url", &updateUrl_}, Text{"quality", &quality_},
             Text{"quality_text", &qualityText_}, Text{"headset_text", &headsetText_}, Text{"cur_res", &curRes_}, Text{"cur_window", &curWindow_},
             Text{"cur_hdr", &curHdr_}, Text{"cur_vsync", &curVsync_}, Text{"cur_blur", &curBlur_}, Text{"cur_grain", &curGrain_},
             Text{"repair_title", &repairTitle_}, Text{"repair_text", &repairText_}, Text{"uninstall_text", &uninstallText_},
             Text{"launch_text", &launchText_}, Text{"footer_text", &footerText_}, Text{"wait_text", &waitText_},
             Text{"request_text", &requestText_}, Text{"mem_text", &memText_}, Text{"progress_text", &progressText_},
             Text{"confirm_title", &confirmTitle_}, Text{"confirm_text", &confirmText_}, Text{"confirm_button", &confirmButton_},
             Text{"upstream_text", &upstreamText_}, Text{"local_text", &localText_}, Text{"report_text", &reportText_},
             Text{"report_redactions", &reportRedactions_}, Text{"report_headline", &reportHeadline_}, Text{"report_zip", &reportZip_},
             Text{"report_request", &reportRequest_}, Text{"hero_action", &heroAction_}, Text{"hero_button", &heroButton_},
             Text{"launch_advice", &launchAdvice_}, Text{"menu_key", &menuKey_}, Text{"first_start_text", &firstStartText_},
             Text{"voice_state", &voiceState_}, Text{"voice_size", &voiceSize_}, Text{"voice_sources", &voiceSources_},
             Text{"voice_licences", &voiceLicences_}, Text{"voice_button", &voiceButton_}, Text{"voice_model_text", &voiceModelText_},
             Text{"voice_parts", &voiceParts_}, Text{"cancel_text", &cancelText_}}) {
        c.Bind(t.name, t.value);
    }
    c.Bind("progress", &progress_);
    c.Bind("update_available", &updateAvailable_);
    c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& args) {
        if (args.empty()) {
            return;
        }
        Act(args[0].Get<Rml::String>(), args.size() > 1 ? args[1].Get<Rml::String>() : std::string());
    });
    handle_ = c.GetModelHandle();
    return true;
}

void App::Dirty() {
    if (handle_) {
        handle_.DirtyAllVariables();
    }
}

void App::SetPage(std::string const& page) {
    page_ = page;
    Dirty();
}

std::string App::StateText() const {
    std::string packs;
    for (PackView const& p : packs_) {
        packs += std::format("pack {}: {}{}\n", p.id, p.state, p.installed ? " (installed)" : "");
    }
    return std::format("page: {}\ngame: {}\ngame ok: {}\ninstalled: {}\nrequest text: {}\nwaiting: {}\nwait text: {}\nmem ready: {}\n{}"
                       "voice: {}{}\nvoice size: {}\n", page_,
        gameOk_ ? detect::Utf8(status_.root.wstring()) : std::string("none"), gameOk_ ? 1 : 0, installed_ ? 1 : 0, requestText_, waiting_ ? 1 : 0,
        waitText_, memReady_ ? 1 : 0, packs, voiceState_, voiceInstalled_ ? " (installed)" : "", voiceSize_);
}

bool App::SteamGame() const {
    // Found through Steam, or a picked folder inside a Steam library.
    std::wstring path = status_.root.wstring();
    std::transform(path.begin(), path.end(), path.begin(), ::towlower);
    return status_.source == "Steam" || path.find(L"\\steamapps\\common\\") != std::wstring::npos;
}

void App::SetGame(fs::path const& root, std::string const& source) {
    status_ = install::ReadStatus(root, source);
    voiceChoicesRead_ = false;  // another game: its voice files decide the card's choices again
}

void App::Start() {
    payload_ = install::FindPayload(options_.exe.parent_path());
    // The game: --game; this copy's own game (it lives in Game\ME1\Binaries\Win64\ASI); the installs we can find.
    if (options_.gameRoot) {
        SetGame(detect::RootFromPicked(*options_.gameRoot).value_or(*options_.gameRoot), "Folder");
    } else {
        fs::path const asiDir = options_.exe.parent_path();
        // This copy's own game: ...\Game\ME1\Binaries\Win64\ASI (Steam, EA app) or ...\ME1\Binaries\Win64\ASI (a repack).
        std::optional<fs::path> own;
        if (_wcsicmp(asiDir.filename().c_str(), L"ASI") == 0) {
            own = detect::RootFromPicked(asiDir);
        }
        if (own) {
            std::error_code ec;
            bool const steam = fs::exists(install::Layout{*own}.Full(install::paths::kSteamAppId), ec);
            SetGame(*own, steam ? "Steam" : "this copy's game folder");
        } else {
            auto const installs = detect::FindInstalls();
            if (!installs.empty()) {
                SetGame(installs.front().root, installs.front().source);
            }
        }
    }
    xr_ = detect::ActiveRuntime();
    if (auto const log = install::ReadFile(detect::HalcyonLogPath())) {
        if (auto const size = detect::EyeSizeFromHalcyonLog(*log)) {
            headsetWidth_ = size->width;
            headsetHeight_ = size->height;
        }
    }
    updateEnabled_ = !install::updates::LatestReleaseApi(options_.repo).empty();
    updateText_ = updateEnabled_ ? "Проверить новую версию. Скачать и установить обновления можно здесь, с сохранением ваших настроек."
                             : "Страница релизов ещё не задана (MEVR-Setup.ini, [Update] repo).";
    if (options_.request) {
        // "Install after I quit" (plugin/SetupRequest): the game wrote the request and started us as it exited.
        page_ = "content";
        requestText_ = "Выберите, что добавить: файлы голосовых приказов или набор текстур.";
        if (auto const text = install::ReadFile(install::RequestFile())) {
            if (auto const request = install::ParseSetupRequest(*text)) {
                std::string names;
                for (std::string const& id : request->packs) {
                    if (packs::Pack const* p = packs::Find(id)) {
                        names += names.empty() ? p->name : std::string(", ") + p->name;
                    } else if (id == install::voice::kContent) {
                        names += names.empty() ? install::voice::kName : std::string(", ") + install::voice::kName;
                    }
                }
                if (!names.empty()) {
                    requestText_ = "Вы просили: " + names + ".";
                }
            }
        }
        std::error_code ec;
        fs::remove(install::RequestFile(), ec);
    }
    if (options_.waitPid != 0) {
        waiting_ = true;
        waitText_ = options_.finishUpdate ? "Скачанный установщик ждёт, пока закроется предыдущий установщик." : "Изменения начнутся после выхода из игры.";
    }
    // Report a problem: a request the game left (its setup copy wasn't there to answer it), or a crash since the last
    // report and the last "no".
    {
        namespace marks = install::report_marks;
        fs::path const local = install::LocalDataDir();
        std::error_code ec;
        if (!local.empty() && fs::exists(marks::RequestFile(local), ec)) {
            if (auto const text = install::ReadFile(marks::RequestFile(local))) {
                if (auto const request = json::Parse(*text)) {
                    reportSnapshot_ = request->Bool("snapshot");
                    reportPerf_ = request->Bool("perf");
                }
            }
            fs::remove(marks::RequestFile(local), ec);
            reportRequest_ = "Вы запросили отчёт из меню игры. Проверьте, что в него войдёт, и создайте его.";
            page_ = "report";
        }
        crashOffer_ = marks::CrashOffer(local);
    }
    Refresh();
}

install::report::Options App::ReportOptions() const {
    install::report::Options o;
    o.gameRoot = gameOk_ ? status_.root : fs::path();
    o.store = SteamGame() ? "Steam" : status_.source == "EA app" ? "EA app" : std::string();
    o.snapshot = reportSnapshot_;
    o.perf = reportPerf_;
    return o;
}

void App::GatherReport() {
    if (busy_ || sample_) {
        return;
    }
    auto bundle = std::make_shared<install::report::Bundle>();
    install::report::Options const options = ReportOptions();
    RunJob("Собираем содержимое отчёта", [bundle, options] { *bundle = install::report::Collect(options); });
    jobDone_ = [this, bundle] {
        bundle_ = bundle;
        reportReady_ = true;
        ShowBundle();
    };
}

void App::ShowBundle() {
    reportFiles_.clear();
    if (!bundle_) {
        return;
    }
    install::report::Bundle const& b = *bundle_;
    reportFiles_.push_back({"REPORT.md", "~" + install::report::SizeText(install::report::BuildReportMd(b).size()),
        "Сводка: система, что пошло не так, изменённые настройки, запрос для вашего ИИ-ассистента"});
    std::uint64_t total = 0;
    for (install::report::File const& f : b.files) {
        total += f.bytes.size();
        std::string note = f.note;
        if (f.redactions > 0) {
            note += std::format("{}{} скрыто", note.empty() ? "" : "; ", f.redactions);
        }
        reportFiles_.push_back({f.name, install::report::SizeText(f.bytes.size()), note});
    }
    install::report::Redactions const& r = b.redactions;
    reportRedactions_ = std::format("{}: имя пользователя Windows {} раз, имя компьютера {}, папка профиля {}, адреса e-mail "
                                    "{}. В архиве они читаются как <user>, <computer>, <profile>, <email>.",
        CountText(r.Total(), "скрытие", "скрытия", "скрытий"), r.userName, r.computerName, r.profilePath, r.email);
    reportHeadline_ = install::report::Headline(b);
    reportText_ = std::format("{}, {} до сжатия.{}", CountText(b.files.size() + 1, "файл", "файла", "файлов"),
        install::report::SizeText(total),
        b.missing.empty() ? std::string() : " На этом ПК отсутствует: " + [&] {
            std::string m;
            for (std::string const& x : b.missing) {
                m += (m.empty() ? "" : ", ") + x;
            }
            return m;
        }() + ".");
    Dirty();
}

void App::Refresh() {
    if (!status_.root.empty()) {
        status_ = install::ReadStatus(status_.root, status_.source);
    }
    xrOk_ = !xr_.kind.empty();
    hasPayload_ = payload_.has_value();
    gameOk_ = status_.gameFound;
    versionOk_ = status_.versionCheck == detect::VersionCheck::Supported;
    installed_ = status_.installed;
    adminNeeded_ = gameOk_ && !status_.canWrite;
    hasGamerSettings_ = status_.hasGamerSettings;
    gamerReadOnly_ = status_.gamerSettingsReadOnly;
    upstreamFound_ = gameOk_ && status_.upstream.Found();
    upstreamText_ = upstreamFound_
        ? std::string("Его dxgi.dll") + (status_.upstream.openxrLoader ? " и openxr_loader.dll лежат" : " лежит") +
              " в Binaries\\Win64. В этот мод встроен рендерер Halcyon, а вдвоём они работали бы оба, поэтому установка " +
              "переименует " + (status_.upstream.openxrLoader ? "их" : "его") +
              " в *.mevr-off. Удаление вернёт на место. Его MELEVR.ini остаётся. Устанавливая, вы соглашаетесь."
        : std::string();
    settingsApplied_ = status_.gameSettings;
    texturesMarked_ = status_.texturesMarked;
    bool const binkUsable = status_.bink != detect::BinkState::Missing && status_.bink != detect::BinkState::Broken;
    loaderOk_ = gameOk_ && binkUsable;
    canInstall_ = gameOk_ && versionOk_ && binkUsable && hasPayload_ && !adminNeeded_;
    ready_ = gameOk_ && versionOk_ && binkUsable;
    headsetKnown_ = headsetWidth_ > 0;

    chipGame_ = gameOk_ ? "Mass Effect 1 LE" : "Игра не найдена";
    chipXr_ = xrOk_ ? xr_.kind : "Нет среды VR";
    chipInstalled_ = installed_ ? "Установлено " + status_.installedVersion : "Не установлено";
    // Install's label: it puts this setup's version in place, so it's an update only when that is newer.
    std::string const mine = install::updates::kVersion;
    std::string const theirs = status_.installedVersion;
    int const order = installed_ && !theirs.empty() ? install::updates::CompareVersions(theirs, mine) : 0;
    if (!installed_) {
        installTitle_ = "Готово";
        installButton_ = "Установить";
    } else if (theirs.empty()) {
        installTitle_ = std::format("Установлено: переустановите, чтобы поставить версию этого установщика ({})", mine);
        installButton_ = "Переустановить";
    } else if (order < 0) {
        installTitle_ = std::format("Установлено {}: этот установщик обновит до {}", theirs, mine);
        installButton_ = "Обновить";
    } else if (order == 0) {
        installTitle_ = std::format("Установлено {}, та же версия, что у этого установщика: переустановите, чтобы заменить файлы", theirs);
        installButton_ = "Переустановить";
    } else {
        installTitle_ = std::format("Установлено {} — новее этого установщика ({}): установка откатит до {}", theirs, mine, mine);
        installButton_ = std::format("Установить {}", mine);
    }
    rootText_ = gameOk_ ? detect::Utf8(status_.root.wstring()) : "Папка игры не выбрана";
    sourceText_ = status_.source.empty() ? "пока нет" : SourceText(status_.source);
    settingsPending_ = status_.pendingSettings.has_value();
    menuKey_ = MenuKeyText();
    launchAdvice_ = install::launch::runtime_start::PlanFor(xr_.kind, xr_.manifest).advice;
    firstStartText_ = FirstStartText(installed_, settingsPending_);
    gameTitle_ = gameOk_ ? "Найдено" : "Не найдено";
    gameText_ = gameOk_ ? SourceText(status_.source) + ": " + detect::Utf8(status_.root.filename().wstring()) : "Выберите папку на странице «Добро пожаловать»";
    versionTitle_ = status_.version ? status_.version->ToString() : "Неизвестно";
    versionText_ = versionOk_ ? "Версия, под которую собран мод"
        : gameOk_ ? "Моду нужна версия 2.0.0.48602, на любой другой он не включается. Обновите игру в Steam или EA app."
                  : "";
    xrTitle_ = xrOk_ ? xr_.kind : "Нет";
    xrText_ = xrOk_ ? (xr_.name.empty() ? std::string("Активная среда OpenXR") : xr_.name)
                    : "Активная среда OpenXR не найдена: установите SteamVR, приложение Meta или Virtual Desktop и сделайте её активной";
    switch (status_.bink) {
    case detect::BinkState::Original: loaderTitle_ = "Пока нет"; break;
    case detect::BinkState::Ours: loaderTitle_ = "Загрузчик MEVR"; break;
    case detect::BinkState::LEBinkProxy: loaderTitle_ = "ME3Tweaks'"; break;
    case detect::BinkState::OtherProxy: loaderTitle_ = "Другой прокси"; break;
    case detect::BinkState::Missing:
    case detect::BinkState::Broken: loaderTitle_ = gameOk_ ? "Сломан" : "-"; break;
    }
    loaderText_ = gameOk_ ? detect::BinkStateText(status_.bink) : "";
    loaderPlan_ = status_.bink == detect::BinkState::LEBinkProxy ? "ASI-загрузчик ME3Tweaks уже установлен: он загрузит мод, добавлять ничего не нужно"
        : status_.bink == detect::BinkState::OtherProxy           ? "Установлен другой прокси bink2w64.dll: он используется как есть (он должен загружать ASI\\*.asi)"
                                                                  : "Наш: bink2w64.dll игры сохранён как bink2w64_original.dll, а наш ставится перед ним";
    install::settings::Target const target = install::settings::TargetFor(quality_, headsetWidth_, headsetHeight_);
    qualityText_ = quality_ == "keep" ? "Разрешение остаётся как есть"
        : quality_ == "auto"         ? std::format("Стартует с {} × {} на глаз. После первого сеанса VR и перезапуска берёт 85% найденного размера глаза.",
                                           target.width, target.height)
        : target.width > 0           ? std::format("{} × {} на глаз{}", target.width, target.height,
                                           quality_ == "headset" ? " (0.85 размера глаза вашего шлема; FSR доводит до него)" : quality_ == "performance" ? " (значение Halcyon по умолчанию)" : "")
                                     : "";
    headsetText_ = headsetKnown_ ? std::format("{} × {} на глаз, из последнего сеанса VR", headsetWidth_, headsetHeight_)
                                 : "Пока неизвестно: берётся из последнего сеанса VR, либо «Определить» спросит среду VR";
    // The Settings page's current values.
    if (auto const gamer = install::ReadFile(install::Layout{status_.root}.Full(install::paths::kGamerSettings)); gameOk_ && gamer) {
        install::settings::Current const now = install::settings::Read(*gamer);
        curRes_ = now.resX > 0 ? std::format("{} × {}", now.resX, now.resY) : "По умолчанию";
        curWindow_ = now.borderless ? "Без рамки" : "В окне или на весь экран";
        curHdr_ = OnOff(now.hdr);
        curVsync_ = OnOff(now.vsync);
        curBlur_ = OnOff(now.motionBlur);
        curGrain_ = OnOff(now.filmGrain);
    } else {
        curRes_ = curWindow_ = curHdr_ = curVsync_ = curBlur_ = curGrain_ = "-";
    }
    // Repair, Uninstall, Launch.
    UpdateRepairStatus();
    UpdateHero();
    {
        fs::path const local = install::LocalDataDir();
        localText_ = local.empty() ? "Не найдено."
                                   : detect::Utf8(local.wstring()) + ": снимки, записи, замеры времени кадров, отчёты о падениях, скачанные "
                                     "рабочие файлы Mass Effect Modder и наборов текстур. Выключено: они остаются.";
    }
    if (status_.hasRecord) {
        install::Transaction tx(install::Layout{status_.root});
        tx.Load();
        int added = 0, replaced = 0, edited = 0;
        for (install::FileEntry const& f : tx.Data().files) {
            added += f.action == "added" ? 1 : 0;
            replaced += f.action == "replaced" || f.action == "renamed" ? 1 : 0;
            edited += f.action == "edited" ? 1 : 0;
        }
        uninstallText_ = std::format("Удалено: {}. Возвращено как было: {}. Файлов настроек возвращено к вашим значениям: {}.",
            CountText(added, "файл", "файла", "файлов"), CountText(replaced, "файл", "файла", "файлов"), edited);
    } else {
        uninstallText_ = "Ничего не установлено.";
    }
    if (launchText_.empty()) {
        launchText_ = "Лаунчер Legendary Edition лишь запускает MassEffect1.exe с вашими настройками языка и субтитров; «Запустить» делает "
                      "то же самое напрямую, при работающем Steam.";
    }
    footerText_ = gameOk_ ? "Журнал мода: Binaries\\Win64\\Logs\\LE1MEVR.log · журнал загрузчика: Logs\\MEVRLoader.log" : "";
    std::error_code ec;
    memReady_ = fs::exists(packs::MemExe(), ec);
    memText_ = memReady_ ? "Готов: он ставит наборы текстур ниже (GPL, из собственных релизов на GitHub)."
                         : "Ставит наборы текстур (GPL, от команды ALOT). Скачивается из собственных релизов на GitHub по кнопке «Скачать».";
    UpdatePacks();
    UpdateVoice();
    Dirty();
}

void App::UpdateRepairStatus(bool reread) {
    if (sample_) {
        // The preview's stand-ins: status_.needsRepair as Sample set it.
    } else if (!installed_ || status_.root.empty()) {
        repairTitle_ = "Не установлено";
        repairText_ = "Сначала установите мод.";
        repairNeeded_ = false;
        return;
    } else if (reread) {
        // The files as they are now (ReadStatus: what Repair would put back; a loader another tool installed since isn't).
        status_.needsRepair = install::ReadStatus(status_.root, status_.source).needsRepair;
    }
    if (!installed_) {
        repairTitle_ = "Не установлено";
        repairText_ = "Сначала установите мод.";
        repairNeeded_ = false;
        return;
    }
    std::vector<std::string> items;
    for (std::string const& f : status_.needsRepair) {
        items.push_back(RepairItemName(f));
    }
    // The VR game settings chosen at install, waiting for the game's first start: Repair puts them on once the file exists.
    bool const settingsDue = settingsPending_ && hasGamerSettings_;
    if (settingsDue) {
        items.push_back("настройки VR, выбранные при установке (файл настроек игры с тех пор появился)");
    }
    repairNeeded_ = !items.empty();
    std::size_t const files = status_.needsRepair.size();
    repairTitle_ = files == 0 && !settingsDue ? "Все файлы как после установки"
        : files == 0                          ? "Настройки VR ждут"
                                              : CountText(files, "файл", "файла", "файлов") +
                                                    (CountIsOne(files) ? " требует исправления" : " требуют исправления");
    repairText_.clear();
    for (std::string const& item : items) {
        repairText_ += (repairText_.empty() ? "" : ", ") + item;
    }
    if (items.empty()) {
        repairText_ = settingsPending_ ? "Делать нечего. Настройки VR всё ещё ждут первого запуска игры (страница «Добро пожаловать»)." : "Делать нечего.";
    } else {
        repairText_ += ". «Исправить» вернёт их на место.";
    }
}

void App::UpdateHero() {
    bool const binkUsable = status_.bink != detect::BinkState::Missing && status_.bink != detect::BinkState::Broken;
    heroAction_.clear();
    heroButton_.clear();
    heroEnabled_ = false;
    if (!gameOk_) {
        heroTitle_ = "Игра не найдена";
        heroText_ = "Mass Effect Legendary Edition не найдена в Steam или EA app. Выберите её папку ниже (ту, где есть Game\\ME1).";
    } else if (!versionOk_) {
        heroTitle_ = "Неподдерживаемая версия игры";
        heroText_ = "Мод собран для MassEffect1.exe 2.0.0.48602 и на любой другой версии не включается, поэтому не установлен.";
    } else if (!binkUsable) {
        heroTitle_ = "Игру нужно исправить";
        heroText_ = detect::BinkStateText(status_.bink);
    } else if (installed_ && repairNeeded_ && !status_.needsRepair.empty()) {
        heroTitle_ = "Мод нужно исправить";
        heroText_ = "Часть его файлов изменилась или пропала после установки («проверка файлов» в Steam или EA app возвращает игре её собственный "
                    "загрузчик и оглавление, и мод отключается): " + repairText_;
        heroAction_ = "repair";
        heroButton_ = "Исправить";
        heroEnabled_ = hasPayload_;
        if (!hasPayload_) {
            heroText_ += " Запустите MEVR-Setup.exe из загрузки, чтобы исправить.";
        }
    } else if (!hasGamerSettings_) {
        heroTitle_ = installed_ ? "Остался один шаг: запустите игру один раз" : "Mass Effect 1 здесь ещё не запускалась";
        heroText_ = installed_ ? "Mass Effect 1 создаёт файл настроек при первом запуске, и настройки VR, выбранные при установке, записываются в "
                                 "него. Запустите игру один раз: установщик дождётся файла, вы выйдете в главное меню игры, и настройки применятся сами."
                               : "Игра создаёт файл настроек при первом запуске, и настройки VR записываются в него. Запустите игру один раз сейчас или "
                                 "сначала установите мод: выбранные настройки хранятся, пока файла нет.";
        heroAction_ = "first_start";
        heroButton_ = "Запустить игру один раз";
        heroEnabled_ = true;
    } else if (installed_ && settingsPending_) {
        heroTitle_ = "Остался один шаг: настройки VR";
        heroText_ = "Mass Effect 1 создала файл настроек после установки: примените настройки VR, выбранные тогда (окно без рамки в "
                    "разрешении VR; ваши значения сохранены для восстановления).";
        heroAction_ = "apply_pending";
        heroButton_ = "Применить их";
        heroEnabled_ = true;
    } else if (installed_) {
        heroTitle_ = "Готово к VR";
        heroText_ = "Мод установлен. " + launchAdvice_ + " В игре " + menuKey_ + " или оба стика, удерживаемые вместе, открывают VR-меню.";
        heroAction_ = "launch";
        heroButton_ = "Запустить";
        heroEnabled_ = true;
    } else if (upstreamFound_) {
        heroTitle_ = "Готово к установке";
        heroText_ = "MELE-VR от Halcyon установлен: установка отодвинет два его DLL (удаление вернёт их), затем поставит мод, его ASI-загрузчик и "
                    "рекомендованные настройки VR, записав всё, чтобы это можно было отменить. «Настройки» позволяют сначала выбрать параметры.";
        heroAction_ = "install";  // one click: the Install page's defaults (research/33 section 11)
        heroButton_ = "Установить";
        heroEnabled_ = canInstall_ && hasPayload_;
    } else {
        heroTitle_ = "Готово к установке";
        heroText_ = "Всё в порядке. Установка поставит мод, его ASI-загрузчик и рекомендованные настройки VR, записав всё, чтобы это можно было "
                    "отменить. «Настройки» позволяют сначала выбрать параметры.";
        heroAction_ = "install";  // one click: the Install page's defaults (research/33 section 11)
        heroButton_ = "Установить";
        heroEnabled_ = canInstall_ && hasPayload_;
    }
}

install::settings::Choices App::PendingOrModelChoices() const {
    return status_.pendingSettings ? install::ChoicesFromPending(*status_.pendingSettings) : ChoicesFromModel();
}

void App::StartFirstRun() {
    if (!gameOk_ || waiting_ || busy_) {
        return;
    }
    namespace fst = install::launch::first_start;
    install::Layout const layout{status_.root};
    bool const steam = SteamGame();
    // From the Settings page: what's chosen there; elsewhere what the install chose, if it waits.
    install::settings::Choices const choices = page_ == "settings" ? ChoicesFromModel() : PendingOrModelChoices();
    canCancel_ = true;
    RunJob(fst::PhaseText(fst::Phase::Starting), [this, layout, steam, choices] {
        auto const t0 = std::chrono::steady_clock::now();
        auto seconds = [](std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); };
        std::error_code ec;
        bool const already = detect::GameRunningIn(layout.root);
        if (already) {
            Post("Mass Effect 1 уже запущена: ждём её файл настроек и её закрытия.");
        } else {
            install::launch::Result const r = install::launch::StartLe1(layout.root, steam);
            Post(r.message, !r.started);
            if (!r.started) {
                Post(fst::PhaseText(fst::Phase::Closed), true);
                return;
            }
        }
        double lastSeen = -1.0;  // seconds since t0 when the game was last seen running
        fst::Phase phase = fst::Phase::Starting;
        fst::Phase shown = fst::Phase::Apply;  // anything but the first phase, so the first one is posted
        while (!cancel_) {
            fst::Observation o;
            o.sinceLaunchS = seconds(std::chrono::steady_clock::now() - t0);
            o.gameRunning = detect::GameRunningIn(layout.root);
            o.fileExists = fs::exists(layout.Full(install::paths::kGamerSettings), ec);
            if (o.gameRunning) {
                lastSeen = o.sinceLaunchS;
            }
            o.sinceSeenS = lastSeen < 0.0 ? -1.0 : o.sinceLaunchS - lastSeen;
            phase = fst::Next(o);
            if (phase != shown) {
                shown = phase;
                Progress(fst::PhaseText(phase), phase == fst::Phase::WaitingForQuit ? 66 : phase == fst::Phase::WaitingForFile ? 33 : -1);
                if (phase == fst::Phase::WaitingForQuit) {
                    Post(fst::PhaseText(phase));
                }
            }
            if (fst::Finished(phase)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (cancel_) {
            Post("Ожидание остановлено. Когда Mass Effect 1 создаст файл настроек и закроется, «Исправить» (или «Настройки» → «Применить») включит настройки VR.");
            return;
        }
        if (phase != fst::Phase::Apply) {
            Post(fst::PhaseText(phase), true);
            return;
        }
        Post(fst::PhaseText(phase));
        Apply(install::ApplySettings(layout, choices));
    });
}

void App::UpdateVoice() {
    namespace voice = install::voice;
    voiceSources_ = "GitHub: moonshine-ai/moonshine (v0.1.5), ggml-org/llama.cpp (b12128) и релиз этого мода " + std::string(voice::kModelTag) +
        "; Hugging Face: moonshine-ai/moonshine-voice-assets";
    voiceLicences_ = "MIT (Moonshine Voice и его модели, ONNX Runtime, llama.cpp); Apache-2.0 (модель приказов, дообученная из Qwen3-0.6B)";
    voice::Part const* modelPart = voice::Find("llm-model");
    voiceModelText_ = std::format("Понимает свободные формулировки, в которых не уверены правила: llama.cpp ({}) и наша дообученная модель ({}, релиз этого мода "
                                  "{}). Если релиз ещё не опубликован, установка всё равно поставит распознаватель речи, и голосовые приказы "
                                  "будут работать по встроенным правилам; уровень модели дождётся релиза, и «Применить» добавит её, когда он появится.",
        install::http::SizeText(voice::SizesFor({"llm-runtime"}).download), install::http::SizeText(modelPart != nullptr ? modelPart->modelSize : 0),
        voice::kModelTag);
    if (sample_) {
        return;  // the preview's sample states set the rest
    }
    bool const haveGame = gameOk_ && !status_.root.empty();
    voice::State state;
    if (haveGame) {
        state = voice::ReadState(install::Layout{status_.root});
        if (!voiceChoicesRead_) {
            // What's installed decides the choices the first time; the player's ticks after that.
            voiceChoicesRead_ = true;
            if (!state.parts.empty()) {
                voiceSmall_ = state.Has("models-small");
                voiceMedium_ = state.Has("models-medium");
                voiceLlm_ = state.Has("llm-runtime") || state.Has("llm-model");
            }
        }
    }
    voice::Choices const choices{voiceSmall_, voiceMedium_, voiceLlm_};
    std::vector<std::string> const wanted = voice::PartsFor(choices);
    std::vector<std::string> missing;
    for (std::string const& id : wanted) {
        if (!state.Has(id)) {
            missing.push_back(id);
        }
    }
    bool const extra = std::any_of(state.parts.begin(), state.parts.end(),
        [&](std::string const& id) { return std::find(wanted.begin(), wanted.end(), id) == wanted.end(); });
    voice::Sizes const fetch = voice::SizesFor(missing);
    voice::Sizes const disk = voice::SizesFor(wanted);
    voiceInstalled_ = state.Has("recognizer") && state.Has("models-tiny");
    bool const dllOk = state.dll || (payload_ && payload_->HasVoice());
    voiceReady_ = haveGame && dllOk && !waiting_;
    voiceUpToDate_ = voiceInstalled_ && missing.empty() && !extra && state.damaged.empty();
    voiceWarn_ = !state.damaged.empty() || (haveGame && !dllOk);
    voiceSize_ = std::format("{} на скачивание, {} в папке игры", fetch.download == 0 ? std::string("ничего") : install::http::SizeText(fetch.download),
        install::http::SizeText(disk.disk));
    voiceParts_.clear();
    for (std::string const& id : state.parts) {
        if (voice::Part const* p = voice::Find(id)) {
            voiceParts_ += (voiceParts_.empty() ? "" : ", ") + p->name;
        }
    }
    voiceButton_ = !voiceInstalled_ ? "Установить" : !state.damaged.empty() ? "Скачать заново" : "Применить";
    if (!haveGame) {
        voiceState_ = "Сначала найдите игру (страница «Добро пожаловать»).";
    } else if (!dllOk) {
        voiceState_ = "В этой установке нет MEVR-Voice.dll: сначала установите или исправьте этот релиз мода тем MEVR-Setup.exe из его загрузки, "
                      "затем добавьте сюда голосовые файлы.";
    } else if (!state.damaged.empty()) {
        voiceState_ = std::format("Файлов не хватает или они изменены: {}. {} скачает недостающее заново.",
            state.damaged.size(), voiceButton_);
    } else if (voiceUpToDate_) {
        voiceState_ = "Установлено: " + voiceParts_ + ". В игре прижмите левый хват у левого уха, говорите, отпустите.";
    } else if (voiceInstalled_ && voiceLlm_ && !state.Has("llm-model") && !extra && missing.size() <= 2) {
        voiceState_ = "Распознаватель речи установлен, голосовые приказы работают. Уровень модели ждёт: его релиз (voice-model-1) "
                      "ещё не был опубликован на момент установки. «Применить» добавит его, когда он появится.";
    } else if (voiceInstalled_) {
        voiceState_ = "Установлено: " + voiceParts_ + ". «Применить» приведёт в соответствие с выбором ниже.";
    } else {
        voiceState_ = "Не установлено.";
    }
}

void App::UpdatePacks() {
    packs_.clear();
    for (packs::Pack const& p : packs::Catalogue()) {
        PackView v;
        v.id = p.id;
        v.name = p.name;
        v.author = p.author;
        v.description = p.description;
        v.size = p.size;
        v.licence = p.licence;
        v.requirements = p.requirements;
        v.large = p.large;
        v.installed = std::any_of(status_.packs.begin(), status_.packs.end(), [&](install::PackRecord const& r) { return r.id == p.id; });
        for (auto const& [id, file] : packFiles_) {
            if (id == p.id) {
                v.file = detect::Utf8(file.filename().wstring());
            }
        }
        if (v.file.empty()) {
            auto const found = packs::FindDownloads(p);
            if (!found.empty()) {
                packFiles_.emplace_back(p.id, found.front());
                v.file = detect::Utf8(found.front().filename().wstring());
            }
        }
        if (v.installed) {
            v.state = "Установлено этим установщиком.";
        } else if (!v.file.empty()) {
            v.state = "Готов к установке: " + v.file;
        } else {
            v.state = "Не скачано: откройте его страницу, скачайте версию для MEM и выберите файл.";
        }
        packs_.push_back(std::move(v));
    }
}

void App::Post(std::string text, bool error) {
    std::lock_guard const lock(jobMutex_);
    jobLog_.push_back({std::move(text), error});
    jobChanged_ = true;
}

void App::Progress(std::string text, int percent) {
    std::lock_guard const lock(jobMutex_);
    jobProgress_ = std::move(text);
    if (percent >= 0) {
        jobPercent_ = percent;
    }
    jobChanged_ = true;
}

void App::Apply(install::Outcome const& outcome) {
    for (std::string const& line : outcome.log) {
        Post(line, line.rfind("error: ", 0) == 0);
    }
    Post(outcome.ok ? "Готово." : "Остановлено: смотрите ошибки выше.", !outcome.ok);
}

void App::RunJob(std::string title, std::function<void()> job) {
    if (busy_) {
        return;
    }
    busy_ = true;
    cancel_ = false;
    log_.clear();
    {
        std::lock_guard const lock(jobMutex_);
        jobLog_.clear();
        jobProgress_ = title;
        jobPercent_ = 0;
        jobChanged_ = true;
    }
    working_ = true;
    worker_ = std::thread([this, job = std::move(job)] {
        job();
        working_ = false;
    });
    Dirty();
}

void App::Tick() {
    if (page_ != shownPage_) {
        shownPage_ = page_;
        if (page_ == "repair" && !busy_) {
            UpdateRepairStatus(true);  // the files as they are now, not when the setup started
            UpdateHero();
            Dirty();
        }
        if (page_ == "report" && !busy_) {
            GatherReport();  // the preview: what the zip would hold now
        }
    }
    if (waiting_ && options_.waitPid != 0) {
        HANDLE const process = OpenProcess(SYNCHRONIZE, FALSE, options_.waitPid);
        bool const gone = process == nullptr || WaitForSingleObject(process, 0) != WAIT_TIMEOUT;
        if (process != nullptr) {
            CloseHandle(process);
        }
        if (gone) {
            waiting_ = false;
            options_.waitPid = 0;
            Refresh();
        }
    }
    if (options_.finishUpdate && !waiting_ && !busy_) {
        options_.finishUpdate = false;
        Act("finish_update",{});
    }
    bool changed = false;
    {
        std::lock_guard const lock(jobMutex_);
        if (jobChanged_) {
            jobChanged_ = false;
            log_ = jobLog_;
            progressText_ = jobProgress_;
            progress_ = jobPercent_;
            if (applyingUpdate_ && progress_ >= 90) canCancel_ = false;
            changed = true;
        }
    }
    if (busy_ && !working_ && worker_.joinable()) {
        worker_.join();
        busy_ = false;
        applyingUpdate_ = false;
        canCancel_ = false;
        cancelText_ = "Прекратить ожидание";
        {
            std::lock_guard const lock(jobMutex_);
            log_ = jobLog_;
        }
        if (jobDone_) {
            auto done = std::move(jobDone_);
            jobDone_ = {};
            done();
        }
        Refresh();
        changed = true;
    }
    if (changed) {
        Dirty();
    }
}

install::settings::Choices App::ChoicesFromModel() const {
    install::settings::Choices c;
    c.target = install::settings::TargetFor(quality_, headsetWidth_, headsetHeight_);
    c.eyeSizeAtFirstStart = quality_ == "auto";
    c.vsyncOff = optVsync_;
    c.motionBlurOff = optBlur_;
    c.filmGrainOff = optGrain_;
    c.hdrOff = optHdr_;
    return c;
}

void App::Confirm(std::string title, std::string text, std::string button, bool danger, std::function<void()> action) {
    confirmTitle_ = std::move(title);
    confirmText_ = std::move(text);
    confirmButton_ = std::move(button);
    confirmDanger_ = danger;
    confirmOpen_ = true;
    pending_ = std::move(action);
    Dirty();
}

void App::Act(std::string const& name, std::string const& arg) {
    install::Layout const layout{status_.root};
    if (name == "confirm") {
        if (!confirmOpen_) {
            return;  // a key press on a dialog that isn't open
        }
        confirmOpen_ = false;
        auto action = std::move(pending_);
        pending_ = {};
        if (action) {
            action();
        }
        Dirty();
        return;
    }
    if (name == "cancel") {
        confirmOpen_ = false;
        pending_ = {};
        Dirty();
        return;
    }
    if (name == "stop_waiting") {
        // The jobs that can be stopped: "Запустить игру один раз" waiting for the game, Launch waiting for SteamVR, the voice
        // orders' download.
        if (busy_ && canCancel_) {
            cancel_ = true;
        }
        return;
    }
    if (busy_ || confirmOpen_) {
        return;  // every button below is disabled while a job runs or a dialog is open: the keyboard can't get past that
    }
    if (name == "browse") {
        if (!host_.pickFolder) {
            return;
        }
        if (auto const picked = host_.pickFolder(L"Папка Mass Effect Legendary Edition")) {
            if (auto const root = detect::RootFromPicked(*picked)) {
                SetGame(*root, "Folder");
            } else {
                Post("В этой папке нет Mass Effect Legendary Edition (Game\\ME1\\Binaries\\Win64\\MassEffect1.exe) "
                     "или копии с ME1\\Binaries\\Win64\\MassEffect1.exe прямо в ней.", true);
                std::lock_guard const lock(jobMutex_);
                log_ = jobLog_;
            }
            Refresh();
        }
    } else if (name == "install") {
        if (!canInstall_ || waiting_ || !payload_) {
            return;
        }
        install::InstallOptions options;
        options.gameSettings = optSettings_;
        options.choices = ChoicesFromModel();
        options.setupExe = options_.exe;
        options.source = SteamGame() ? "Steam" : status_.source;
        auto const payload = *payload_;
        RunJob("Установка", [this, layout, payload, options] {
            Apply(install::Install(layout, payload, options));
            if (!install::ReadStatus(layout.root, "").hasGamerSettings && options.gameSettings) {
                Post("Остался один шаг: Mass Effect 1 ещё не запускалась на этом ПК, поэтому настройки VR ждут её файла настроек. Нажмите "
                     "«Запустить игру один раз» выше (или на странице «Добро пожаловать»).",
                    true);
            }
        });
    } else if (name == "goto_install") {
        page_ = "install";
    } else if (name == "first_start") {
        StartFirstRun();
    } else if (name == "apply_pending") {
        if (waiting_ || !hasGamerSettings_) {
            return;
        }
        auto const choices = PendingOrModelChoices();
        RunJob("Применение настроек VR", [this, layout, choices] { Apply(install::ApplySettings(layout, choices)); });
    } else if (name == "uninstall") {
        if (waiting_ || !installed_) {
            return;
        }
        bool const keep = keepSettings_;
        bool const removeLocal = removeLocal_;
        std::string text = keep ? "Убирает мод и его загрузчик и возвращает каждый изменённый файл и настройку игры. Ваши настройки VR и калибровка остаются."
                                : "Убирает мод и его загрузчик, возвращает каждый изменённый файл и настройку игры и удаляет ваши настройки VR и калибровку.";
        if (removeLocal) {
            text += " Также удаляет диагностику и загрузки мода в %LOCALAPPDATA%\\MEVR.";
        }
        Confirm("Удалить VR-мод?", text, "Удалить", true, [this, layout, keep, removeLocal] {
            RunJob("Удаление", [this, layout, keep, removeLocal] { Apply(install::Uninstall(layout, keep, options_.exe, removeLocal)); });
        });
    } else if (name == "verify") {
        if (!installed_) {
            return;
        }
        Refresh();  // UpdateRepairStatus: the files as they are now
    } else if (name == "repair") {
        if (waiting_ || !installed_ || !payload_) {
            return;
        }
        auto const payload = *payload_;
        RunJob("Исправление", [this, layout, payload] { Apply(install::Repair(layout, payload)); });
    } else if (name == "apply") {
        if (waiting_ || !hasGamerSettings_) {
            return;
        }
        auto const choices = ChoicesFromModel();
        RunJob("Применение настроек VR", [this, layout, choices] { Apply(install::ApplySettings(layout, choices)); });
    } else if (name == "restore") {
        if (waiting_ || !settingsApplied_) {
            return;
        }
        RunJob("Восстановление ваших настроек", [this, layout] { Apply(install::RestoreSettings(layout)); });
    } else if (name == "launch") {
        if (!gameOk_ || waiting_) {
            return;
        }
        if (detect::GameRunningIn(status_.root)) {
            launchText_ = "Mass Effect уже запущена.";
            Refresh();
            return;
        }
        bool const steam = SteamGame();
        fs::path const root = status_.root;
        namespace rs = install::launch::runtime_start;
        rs::Plan const plan = rs::PlanFor(xr_.kind, xr_.manifest);
        canCancel_ = plan.action == rs::Action::StartSteamVr;  // the wait for SteamVR can be cut short
        std::string const kind = xr_.kind;
        std::string const manifest = xr_.manifest;
        RunJob("Запуск Mass Effect", [this, root, steam, kind, manifest] {
            install::launch::LaunchHooks hooks;
            hooks.progress = [this](std::string const& step, int percent) { Progress(step, percent); };
            hooks.post = [this](std::string const& line, bool warning) { Post(line, warning); };
            hooks.cancelled = [this] { return cancel_.load(); };
            install::launch::Result const r = install::launch::StartLe1WithVr(root, steam, kind, manifest, hooks);
            std::lock_guard const lock(jobMutex_);
            jobProgress_ = r.message;
        });
        jobDone_ = [this] {
            launchText_.clear();
            for (LogLine const& line : log_) {
                launchText_ += (launchText_.empty() ? "" : " ") + line.text;
            }
        };
    } else if (name == "launch_steam") {
        install::launch::StartThroughSteam();
    } else if (name == "detect_headset") {
        Confirm("Спросить среду VR?",
            "«Определить» запустит среду VR (может открыться SteamVR), чтобы узнать собственное разрешение глаза шлема. Больше ничего не меняется.", "Определить", false,
            [this] {
                auto probe = std::make_shared<HeadsetProbe>();
                RunJob("Запрос к среде VR", [this, probe] {
                    *probe = ProbeHeadset();
                    if (probe->ok) {
                        Post(std::format("{} сообщает {} × {} на глаз ({})", probe->runtime, probe->width, probe->height, probe->system));
                    } else {
                        Post("Нет ответа: " + probe->error, true);
                    }
                });
                jobDone_ = [this, probe] {
                    if (probe->ok) {
                        headsetWidth_ = probe->width;
                        headsetHeight_ = probe->height;
                        quality_ = "headset";
                    }
                };
            });
    } else if (name == "check_update") {
        if (!updateEnabled_) {
            return;
        }
        if (install::updates::LatestReleaseApi(options_.repo).empty()) {
            return;
        }
        auto result = std::make_shared<install::updates::CheckResult>();
        std::string const repo = options_.repo;
        std::string const mine = status_.installedVersion.empty() ? install::updates::kVersion : status_.installedVersion;
        RunJob("Проверка обновлений", [this, repo, mine, result] {
            *result = install::updates::Check(repo,mine);
            Post(result->text, result->state == install::updates::CheckResult::State::Failed);
        });
        jobDone_ = [this, result] {
            updateText_ = result->text;
            updateUrl_ = result->page;
            updateRelease_ = result->release;
            updateAvailable_ = updateRelease_ && updateRelease_->package &&
                (!updateRelease_->package->sha256.empty() || updateRelease_->checksums);
            if (updateAvailable_) {
                updateText_ += " Скачать " + install::http::SizeText(updateRelease_->package->size) + ". Ваши настройки сохранятся.";
            } else if (updateRelease_) {
                updateText_ += " Его проверенный пакет обновления пока недоступен. Проверьте позже.";
            }
        };
    } else if (name == "install_update") {
        if (!gameOk_ || !installed_ || !updateAvailable_ || !updateRelease_ || !host_.updateInstaller) return;
        auto const release = *updateRelease_;
        auto const game = status_.root;
        auto prepared = std::make_shared<install::updates::Prepared>();
        canCancel_ = true;
        cancelText_ = "Отменить скачивание";
        RunJob("Скачивание обновления",[this,release,prepared] {
            *prepared = install::updates::Prepare(release,[this](std::string const& text,int percent) { Progress(text,percent); },&cancel_);
            if (!prepared->error.empty()) Post(prepared->error,true);
        });
        jobDone_ = [this,prepared,game] {
            if (!prepared->error.empty() || cancel_.load()) return;
            std::string const error = host_.updateInstaller(prepared->installer,game);
            if (!error.empty()) {
                Post(error,true);
                updateText_ = error + " Нажмите «Скачать и установить», чтобы повторить.";
            }
        };
    } else if (name == "finish_update") {
        page_ = "welcome";
        if (!gameOk_ || !payload_) {
            Post("Пакет обновления или выбранная папка игры отсутствует.",true);
            return;
        }
        canCancel_ = true;
        applyingUpdate_ = true;
        cancelText_ = "Отменить обновление";
        auto const payload = *payload_;
        auto const installer = options_.exe;
        RunJob("Установка обновления",[this,layout,payload,installer] {
            Apply(install::updates::Apply(layout,payload,installer,[this](std::string const& text,int percent) { Progress(text,percent); },&cancel_));
        });
    } else if (name == "voice_toggle") {
        if (arg == "small") {
            voiceSmall_ = !voiceSmall_;
        } else if (arg == "medium") {
            voiceMedium_ = !voiceMedium_;
        } else if (arg == "llm") {
            voiceLlm_ = !voiceLlm_;
        }
        UpdateVoice();
        Dirty();
    } else if (name == "voice_install") {
        if (!voiceReady_ || !gameOk_ || waiting_) {
            return;
        }
        namespace voice = install::voice;
        install::Layout const voiceLayout{status_.root};
        voice::Choices const choices{voiceSmall_, voiceMedium_, voiceLlm_};
        voice::ModelSource const model = voice::ReadModelSource(options_.exe.parent_path() / L"MEVR-Setup.ini");
        std::optional<install::Payload> const payload = payload_;
        voice::State const state = voice::ReadState(voiceLayout);
        std::vector<std::string> fetch;
        for (std::string const& id : voice::PartsFor(choices)) {
            if (!state.Has(id)) {
                fetch.push_back(id);
            }
        }
        auto run = [this, voiceLayout, payload, choices, model] {
            canCancel_ = true;
            cancelText_ = "Остановить скачивание";
            RunJob("Установка голосовых приказов", [this, voiceLayout, payload, choices, model] {
                Apply(voice::Install(voiceLayout, payload, choices, model, [this](voice::Step const& step) { Progress(step.text, step.percent); },
                    &cancel_));
            });
        };
        voice::Sizes const sizes = voice::SizesFor(fetch);
        if (sizes.download == 0 && state.damaged.empty()) {
            run();  // only taking parts out, or nothing to fetch: no download to confirm
            return;
        }
        Confirm("Скачать голосовые приказы?",
            std::format("Около {} ({} на канале 50 Мбит/с) из официальных релизов Moonshine Voice и llama.cpp на GitHub, моделей Moonshine "
                        "на Hugging Face и собственного релиза этого мода на GitHub для модели приказов. Каждый файл сверяется по "
                        "SHA-256 и кладётся в папку игры ASI\\voice с записью, чтобы «Удалить» здесь или удаление мода убрали его. Всё работает "
                        "на вашем ПК: микрофон слышен, только пока вы держите жест рации, и ничего никуда не отправляется. Сначала закройте "
                        "игру.",
                install::http::SizeText(sizes.download), install::http::TimeText(sizes.download, 6.25e6)),
            "Скачать и установить", false, run);
    } else if (name == "voice_remove") {
        if (!gameOk_ || waiting_) {
            return;
        }
        install::Layout const voiceLayout{status_.root};
        Confirm("Удалить голосовые приказы?",
            "Убирает скачанные голосовые файлы из папки игры ASI\\voice (распознаватель, его модели, уровень модели). Мод "
            "продолжает работать без них: в журнале будет \"voice off\", а отряд принимает приказы жестами, как раньше. Установка вернёт их.",
            "Удалить", true, [this, voiceLayout] {
                RunJob("Удаление голосовых приказов", [this, voiceLayout] { Apply(install::voice::Remove(voiceLayout)); });
            });
    } else if (name == "voice_check") {
        if (!gameOk_) {
            return;
        }
        install::Layout const voiceLayout{status_.root};
        RunJob("Проверка голосовых файлов", [this, voiceLayout] { Apply(install::voice::Check(voiceLayout, true)); });
    } else if (name == "mem_page") {
        if (host_.openUrl) {
            host_.openUrl(packs::kMemPage);
        }
    } else if (name == "mem_download") {
        if (memReady_) {
            return;
        }
        // First what the latest release offers (its name and size), then the download once the player has seen them.
        auto asset = std::make_shared<std::optional<packs::Asset>>();
        RunJob("Поиск последнего релиза Mass Effect Modder", [this, asset] {
            packs::MemLookup const found = packs::FindMemRelease();
            *asset = found.asset;
            if (!*asset) {
                Post(found.error + ". Воспользуйтесь его страницей.", true);
            }
        });
        jobDone_ = [this, asset] {
            if (!*asset) {
                return;
            }
            packs::Asset const a = **asset;
            Confirm("Скачать Mass Effect Modder?",
                std::format("{} ({}, {} на канале 50 Мбит/с) с github.com/MassEffectModder, из его официальных релизов. GPL-3.0. Он будет в "
                            "%LOCALAPPDATA%\\MEVR\\tools.",
                    a.name, install::http::SizeText(a.size), install::http::TimeText(a.size, 6.25e6)),
                "Скачать", false, [this, a] {
                    RunJob("Скачивание " + a.name, [this, a] {
                        std::string const error = packs::DownloadMem(a, [this, a](install::http::Progress const& p) {
                            if (p.total > 0 && p.received >= p.total) {
                                Progress("Распаковка " + a.name, 100);
                                return;
                            }
                            int const percent = p.total > 0 ? static_cast<int>(100 * p.received / p.total) : 0;
                            Progress(std::format("Скачивание {}: {} из {}", a.name, install::http::SizeText(p.received), install::http::SizeText(p.total)), percent);
                        }, &cancel_);
                        Post(error.empty() ? "Mass Effect Modder готов." : error, !error.empty());
                    });
                });
        };
    } else if (name == "pack_page") {
        if (packs::Pack const* p = packs::Find(arg); p != nullptr && host_.openUrl) {
            host_.openUrl(p->url);
        }
    } else if (name == "pack_file") {
        packs::Pack const* p = packs::Find(arg);
        if (p == nullptr || !host_.pickFile) {
            return;
        }
        if (auto const file = host_.pickFile(std::wstring(L"Скачанный ") + detect::Wide(p->name))) {
            std::erase_if(packFiles_, [&](auto const& e) { return e.first == arg; });
            packFiles_.emplace_back(arg, *file);
            Refresh();
        }
    } else if (name == "pack_install") {
        packs::Pack const* p = packs::Find(arg);
        auto const it = std::find_if(packFiles_.begin(), packFiles_.end(), [&](auto const& e) { return e.first == arg; });
        if (p == nullptr || it == packFiles_.end() || !gameOk_ || waiting_ || !memReady_) {
            return;
        }
        fs::path const file = it->second;
        std::string const id = p->id;
        std::string const packName = p->name;
        Confirm(std::string("Установить ") + p->name + "?",
            std::format("Это навсегда меняет файлы игры: Mass Effect Modder перезаписывает её текстуры ({}; первый раз это может занять "
                        "много времени, пока он сканирует игру). Ставьте наборы текстур после всех остальных контентных модов: контентные моды "
                        "после них уже не поставить, а убрать текстуры можно только полным восстановлением или переустановкой игры. Сначала закройте игру.",
                p->size),
            "Установить навсегда", true, [this, layout, file, id, packName] {
                RunJob("Установка " + packName, [this, layout, file, id] {
                    // MEM, the manifest's record, and our recorded VR game settings again where MEM wrote over them.
                    Apply(install::InstallPack(layout, id, file, [this](packs::InstallStep const& s) {
                        Progress(s.detail.empty() ? s.stage : s.stage + ": " + s.detail, s.percent);
                    }, &cancel_));
                });
            });
    } else if (name == "admin") {
        if (host_.restartAsAdmin) {
            host_.restartAsAdmin();
        }
    } else if (name == "report_toggle") {
        // A checkbox: what the zip holds changes, so the preview is gathered again.
        if (arg == "snapshot") {
            reportSnapshot_ = !reportSnapshot_;
        } else if (arg == "perf") {
            reportPerf_ = !reportPerf_;
        }
        GatherReport();
    } else if (name == "report_refresh") {
        GatherReport();
    } else if (name == "report_create") {
        if (!bundle_) {
            return;
        }
        auto bundle = bundle_;
        install::report::Options const options = ReportOptions();
        auto result = std::make_shared<install::report::Result>();
        RunJob("Запись отчёта", [this, bundle, options, result] {
            *result = install::report::Write(*bundle, options);
            Post(result->ok ? "Сохранено " + detect::Utf8(result->zip.filename().wstring()) + " (" + install::report::SizeText(result->bytes) +
                                  ") to " + result->folder + ". Никуда ничего не отправлялось."
                            : "Не удалось сохранить отчёт: " + result->error,
                !result->ok);
        });
        jobDone_ = [this, result] {
            crashOffer_ = false;
            reportHasZip_ = result->ok;
            if (result->ok) {
                reportZipPath_ = result->zip;
                reportZip_ = "Сохранено в " + result->folder + ": " + detect::Utf8(result->zip.filename().wstring()) +
                             ". Откройте его, чтобы увидеть точное содержимое; приложите к обращению или отдайте своему ИИ-ассистенту.";
                if (host_.revealFile) {
                    host_.revealFile(result->zip);
                }
            } else {
                reportZip_ = "Не удалось сохранить отчёт: " + result->error;
            }
        };
    } else if (name == "report_reveal") {
        if (host_.revealFile && !reportZipPath_.empty()) {
            host_.revealFile(reportZipPath_);
        }
    } else if (name == "report_issue") {
        if (host_.openUrl && bundle_) {
            host_.openUrl(install::report::IssueUrl(*bundle_));
        }
    } else if (name == "crash_report") {
        page_ = "report";
    } else if (name == "crash_dismiss") {
        install::report_marks::Touch(install::report_marks::DismissedMark(install::LocalDataDir()), "dismissed in MEVR Setup");
        crashOffer_ = false;
    }
    Dirty();
}

void App::Sample(std::string const& state) {
    // The preview's stand-ins (tools/setup --preview): a PS VR2 on SteamVR, the Steam copy.
    sample_ = true;
    bool const notInstalled = state == "fresh" || state == "nogame" || state == "oldversion" || state == "firststart-fresh";
    bool const neverStarted = state.rfind("firststart", 0) == 0;  // no GamerSettings.ini yet
    status_ = install::Status{};
    status_.gameFound = state != "nogame";
    status_.root = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mass Effect Legendary Edition";
    status_.source = "Steam";
    status_.version = state == "oldversion" ? detect::ParseVersion("2.0.0.48419") : detect::ParseVersion("2.0.0.48602");
    status_.versionCheck = state == "oldversion" ? detect::VersionCheck::Different : detect::VersionCheck::Supported;
    status_.bink = notInstalled ? detect::BinkState::Original : detect::BinkState::Ours;
    status_.installed = !notInstalled;
    status_.installedVersion = "0.1.0";
    status_.loader = "mevr";
    status_.hasGamerSettings = !neverStarted;
    status_.gameSettings = status_.installed && !neverStarted && state != "pending-apply";
    if ((neverStarted && status_.installed) || state == "pending-apply") {
        install::settings::Choices choices;
        choices.target = install::settings::TargetFor("headset", 2824, 2880);
        status_.pendingSettings = install::PendingFromChoices(choices);
    }
    if (state == "repair-needed") {
        status_.needsRepair = {install::paths::kBink, install::paths::kToc};
    }
    status_.canWrite = true;
    if (state == "content") {
        status_.packs.push_back({"alot-le1", "ALOT for ME1", "mem", "2026-10-23T12:00:00"});
        status_.texturesMarked = true;
    }
    payload_ = install::Payload{L"payload"};
    hasPayload_ = true;
    xr_ = state == "meta" ? detect::XrRuntime{"C:\\Program Files\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json", "Oculus", "Meta"}
                          : detect::XrRuntime{"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json", "SteamVR/OpenXR", "SteamVR"};
    headsetWidth_ = 2824;
    headsetHeight_ = 2880;
    quality_ = "auto";
    log_.clear();
    busy_ = false;
    canCancel_ = false;
    confirmOpen_ = false;
    packFiles_.clear();
    if (state == "content") {
        packFiles_.emplace_back("isl-le1", L"Improved Static Lighting for ME1-363-1-0.7z");
    }
    // Refresh, without touching the disk for the status (the preview's root doesn't exist here).
    install::Status const keep = status_;
    fs::path const root = status_.root;
    status_.root.clear();
    Refresh();
    status_ = keep;
    status_.root = root;
    // Refresh read nothing for these; set what the pages show.
    gameOk_ = keep.gameFound;
    versionOk_ = keep.versionCheck == detect::VersionCheck::Supported;
    installed_ = keep.installed;
    settingsApplied_ = keep.gameSettings;
    hasGamerSettings_ = keep.hasGamerSettings;
    settingsPending_ = keep.pendingSettings.has_value();
    texturesMarked_ = keep.texturesMarked;
    loaderOk_ = gameOk_;
    canInstall_ = gameOk_ && versionOk_;
    ready_ = canInstall_;
    adminNeeded_ = false;
    rootText_ = gameOk_ ? detect::Utf8(root.wstring()) : "Папка игры не выбрана";
    chipGame_ = gameOk_ ? "Mass Effect 1 LE" : "Игра не найдена";
    chipInstalled_ = installed_ ? "Установлено 0.1.0" : "Не установлено";
    gameTitle_ = gameOk_ ? "Найдено" : "Не найдено";
    gameText_ = gameOk_ ? "Steam: Mass Effect Legendary Edition" : "Выберите папку на странице «Добро пожаловать»";
    versionTitle_ = keep.version ? keep.version->ToString() : "Неизвестно";
    loaderTitle_ = keep.bink == detect::BinkState::Ours ? "Загрузчик MEVR" : "Пока нет";
    loaderText_ = gameOk_ ? detect::BinkStateText(keep.bink) : "";
    versionText_ = !gameOk_    ? ""
        : versionOk_           ? "Версия, под которую собран мод"
                               : "Моду нужна версия 2.0.0.48602, на любой другой он не включается. Обновите игру в Steam или EA app.";
    firstStartText_ = FirstStartText(installed_, settingsPending_);
    bool const applied = installed_ && settingsApplied_;
    curRes_ = !hasGamerSettings_ ? "-" : applied ? "2824 × 2880" : "2560 × 1440";
    curWindow_ = !hasGamerSettings_ ? "-" : applied ? "Без рамки" : "В окне или на весь экран";
    curHdr_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    curVsync_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    curBlur_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    curGrain_ = !hasGamerSettings_ ? "-" : OnOff(!applied);
    UpdateRepairStatus();
    UpdateHero();
    uninstallText_ = installed_ ? "Удалено: 41 файл. Возвращено как было: 2 файла. Файлов настроек возвращено к вашим значениям: 2." : "Ничего не установлено.";
    memReady_ = state == "content";
    memText_ = memReady_ ? "Готов: он ставит наборы текстур ниже (GPL, из собственных релизов на GitHub)."
                         : "Ставит наборы текстур (GPL, от команды ALOT). Скачивается из собственных релизов на GitHub по кнопке «Скачать».";
    footerText_ = "Журнал мода: Binaries\\Win64\\Logs\\LE1MEVR.log · журнал загрузчика: Logs\\MEVRLoader.log";
    UpdatePacks();
    if (state == "content") {
        for (PackView& v : packs_) {
            v.installed = v.id == "alot-le1";
            v.state = v.installed ? "Установлено этим установщиком." : v.id == "isl-le1" ? "Ready to install: Improved Static Lighting for ME1-363-1-0.7z"
                                                                                     : "Не скачано: откройте его страницу, скачайте версию для MEM и выберите файл.";
            v.file = v.id == "isl-le1" ? "Improved Static Lighting for ME1-363-1-0.7z" : "";
        }
        requestText_ = "Вы просили: Improved Static Lighting for ME1.";
        voiceInstalled_ = true;
        voiceReady_ = true;
        voiceSmall_ = false;
        voiceMedium_ = false;
        voiceLlm_ = true;
        voiceUpToDate_ = false;
        voiceWarn_ = false;
        voiceButton_ = "Применить";
        voiceParts_ = "Среда выполнения распознавателя речи, Модель распознавателя (tiny)";
        voiceSize_ = std::format("{} на скачивание, {} в папке игры", install::http::SizeText(install::voice::SizesFor({"llm-runtime", "llm-model"}).download),
            install::http::SizeText(install::voice::SizesFor(install::voice::PartsFor({})).disk));
        voiceState_ = "Распознаватель речи установлен, голосовые приказы работают. Уровень модели ждёт: его релиз (voice-model-1) "
                      "ещё не был опубликован на момент установки. «Применить» добавит его, когда он появится.";
    } else {
        for (PackView& v : packs_) {
            v.installed = false;
            v.file.clear();
            v.state = "Не скачано: откройте его страницу, скачайте версию для MEM и выберите файл.";
        }
        requestText_.clear();
        voiceInstalled_ = false;
        voiceReady_ = gameOk_;
        voiceUpToDate_ = false;
        voiceWarn_ = false;
        voiceButton_ = "Установить";
        voiceParts_.clear();
        voiceSize_ = std::format("{} на скачивание, {} в папке игры", install::http::SizeText(install::voice::SizesFor(install::voice::PartsFor({})).download),
            install::http::SizeText(install::voice::SizesFor(install::voice::PartsFor({})).disk));
        voiceState_ = gameOk_ ? "Не установлено." : "Сначала найдите игру (страница «Добро пожаловать»).";
    }
    if (state == "busy") {
        busy_ = true;
        progressText_ = "Installing: copied Game/ME1/Binaries/Win64/ASI/menu/main.rcss";
        progress_ = 64;
        log_ = {{"renamed Game/ME1/Binaries/Win64/bink2w64.dll to Game/ME1/Binaries/Win64/bink2w64_original.dll", false},
            {"added Game/ME1/Binaries/Win64/bink2w64.dll", false}, {"added Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi", false},
            {"added Game/ME1/Binaries/Win64/ASI/fonts/Rajdhani-Bold.ttf", false}, {"added Game/ME1/Binaries/Win64/ASI/menu/main.rcss", false}};
    }
    if (state == "firststart-busy") {
        namespace fst = install::launch::first_start;
        busy_ = true;
        canCancel_ = true;
        progressText_ = fst::PhaseText(fst::Phase::WaitingForQuit);
        progress_ = 66;
        log_ = {{"Started Mass Effect (-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles 20 -OVERRIDELANGUAGE=INT)", false},
            {fst::PhaseText(fst::Phase::WaitingForQuit), false}};
    }
    if (state == "firststart-installed") {
        log_ = {{"added Game/ME1/Binaries/Win64/ASI/LE1MEVR.asi", false},
            {"warning: Mass Effect 1 hasn't been started on this PC yet, so its settings file (GamerSettings.ini) doesn't exist and the VR game "
             "settings can't go on. They're kept: start the game once (MEVR Setup's \"Start the game once\"), quit at its main menu, and they're "
             "applied (or use Repair afterwards).",
                false},
            {"Done.", false},
            {"One step left: Mass Effect 1 hasn't been started on this PC, so the VR game settings wait for its settings file. Press \"Start the "
             "game once\" above (or on the Welcome page).",
                true}};
    }
    if (state == "installed-log") {
        log_ = {{"kept the existing Game/ME1/Binaries/Win64/ASI/MELEVR.ini", false},
            {"edited Game/ME1/Binaries/Win64/ASI/MELEVR.ini (2 values)", false},
            {"edited Game/ME1/BioGame/Config/GamerSettings.ini (9 values)", false},
            {"edited Game/ME1/BioGame/Config/BIOEngine.ini (1 values)", false}, {"Done.", false}};
    }
    if (state == "confirm-uninstall") {
        Confirm("Удалить VR-мод?",
            "Убирает мод и его загрузчик, возвращает каждый изменённый файл и настройку игры и удаляет ваши настройки VR и калибровку.",
            "Удалить", true, {});
    }
    if (state == "confirm-pack") {
        Confirm("Установить Improved Static Lighting for ME1?",
            "Это навсегда меняет файлы игры: Mass Effect Modder перезаписывает её текстуры (около 9.7 ГБ; первый раз это может занять много "
            "времени, пока он сканирует игру). Ставьте наборы текстур после всех остальных контентных модов: контентные моды после них уже не "
            "поставить, а убрать текстуры можно только полным восстановлением или переустановкой игры. Сначала закройте игру.",
            "Установить навсегда", true, {});
    }
    if (state == "waiting") {
        waiting_ = true;
        waitText_ = "Изменения начнутся после выхода из игры.";
    } else {
        waiting_ = false;
    }
    updateEnabled_ = false;
    updateText_ = "Страница релизов ещё не задана (MEVR-Setup.ini, [Update] repo).";
    updateAvailable_ = state == "update-available";
    if (updateAvailable_) {
        updateEnabled_ = true;
        updateText_ = "Вышла версия v0.2.0. Скачать 24 МБ. Ваши настройки сохранятся.";
    }
    // Report a problem's stand-ins: a crash last session, a gathered preview.
    crashOffer_ = state == "crash";
    reportRequest_ = state == "report-request" ? "Вы запросили отчёт из меню игры. Проверьте, что в него войдёт, и создайте его." : "";
    reportSnapshot_ = state == "report-request";
    reportPerf_ = false;
    reportHasZip_ = state == "report-saved";
    reportZip_ = reportHasZip_ ? "Сохранено на рабочий стол: MEVR-report-2026-10-26-0143.zip. Откройте его, чтобы увидеть точное содержимое; приложите к обращению или "
                                 "отдайте своему ИИ-ассистенту."
                               : "";
    reportReady_ = state.rfind("report", 0) == 0;
    reportFiles_.clear();
    if (reportReady_) {
        reportFiles_ = {{"REPORT.md", "~6.2 KB", "Сводка: система, что пошло не так, изменённые настройки, запрос для вашего ИИ-ассистента"},
            {"logs/LE1MEVR.log", "246.1 KB", "4 скрыто"}, {"logs/LE1MEVR.prev.log", "502.1 KB", "6 скрыто"},
            {"logs/MELEVR_Log.txt", "3.0 MB", "только последний сеанс: первые 0.5 МБ и последние 2.5 МБ из 9.1 МБ (файл хранит 182.7 МБ из всех сеансов)"},
            {"logs/MEVRLoader.log", "1.2 KB", ""}, {"logs/crash-breadcrumbs.txt", "29.4 KB", "записано 2026-10-26 00:11"},
            {"logs/reflect-check.json", "80.9 KB", "12 скрыто"}, {"settings/LE1MEVR.ini", "12.7 KB", ""}, {"settings/MELEVR.ini", "9.8 KB", ""},
            {"settings/MEVR-install.json", "6.0 KB", ""}, {"system/folders.txt", "3.1 KB", "имена и размеры файлов"}};
        if (reportSnapshot_) {
            reportFiles_.push_back({"snapshot/20261026-014210.json", "18.2 KB", ""});
            reportFiles_.push_back({"snapshot/20261026-014210-left.png", "2.1 MB", "что видел этот глаз"});
            reportFiles_.push_back({"snapshot/20261026-014210-right.png", "2.1 MB", "что видел этот глаз"});
        }
        reportRedactions_ = "22 скрытия: имя пользователя Windows 4 раза, имя компьютера 2, папка профиля 16, адреса e-mail 0. "
                            "В архиве они читаются как <user>, <computer>, <profile>, <email>.";
        reportHeadline_ = "Падение: 0xC0000005 (нарушение доступа) в ntdll.dll+0x1A12D";
        reportText_ = "12 файлов, 4.1 МБ до сжатия.";
    }
    Dirty();
}

} // namespace mevr::setup
