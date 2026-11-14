// Démarrage à l'ouverture de session sous Windows : valeur « Avenoir » de la
// clé Run du compte (HKCU), sans droits d'administrateur ; le démon démarre
// dans la session de l'utilisateur (un service n'aurait pas accès au bureau).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
// Après windows.h.
#include <shellapi.h>

#include "avenoir/core/error.hpp"

#include "../autostart.hpp"
#include "../gpu.hpp"

namespace avenoir::daemon {

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"Avenoir";
constexpr const char* kWhere = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run (Avenoir)";

std::unexpected<core::Error> autostart_error(const std::string& message) {
    return core::make_error(core::ErrorCode::Config, "démarrage automatique : " + message);
}

} // namespace

core::Result<AutostartState> autostart_state() {
    DWORD size = 0;
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                                        nullptr, nullptr, &size);
    return AutostartState{.enabled = status == ERROR_SUCCESS, .where = kWhere};
}

core::Result<AutostartState> set_autostart(bool enabled) {
    if (!enabled) {
        const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            return autostart_error("suppression refusée (code " + std::to_string(status) + ")");
        }
        return autostart_state();
    }
    std::wstring self(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
        if (length == 0) {
            return autostart_error("exécutable introuvable");
        }
        if (length < self.size()) {
            break;
        }
        self.resize(self.size() * 2);
    }
    self.resize(length);
    const std::wstring command = L"\"" + self + L"\"";
    const LSTATUS status =
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        return autostart_error("écriture refusée (code " + std::to_string(status) + ")");
    }
    return autostart_state();
}

std::optional<DemandingApp> demanding_app(std::uint64_t /*threshold_bytes*/) {
    // Plein écran : jeu Direct3D exclusif, ou application en plein écran
    // (« occupé ») ; le programme est celui de la fenêtre au premier plan.
    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state)) ||
        (state != QUNS_RUNNING_D3D_FULL_SCREEN && state != QUNS_BUSY)) {
        return std::nullopt;
    }
    std::string name = "application en plein écran";
    DWORD pid = 0;
    if (HWND window = GetForegroundWindow(); window != nullptr) {
        GetWindowThreadProcessId(window, &pid);
    }
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        process != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE) {
            const std::wstring full(path.data(), size);
            const std::wstring file = full.substr(full.find_last_of(L"\\/") + 1);
            const int bytes =
                WideCharToMultiByte(CP_UTF8, 0, file.data(), static_cast<int>(file.size()), nullptr,
                                    0, nullptr, nullptr);
            std::string utf8(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, file.data(), static_cast<int>(file.size()), utf8.data(),
                                bytes, nullptr, nullptr);
            name = std::move(utf8);
        }
        CloseHandle(process);
    }
    // Le terminal d'Avenoir ou le bureau en plein écran ne demandent rien.
    for (const std::string_view ignored :
         {"WindowsTerminal.exe", "conhost.exe", "explorer.exe", "avenoir.exe"}) {
        if (name == ignored) {
            return std::nullopt;
        }
    }
    if (is_model_server(name)) {
        return std::nullopt;
    }
    return DemandingApp{.name = std::move(name), .video_memory = 0};
}

} // namespace avenoir::daemon
