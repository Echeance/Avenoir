// Code propre à Windows. Jamais de `cmd.exe` : `ShellExecuteW` résout les
// applications enregistrées (App Paths), le PATH et le navigateur par défaut.
// Chaînes en UTF-8 partout, converties en UTF-16 uniquement ici.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// windows.h doit précéder shellapi.h : ordre protégé du tri de clang-format.
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <powrprof.h>
#include <wbemidl.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <reproc++/reproc.hpp>
// clang-format on

#include "avenoir/core/env.hpp"

#include "platform.hpp"

namespace avenoir::tools::platform {

namespace {

std::optional<std::wstring> to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring{};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                            static_cast<int>(utf8.size()), wide.data(), length) != length) {
        return std::nullopt;
    }
    return wide;
}

std::string to_utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length,
                        nullptr, nullptr);
    return utf8;
}

/// Pointeur COM possédé (Release à la destruction) : RAII, pas de pointeur nu.
template <typename T>
class ComPtr {
  public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&&) = delete;
    ComPtr& operator=(ComPtr&&) = delete;
    ~ComPtr() { reset(); }
    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    /// Adresse pour une fonction COM qui remplit le pointeur.
    [[nodiscard]] T** put() {
        reset();
        return &ptr_;
    }

  private:
    T* ptr_ = nullptr;
};

/// Chaîne allouée par COM (CoTaskMemFree à la destruction).
class CoString {
  public:
    CoString() = default;
    CoString(const CoString&) = delete;
    CoString& operator=(const CoString&) = delete;
    CoString(CoString&&) = delete;
    CoString& operator=(CoString&&) = delete;
    ~CoString() { CoTaskMemFree(text_); }
    [[nodiscard]] PWSTR* put() noexcept { return &text_; }
    [[nodiscard]] std::string utf8() const { return text_ != nullptr ? to_utf8(text_) : ""; }
    [[nodiscard]] std::wstring wide() const { return text_ != nullptr ? text_ : L""; }

  private:
    PWSTR text_ = nullptr;
};

/// COM initialisé pour ce thread le temps d'un appel.
class ComScope {
  public:
    ComScope() : ok_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;
    ~ComScope() {
        if (ok_) {
            CoUninitialize();
        }
    }

  private:
    bool ok_;
};

/// Chemin de fichier (« C:\… », « \\serveur\… ») plutôt qu'identifiant d'application.
bool is_path(std::string_view id) {
    return (id.size() > 2 && id[1] == ':') || id.starts_with("\\\\");
}

/// Nom d'un exécutable sans dossier ni extension (« …\\notepad.exe » → « notepad »).
std::string program_name(std::string_view id) {
    if (const auto slash = id.find_last_of("\\/"); slash != std::string_view::npos) {
        id.remove_prefix(slash + 1);
    }
    if (id.size() > 4 && (id.ends_with(".exe") || id.ends_with(".EXE"))) {
        id.remove_suffix(4);
    }
    return std::string(id);
}

std::expected<void, std::string> shell_open(const std::string& target, const std::string& what) {
    const auto wide = to_wide(target);
    if (!wide) {
        return std::unexpected(what + " : texte UTF-8 invalide");
    }
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", wide->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // API historique : le code de retour est un entier rangé dans un HINSTANCE ;
    // une valeur supérieure à 32 signifie le succès. Seul cast de ce type, isolé ici.
    const auto code =
        reinterpret_cast<INT_PTR>(result); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (code <= 32) {
        return std::unexpected(what + " : échec (code " + std::to_string(code) + ")");
    }
    return {};
}

} // namespace

bool supports_launching() noexcept {
    return true;
}

std::expected<void, std::string> launch_app(const std::string& name) {
    return shell_open(name, "impossible de lancer « " + name + " »");
}

std::expected<void, std::string> open_url(const std::string& url) {
    return shell_open(url, "impossible d'ouvrir l'adresse");
}

