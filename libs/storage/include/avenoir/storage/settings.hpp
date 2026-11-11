#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/storage/secrets.hpp"

// Réglages modifiables uniquement par Avenoir (docs/PROJECT.md § 5.1) : la
// configuration est accompagnée d'un sceau HMAC-SHA256 (`<config>.seal`)
// dont la clé est gardée dans le trousseau du système. Une modification
// faite hors d'Avenoir est refusée et la dernière version scellée restaurée.
namespace avenoir::storage {

/// Résultat de la vérification au lancement.
struct SealReport {
    enum class Status : unsigned char {
        Valid,      ///< Sceau valide : rien à signaler.
        NoSettings, ///< Pas encore de configuration (création guidée à venir).
        Adopted,    ///< Configuration existante scellée pour la première fois.
        Restored,   ///< Modification extérieure refusée, version scellée remise.
        Rejected,   ///< Fichier refusé sans version à remettre : mis de côté.
        Unverified, ///< Clé injoignable (trousseau verrouillé ?) : non vérifiée.
    };
    Status status = Status::Valid;
    std::string message; ///< Phrase pour l'utilisateur ; vide si Valid.
    /// Lignes changées (« - ligne scellée », « + ligne trouvée »), bornées.
    std::vector<std::string> changes;
};

class SealedSettings {
  public:
    /// `secrets` doit vivre plus longtemps que cet objet.
    SealedSettings(std::filesystem::path config_path, SecretStore& secrets);

    /// Au lancement, avant de lire la configuration : vérifie le fichier et
    /// son sceau. Modifié ou supprimé → version scellée restaurée ; sceau
    /// absent, invalide ou rejoué (version précédente) → fichier mis de côté
    /// (`<config>.rejected`). Jamais d'« accepter quand même ».
    [[nodiscard]] core::Result<SealReport> verify();

    /// Seule voie d'écriture : la configuration et son sceau (fichiers
    /// `0600`, remplacement atomique).
    [[nodiscard]] core::Result<void> save(const core::AppConfig& config);

    [[nodiscard]] const std::filesystem::path& config_path() const { return config_path_; }
    [[nodiscard]] std::filesystem::path seal_path() const;

  private:
    [[nodiscard]] core::Result<std::string> key(bool create);
    [[nodiscard]] core::Result<void> seal(const std::string& key, const std::string& content);

    std::filesystem::path config_path_;
    SecretStore* secrets_;
    std::string key_name_;  ///< Nom de la clé de ce fichier dans le trousseau.
    std::string seal_name_; ///< Nom du dernier sceau émis (contre le rejeu).
};

/// Lignes différentes entre la version scellée et celle trouvée (« - » :
/// retirée, « + » : ajoutée), caractères de contrôle écartés, 40 au plus.
[[nodiscard]] std::vector<std::string> changed_lines(const std::string& sealed,
                                                     const std::string& found);

} // namespace avenoir::storage
