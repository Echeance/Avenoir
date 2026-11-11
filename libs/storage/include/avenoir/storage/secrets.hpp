#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/core/error.hpp"

// Secrets d'Avenoir (clé du sceau des réglages…) : trousseau du système,
// sinon fichier lisible par l'utilisateur seul (docs/PROJECT.md § 5.1).
namespace avenoir::storage {

/// Nom de secret valide : minuscules ASCII, chiffres et « - » (100 au plus).
[[nodiscard]] bool is_valid_secret_name(std::string_view name);

class SecretStore {
  public:
    SecretStore() = default;
    SecretStore(const SecretStore&) = delete;
    SecretStore& operator=(const SecretStore&) = delete;
    SecretStore(SecretStore&&) = delete;
    SecretStore& operator=(SecretStore&&) = delete;
    virtual ~SecretStore() = default;

    /// Valeur du secret `name` ; rien s'il n'existe pas ; erreur si le
    /// magasin ne répond pas.
    [[nodiscard]] virtual core::Result<std::optional<std::string>> read(std::string_view name) = 0;
    /// Crée ou remplace le secret `name`. Valeur ASCII imprimable.
    [[nodiscard]] virtual core::Result<void> write(std::string_view name,
                                                   std::string_view value) = 0;
    /// Où les secrets sont gardés, pour l'utilisateur (« trousseau du système »).
    [[nodiscard]] virtual std::string description() const = 0;
    /// Vrai pour le trousseau du système ; faux pour un repli plus faible
    /// (fichier), à signaler à l'utilisateur.
    [[nodiscard]] virtual bool is_system_keychain() const = 0;
};

/// En mémoire, pour les tests.
class MemorySecretStore final : public SecretStore {
  public:
    [[nodiscard]] core::Result<std::optional<std::string>> read(std::string_view name) override;
    [[nodiscard]] core::Result<void> write(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::string description() const override { return "mémoire"; }
    [[nodiscard]] bool is_system_keychain() const override { return true; }

  private:
    std::map<std::string, std::string, std::less<>> values_;
};

/// Un fichier par secret dans `directory` (dossier `0700`, fichiers `0600`) :
/// repli quand le trousseau du système manque. Protège des autres comptes,
/// pas d'un programme lancé par l'utilisateur.
class FileSecretStore final : public SecretStore {
  public:
    explicit FileSecretStore(std::filesystem::path directory);
    [[nodiscard]] core::Result<std::optional<std::string>> read(std::string_view name) override;
    [[nodiscard]] core::Result<void> write(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] bool is_system_keychain() const override { return false; }

  private:
    std::filesystem::path directory_;
};

/// Trousseau du système (Windows Credential Manager ; Secret Service par
/// `secret-tool` sous Linux) s'il répond, sinon `FileSecretStore` dans
/// `fallback_directory`. Un secret déjà gardé dans le fichier (trousseau
/// absent à un lancement précédent) reste lu.
[[nodiscard]] std::unique_ptr<SecretStore>
open_secret_store(const std::filesystem::path& fallback_directory);

} // namespace avenoir::storage