/// Raccourcis (.lnk) des dossiers Programmes du menu Démarrer, avec leur nom
/// affiché localisé (« Notepad.lnk » → « Bloc-notes », via desktop.ini).
void add_start_menu_shortcuts(std::vector<AppEntry>& apps) {
    for (const KNOWNFOLDERID& known : {FOLDERID_CommonPrograms, FOLDERID_Programs}) {
        CoString root;
        if (FAILED(SHGetKnownFolderPath(known, KF_FLAG_DEFAULT, nullptr, root.put()))) {
            continue;
        }
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 std::filesystem::path(root.wide()),
                 std::filesystem::directory_options::skip_permission_denied, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (!it->is_regular_file(error) || it->path().extension() != L".lnk") {
                continue;
            }
            SHFILEINFOW info{};
            std::string name;
            if (SHGetFileInfoW(it->path().c_str(), 0, &info, sizeof(info), SHGFI_DISPLAYNAME) !=
                0) {
                name = to_utf8(info.szDisplayName);
            }
            const std::string stem = to_utf8(it->path().stem().wstring());
            if (name.empty()) {
                name = stem;
            }
            // Raccourcis de désinstallation ou d'aide : pas des applications à lancer.
            const auto lowered = tools::detail::normalize_app_name(name);
            if (lowered.contains("uninstall") || lowered.contains("desinstall")) {
                continue;
            }
            AppEntry app{
                .id = to_utf8(it->path().wstring()), .name = std::move(name), .aliases = {}};
            if (stem != app.name) {
                app.aliases.push_back(stem);
            }
            apps.push_back(std::move(app));
        }
    }
}

std::vector<AppEntry> installed_apps() {
    // Dossier « Applications » du menu Démarrer (shell:AppsFolder) pour les
    // applications du Store, plus les raccourcis du menu Démarrer pour les
    // applications classiques (le dossier Applications les remplit en différé).
    const ComScope com;
    std::vector<AppEntry> apps;
    add_start_menu_shortcuts(apps);
    // IShellFolder::EnumObjects avec des drapeaux explicites : l'énumération
    // par défaut d'IShellItem ne rend que quelques éléments de ce dossier.
    PIDLIST_ABSOLUTE folder_id = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, &folder_id))) {
        return apps;
    }
    ComPtr<IShellFolder> folder;
    const HRESULT bound = SHBindToObject(nullptr, folder_id, nullptr, IID_PPV_ARGS(folder.put()));
    CoTaskMemFree(folder_id);
    if (FAILED(bound)) {
        return apps;
    }
    ComPtr<IEnumIDList> items;
    if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, items.put()) != S_OK) {
        return apps;
    }
    const auto name_of = [&folder](PCUITEMID_CHILD item, SHGDNF flags) -> std::string {
        STRRET value{};
        CoString text;
        if (FAILED(folder->GetDisplayNameOf(item, flags, &value)) ||
            FAILED(StrRetToStrW(&value, item, text.put()))) {
            return {};
        }
        return text.utf8();
    };
    PITEMID_CHILD item = nullptr;
    while (items->Next(1, &item, nullptr) == S_OK) {
        AppEntry app{.id = name_of(item, SHGDN_INFOLDER | SHGDN_FORPARSING),
                     .name = name_of(item, SHGDN_NORMAL),
                     .aliases = {}};
        CoTaskMemFree(item);
        item = nullptr;
        if (app.id.empty() || app.name.empty() ||
            std::ranges::any_of(apps,
                                [&app](const AppEntry& known) { return known.name == app.name; })) {
            continue; // déjà trouvée par son raccourci
        }
        if (auto program = program_name(app.id); !program.empty() && program != app.name) {
            app.aliases.push_back(std::move(program));
        }
        apps.push_back(std::move(app));
    }
    return apps;
}

