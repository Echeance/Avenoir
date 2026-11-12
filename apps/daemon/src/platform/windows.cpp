// Démarrage à l'ouverture de session sous Windows : valeur « Avenoir » de la
// clé Run du compte (HKCU), sans droits d'administrateur ; le démon démarre
// dans la session de l'utilisateur (un service n'aurait pas accès au bureau).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <vector>

#include <windows.h>

#include "avenoir/core/error.hpp"

#include "../autostart.hpp"

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

} // namespace avenoir::daemon
