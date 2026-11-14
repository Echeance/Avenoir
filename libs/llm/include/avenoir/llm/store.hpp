#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"

// Magasin de modèles (décision 65) : modèles conseillés par le banc d'essai
// (evals/resultats/COMPARAISON.md), installés et retirés par l'API d'Ollama.
namespace avenoir::llm {

/// Modèle du catalogue, évalué par le banc d'essai.
struct CatalogModel {
    std::string_view name;  ///< Nom dans la configuration (`[models.<nom>]`).
    std::string_view tag;   ///< Étiquette Ollama.
    std::string_view label; ///< « Gemma 4 26B-A4B ».
    std::string_view note;  ///< À quoi il sert.
    std::string_view score; ///< Résultat au banc (« 135/135, série 12 »).
    double video_memory_gb; ///< Mémoire vidéo mesurée (`ollama ps`), Go.
    double disk_gb;         ///< Place sur le disque, Go.
};

/// Catalogue, dans l'ordre où les modèles sont conseillés.
[[nodiscard]] std::span<const CatalogModel> model_catalog();
/// Modèle du catalogue de nom ou d'étiquette `name`, ou `nullptr`.
[[nodiscard]] const CatalogModel* find_catalog_model(std::string_view name);

/// Vrai si `tag` est une étiquette Ollama acceptable : minuscules, chiffres,
/// « . », « _ », « - », « : » et « / » (espace de noms), 100 au plus, sans
/// « .. » ni « // ».
[[nodiscard]] bool is_valid_model_tag(std::string_view tag);
/// Nom de configuration tiré d'une étiquette (`qwen3:4b` → `qwen3-4b`).
[[nodiscard]] std::string config_name_for(std::string_view tag);

/// Serveur Ollama utilisé par le magasin : celui d'un modèle configuré sur le
/// port d'Ollama (11434), sinon `http://localhost:11434`.
[[nodiscard]] std::string ollama_server(const core::AppConfig& config);

/// Modèle installé sur le serveur.
struct InstalledModel {
    std::string tag;
    std::uint64_t size_bytes = 0;
};
[[nodiscard]] core::Result<std::vector<InstalledModel>> installed_models(const std::string& server);

/// Avancement d'un téléchargement : étape (« pulling manifest »…), octets
/// reçus et total (0 si inconnu).
using PullProgress =
    std::function<void(std::string_view status, std::uint64_t completed, std::uint64_t total)>;
/// Télécharge `tag` sur le serveur (`/api/pull`), interruptible.
[[nodiscard]] core::Result<void> pull_model(const std::string& server, std::string_view tag,
                                            const PullProgress& progress,
                                            const std::stop_token& stop);
/// Supprime `tag` du serveur (`/api/delete`).
[[nodiscard]] core::Result<void> delete_model(const std::string& server, std::string_view tag);

} // namespace avenoir::llm