std::expected<void, std::string> launch_entry(const AppEntry& app) {
    const std::string target = is_path(app.id) ? app.id : "shell:AppsFolder\\" + app.id;
    return shell_open(target, "impossible de lancer « " + app.name + " »");
}

std::optional<AppEntry> default_browser() {
    // Application associée au protocole https (choix de l'utilisateur).
    std::wstring buffer(MAX_PATH, L'\0');
    const auto query = [&buffer](ASSOCSTR what) -> std::string {
        auto size = static_cast<DWORD>(buffer.size());
        if (FAILED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, what, L"https", nullptr, buffer.data(),
                                     &size)) ||
            size == 0) {
            return {};
        }
        return to_utf8(std::wstring_view(buffer.data(), size - 1));
    };
    std::string executable = query(ASSOCSTR_EXECUTABLE);
    if (executable.empty()) {
        return std::nullopt;
    }
    std::string name = query(ASSOCSTR_FRIENDLYAPPNAME);
    if (name.empty()) {
        name = program_name(executable);
    }
    AppEntry app{.id = std::move(executable), .name = std::move(name), .aliases = {}};
    app.aliases.push_back(program_name(app.id));
    return app;
}

// ─── Contrôle du système (ROADMAP 2.1) ──────────────────────────────────────

namespace {

/// Message d'erreur d'un HRESULT.
std::string com_error(const std::string& what, HRESULT result) {
    return std::format("{} : échec (0x{:08X})", what, static_cast<unsigned long>(result));
}

/// Volume de la sortie audio par défaut (Core Audio).
std::expected<void, std::string> with_endpoint_volume(
    const std::function<std::expected<void, std::string>(IAudioEndpointVolume&)>& action) {
    const ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(enumerator.put()));
    if (FAILED(result)) {
        return std::unexpected(com_error("audio", result));
    }
    ComPtr<IMMDevice> device;
    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
    if (FAILED(result)) {
        return std::unexpected("aucune sortie audio");
    }
    ComPtr<IAudioEndpointVolume> volume;
    result = device->Activate(
        __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            volume.put()));
    if (FAILED(result)) {
        return std::unexpected(com_error("volume", result));
    }
    return action(*volume.get());
}

namespace media = winrt::Windows::Media::Control;

/// Exécute `action` sur la session multimédia active (lecteur qui a la main),
/// dans un fil à part : WinRT y est initialisé en multithread, sans toucher
/// à l'appartement COM du fil appelant.
std::expected<std::string, std::string> with_media_session(
    const std::function<std::string(const media::GlobalSystemMediaTransportControlsSession&)>&
        action) {
    std::expected<std::string, std::string> outcome =
        std::unexpected("aucun lecteur multimédia actif");
    std::jthread worker([&outcome, &action] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            outcome = std::unexpected("multimédia : initialisation impossible");
            return;
        }
        try {
            const auto manager =
                media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            if (const auto session = manager.GetCurrentSession()) {
                outcome = action(session);
            }
        } catch (...) {
            outcome = std::unexpected("le lecteur multimédia ne répond pas");
        }
        winrt::uninit_apartment();
    });
    worker.join();
    return outcome;
}

std::string session_description(const media::GlobalSystemMediaTransportControlsSession& session) {
    const auto properties = session.TryGetMediaPropertiesAsync().get();
    const std::string title = winrt::to_string(properties.Title());
    const std::string artist = winrt::to_string(properties.Artist());
    std::string out = title.empty() ? "(sans titre)" : "« " + title + " »";
    if (!artist.empty()) {
        out += " de " + artist;
    }
    using Status = media::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch (session.GetPlaybackInfo().PlaybackStatus()) {
    case Status::Playing:
        out += " (en lecture)";
        break;
    case Status::Paused:
        out += " (en pause)";
        break;
    case Status::Stopped:
        out += " (arrêté)";
        break;
    default:
        break;
    }
    return out;
}

