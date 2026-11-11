// Trousseau sous Windows : Gestionnaire d'identification (Credential
// Manager), identifiants génériques « avenoir/<nom> » propres à
// l'utilisateur, chiffrés par le système.

#ifndef NOMINMAX
#define NOMINMAX
#endif
// windows.h d'abord : wincred.h en dépend.
// clang-format off
#include <windows.h>
#include <wincred.h>
// clang-format on

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/storage/secrets.hpp"

#include "../platform.hpp"

namespace avenoir::storage::platform {

namespace {

std::unexpected<core::Error> keychain_error(std::string message) {
    return core::make_error(core::ErrorCode::Config, "trousseau du système : " + message);
}

/// « avenoir/<nom> » en UTF-16 (noms ASCII, vérifiés).
std::wstring target_name(std::string_view name) {
    std::wstring target = L"avenoir/";
    for (const char c : name) {
        target += static_cast<wchar_t>(c);
    }
    return target;
}

class CredentialStore final : public SecretStore {
  public:
    core::Result<std::optional<std::string>> read(std::string_view name) override {
        if (!is_valid_secret_name(name)) {
            return keychain_error("nom de secret invalide");
        }
        const std::wstring target = target_name(name);
        PCREDENTIALW credential = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE) {
            const DWORD code = GetLastError();
            if (code == ERROR_NOT_FOUND) {
                return std::optional<std::string>{};
            }
            return keychain_error("lecture refusée (code " + std::to_string(code) + ")");
        }
        std::string value(reinterpret_cast<const char*>(credential->CredentialBlob),
                          credential->CredentialBlobSize);
        CredFree(credential);
        return std::optional<std::string>{std::move(value)};
    }

    core::Result<void> write(std::string_view name, std::string_view value) override {
        if (!is_valid_secret_name(name)) {
            return keychain_error("nom de secret invalide");
        }
        std::wstring target = target_name(name);
        std::wstring user = L"avenoir";
        std::vector<BYTE> blob(value.begin(), value.end());
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = blob.data();
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // jamais itinérant
        credential.UserName = user.data();
        if (CredWriteW(&credential, 0) == FALSE) {
            return keychain_error("écriture refusée (code " + std::to_string(GetLastError()) + ")");
        }
        return {};
    }

    [[nodiscard]] std::string description() const override {
        return "trousseau du système (Gestionnaire d'identification)";
    }
    [[nodiscard]] bool is_system_keychain() const override { return true; }
};

} // namespace

std::unique_ptr<SecretStore> system_keychain() {
    return std::make_unique<CredentialStore>();
}

} // namespace avenoir::storage::platform