/// Connexion WMI à l'espace ROOT\WMI (luminosité de l'écran intégré).
class WmiConnection {
  public:
    WmiConnection() {
        ComPtr<IWbemLocator> locator;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(locator.put())))) {
            return;
        }
        BSTR space = SysAllocString(L"ROOT\\WMI");
        const HRESULT result = locator->ConnectServer(space, nullptr, nullptr, nullptr, 0, nullptr,
                                                      nullptr, services_.put());
        SysFreeString(space);
        if (FAILED(result) ||
            FAILED(CoSetProxyBlanket(services_.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                     RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                                     EOAC_NONE))) {
            services_.reset();
        }
    }

    [[nodiscard]] IWbemServices* services() const noexcept { return services_.get(); }

    /// Premier objet d'une requête WQL.
    [[nodiscard]] bool first(const wchar_t* query, ComPtr<IWbemClassObject>& object) const {
        if (services_.get() == nullptr) {
            return false;
        }
        BSTR language = SysAllocString(L"WQL");
        BSTR text = SysAllocString(query);
        ComPtr<IEnumWbemClassObject> results;
        const HRESULT result = services_->ExecQuery(
            language, text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
            results.put());
        SysFreeString(language);
        SysFreeString(text);
        if (FAILED(result)) {
            return false;
        }
        ULONG count = 0;
        return SUCCEEDED(results->Next(2000, 1, object.put(), &count)) && count == 1;
    }

  private:
    ComPtr<IWbemServices> services_;
};

std::expected<int, std::string> read_brightness() {
    const ComScope com;
    const WmiConnection wmi;
    ComPtr<IWbemClassObject> monitor;
    if (!wmi.first(L"SELECT CurrentBrightness FROM WmiMonitorBrightness", monitor)) {
        return std::unexpected("luminosité non réglable sur cet écran");
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(monitor->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr))) {
        return std::unexpected("luminosité illisible");
    }
    const int level = value.vt == VT_UI1 ? static_cast<int>(value.bVal) : -1;
    VariantClear(&value);
    if (level < 0) {
        return std::unexpected("luminosité illisible");
    }
    return level;
}

constexpr const wchar_t* kPersonalizeKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

std::optional<DWORD> read_personalize(const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, name, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

/// Fenêtre cachée qui porte l'icône des notifications.
class HiddenWindow {
  public:
    HiddenWindow() {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"AvenoirNotification";
        RegisterClassW(&window_class); // déjà enregistrée : sans gravité
        handle_ = CreateWindowExW(0, L"AvenoirNotification", L"Avenoir", 0, 0, 0, 0, 0, nullptr,
                                  nullptr, window_class.hInstance, nullptr);
    }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    HiddenWindow(HiddenWindow&&) = delete;
    HiddenWindow& operator=(HiddenWindow&&) = delete;
    ~HiddenWindow() {
        if (handle_ != nullptr) {
            DestroyWindow(handle_);
        }
    }
    [[nodiscard]] HWND get() const noexcept { return handle_; }

  private:
    HWND handle_ = nullptr;
};

/// Notifications en cours d'affichage : chacune garde son icône quelques
/// secondes dans un fil ; à la fermeture d'Avenoir, les fils sont arrêtés
/// et les icônes retirées.
class Notifier {
  public:
    void show(std::wstring title, std::wstring body) {
        const std::scoped_lock lock(mutex_);
        std::erase_if(shown_, [](const Shown& shown) { return shown.done->load(); });
        auto done = std::make_shared<std::atomic<bool>>(false);
        shown_.push_back(Shown{
            .done = done,
            .thread = std::jthread([title = std::move(title), body = std::move(body),
                                    done](const std::stop_token& stop) {
                display(stop, title, body);
                done->store(true);
            }),
        });
    }

  private:
    struct Shown {
        std::shared_ptr<std::atomic<bool>> done;
        std::jthread thread;
    };

    static void display(const std::stop_token& stop, const std::wstring& title,
                        const std::wstring& body) {
        const HiddenWindow window;
        if (window.get() == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window.get();
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
        // IDI_INFORMATION en version large (le projet ne définit pas UNICODE).
        data.hIcon = LoadIconW(
            nullptr,
            MAKEINTRESOURCEW(
                32516)); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
        wcsncpy_s(data.szTip, L"Avenoir", _TRUNCATE);
        wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, body.c_str(), _TRUNCATE);
        data.dwInfoFlags = NIIF_INFO;
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            return;
        }
        // Le temps que la notification s'affiche et se lise ; arrêt anticipé
        // à la fermeture d'Avenoir.
        std::mutex mutex;
        std::unique_lock lock(mutex);
        std::condition_variable_any wake;
        wake.wait_for(lock, stop, std::chrono::seconds(10), [] { return false; });
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    std::mutex mutex_;
    std::vector<Shown> shown_;
};

Notifier& notifier() {
    static Notifier instance;
    return instance;
}

/// Accorde au processus le droit d'éteindre la machine (SE_SHUTDOWN_NAME).
bool enable_shutdown_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok =
        LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &privileges.Privileges[0].Luid) &&
        AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) &&
        GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

} // namespace

bool supports_volume() {
    // Sans sortie audio (machine virtuelle, serveur), l'outil n'est pas proposé.
    return get_volume().has_value();
}

std::expected<VolumeState, std::string> get_volume() {
    VolumeState state;
    auto done = with_endpoint_volume(
        [&state](IAudioEndpointVolume& volume) -> std::expected<void, std::string> {
            float level = 0;
            BOOL muted = FALSE;
            if (FAILED(volume.GetMasterVolumeLevelScalar(&level)) ||
                FAILED(volume.GetMute(&muted))) {
                return std::unexpected("volume illisible");
            }
            state.level = static_cast<int>(std::lround(level * 100.0F));
            state.muted = muted != FALSE;
            return {};
        });
    if (!done) {
        return std::unexpected(done.error());
    }
    return state;
}

std::expected<void, std::string> set_volume(int level) {
    return with_endpoint_volume(
        [level](IAudioEndpointVolume& volume) -> std::expected<void, std::string> {
            const HRESULT result =
                volume.SetMasterVolumeLevelScalar(static_cast<float>(level) / 100.0F, nullptr);
            if (FAILED(result)) {
                return std::unexpected(com_error("volume", result));
            }
            return {};
        });
}

std::expected<void, std::string> set_muted(bool muted) {
    return with_endpoint_volume(
        [muted](IAudioEndpointVolume& volume) -> std::expected<void, std::string> {
            const HRESULT result = volume.SetMute(muted ? TRUE : FALSE, nullptr);
            if (FAILED(result)) {
                return std::unexpected(com_error("son", result));
            }
            return {};
        });
}

bool supports_media() {
    return true;
}

std::expected<void, std::string> media_command(MediaCommand command) {
    auto done = with_media_session(
        [command](const media::GlobalSystemMediaTransportControlsSession& session) -> std::string {
            bool ok = false;
            switch (command) {
            case MediaCommand::PlayPause:
                ok = session.TryTogglePlayPauseAsync().get();
                break;
            case MediaCommand::Play:
                ok = session.TryPlayAsync().get();
                break;
            case MediaCommand::Pause:
                ok = session.TryPauseAsync().get();
                break;
            case MediaCommand::Next:
                ok = session.TrySkipNextAsync().get();
                break;
            case MediaCommand::Previous:
                ok = session.TrySkipPreviousAsync().get();
                break;
            case MediaCommand::Stop:
                ok = session.TryStopAsync().get();
                break;
            }
            return ok ? "ok" : "";
        });
    if (!done) {
        return std::unexpected(done.error());
    }
    if (done->empty()) {
        return std::unexpected("le lecteur multimédia refuse cette commande");
    }
    return {};
}

std::expected<std::string, std::string> now_playing() {
    return with_media_session(session_description);
}

bool supports_brightness() {
    return read_brightness().has_value();
}

std::expected<int, std::string> get_brightness() {
    return read_brightness();
}

std::expected<void, std::string> set_brightness(int level) {
    const ComScope com;
    const WmiConnection wmi;
    ComPtr<IWbemClassObject> methods;
    if (!wmi.first(L"SELECT __PATH FROM WmiMonitorBrightnessMethods", methods)) {
        return std::unexpected("luminosité non réglable sur cet écran");
    }
    VARIANT path;
    VariantInit(&path);
    if (FAILED(methods->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR) {
        VariantClear(&path);
        return std::unexpected("luminosité non réglable sur cet écran");
    }
    ComPtr<IWbemClassObject> definition;
    ComPtr<IWbemClassObject> in_definition;
    ComPtr<IWbemClassObject> arguments;
    BSTR class_name = SysAllocString(L"WmiMonitorBrightnessMethods");
    BSTR method = SysAllocString(L"WmiSetBrightness");
    bool ok =
        SUCCEEDED(wmi.services()->GetObject(class_name, 0, nullptr, definition.put(), nullptr)) &&
        SUCCEEDED(definition->GetMethod(method, 0, in_definition.put(), nullptr)) &&
        SUCCEEDED(in_definition->SpawnInstance(0, arguments.put()));
    if (ok) {
        VARIANT timeout;
        VariantInit(&timeout);
        timeout.vt = VT_I4;
        timeout.lVal = 0;
        VARIANT brightness;
        VariantInit(&brightness);
        brightness.vt = VT_UI1;
        brightness.bVal = static_cast<BYTE>(level);
        ok = SUCCEEDED(arguments->Put(L"Timeout", 0, &timeout, 0)) &&
             SUCCEEDED(arguments->Put(L"Brightness", 0, &brightness, 0)) &&
             SUCCEEDED(wmi.services()->ExecMethod(path.bstrVal, method, 0, nullptr, arguments.get(),
                                                  nullptr, nullptr));
    }
    SysFreeString(class_name);
    SysFreeString(method);
    VariantClear(&path);
    if (!ok) {
        return std::unexpected("luminosité : échec du réglage");
    }
    return {};
}

bool supports_dark_mode() {
    return read_personalize(L"AppsUseLightTheme").has_value();
}

std::expected<bool, std::string> get_dark_mode() {
    const auto light = read_personalize(L"AppsUseLightTheme");
    if (!light) {
        return std::unexpected("mode sombre : réglage introuvable");
    }
    return *light == 0;
}

std::expected<void, std::string> set_dark_mode(bool dark) {
    const DWORD light = dark ? 0 : 1;
    for (const wchar_t* name : {L"AppsUseLightTheme", L"SystemUsesLightTheme"}) {
        if (RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalizeKey, name, REG_DWORD, &light,
                            sizeof(light)) != ERROR_SUCCESS) {
            return std::unexpected("mode sombre : échec de l'écriture du réglage");
        }
    }
    // Prévenir les fenêtres ouvertes (explorateur, applications) du changement.
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(
        HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            L"ImmersiveColorSet"),
        SMTO_ABORTIFHUNG, 1000, &ignored);
    return {};
}

bool supports_lock() {
    return true;
}

std::expected<void, std::string> lock_screen() {
    if (!LockWorkStation()) {
        return std::unexpected("verrouillage : échec (code " + std::to_string(GetLastError()) +
                               ")");
    }
    return {};
}

bool supports_notify() {
    return true;
}

std::expected<void, std::string> notify(const std::string& title, const std::string& body) {
    auto wide_title = to_wide(title);
    auto wide_body = to_wide(body);
    if (!wide_title || !wide_body) {
        return std::unexpected("notification : texte UTF-8 invalide");
    }
    notifier().show(std::move(*wide_title), std::move(*wide_body));
    return {};
}

bool supports_power() {
    return true;
}

std::expected<void, std::string> power(PowerAction action) {
    if (action == PowerAction::Suspend) {
        if (!SetSuspendState(FALSE, FALSE, FALSE)) {
            return std::unexpected("mise en veille : échec (code " +
                                   std::to_string(GetLastError()) + ")");
        }
        return {};
    }
    if (!enable_shutdown_privilege()) {
        return std::unexpected("droit d'éteindre l'ordinateur refusé");
    }
    const UINT flags = action == PowerAction::Shutdown ? EWX_POWEROFF : EWX_REBOOT;
    if (!ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED)) {
        return std::unexpected("arrêt : échec (code " + std::to_string(GetLastError()) + ")");
    }
    return {};
}

std::string_view os_name() noexcept {
    return "Windows";
}

std::optional<std::string> distribution() {
    return std::nullopt;
}

std::optional<std::string> hostname() {
    return core::read_env("COMPUTERNAME");
}

// ─── Programmes ──────────────────────────────────────────────────────────────

std::expected<CommandExit, CommandFailure> run_command(const std::vector<std::string>& arguments,
                                                       std::chrono::milliseconds timeout,
                                                       const std::stop_token& stop,
                                                       std::size_t max_output) {
    if (arguments.empty()) {
        return std::unexpected(CommandFailure{.message = "aucun programme"});
    }
    // reproc attend sur le processus lui-même (WaitForSingleObject) : un
    // processus de fond qui hérite de la sortie (fichier temporaire) ne
    // bloque rien.
    std::FILE* raw = nullptr;
    if (tmpfile_s(&raw) != 0 || raw == nullptr) {
        return std::unexpected(CommandFailure{.message = "fichier temporaire impossible"});
    }
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> out(raw, &std::fclose);
    reproc::options options;
    options.redirect.out.type = reproc::redirect::file_;
    options.redirect.out.file = out.get();
    options.redirect.err.type = reproc::redirect::discard;
    options.redirect.in.type = reproc::redirect::discard;
    const reproc::stop_actions halt{
        .first = {.action = reproc::stop::terminate, .timeout = reproc::milliseconds(2000)},
        .second = {.action = reproc::stop::kill, .timeout = reproc::milliseconds(1000)},
        .third = {.action = reproc::stop::noop, .timeout = reproc::milliseconds(0)},
    };
    options.stop = halt;
    reproc::process process;
    if (process.start(arguments, options)) {
        return std::unexpected(
            CommandFailure{.message = "lancement de " + arguments.front() + " impossible"});
    }
    const auto end = std::chrono::steady_clock::now() + timeout;
    int status = -1;
    while (true) {
        const auto [code, error] = process.wait(reproc::milliseconds(100));
        if (!error) {
            status = code;
            break;
        }
        if (error != std::errc::timed_out) {
            return std::unexpected(
                CommandFailure{.message = arguments.front() + " : " + error.message()});
        }
        if (stop.stop_requested()) {
            const auto halted = process.stop(halt);
            (void)halted;
            return std::unexpected(CommandFailure{.message = {}, .cancelled = true});
        }
        if (std::chrono::steady_clock::now() >= end) {
            const auto halted = process.stop(halt);
            (void)halted;
            return std::unexpected(
                CommandFailure{.message = arguments.front() + " : délai dépassé"});
        }
    }
    // Une seule lecture, bornée : seules quelques lignes servent.
    std::string output(max_output, '\0');
    std::size_t read = 0;
    if (std::fseek(out.get(), 0, SEEK_SET) == 0) {
        read = std::fread(output.data(), 1, output.size(), out.get());
    }
    output.resize(read);
    return CommandExit{.exit_code = status, .output = std::move(output)};
}

} // namespace avenoir::tools::platform
