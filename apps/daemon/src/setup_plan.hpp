#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/llm/discovery.hpp"

// Création guidée de la configuration : ce qui se décide sans terminal ni
// réseau (testé à part), à partir de ce qu'Avenoir a trouvé.
namespace avenoir::daemon {

/// Ce que la création guidée a trouvé.
struct SetupFindings {
    std::vector<llm::DiscoveredServer> local_servers; ///< Sur cette machine.
    /// Sur le réseau, déclarés par l'utilisateur comme lui appartenant
    /// (`local = true` : pas de confirmation pour y passer).
    std::vector<llm::DiscoveredServer> owned_servers;
    /// Saisis à la main mais pas déclarés à soi : tenus pour distants.
    std::vector<llm::DiscoveredServer> remote_servers;
    bool claude_code = false;   ///< `claude` trouvé dans le PATH.
    bool anthropic_key = false; ///< ANTHROPIC_API_KEY définie.
};

/// Modèle proposé à l'utilisateur.
struct SetupCandidate {
    core::NamedLlm model;
    std::string label;   ///< « gemma4:26b-a4b, Ollama sur cette machine »
    std::string note;    ///< Avis tiré du banc d'essai, ou « non évalué ».
    bool remote = false; ///< La conversation quitte la machine et le réseau.
    bool recommended = false;
};

/// Modèles proposés : d'abord ceux conseillés par le banc d'essai, puis les
/// autres modèles locaux, puis les distants (écartés en mode « local
/// uniquement »). Noms de section uniques.
[[nodiscard]] std::vector<SetupCandidate> make_candidates(const SetupFindings& findings,
                                                          bool local_only);

/// Nom de section (`[models.<nom>]`) tiré d'un identifiant de modèle :
/// « qwen3.8:27b-q4_K_M » → « qwen3-8 », unique parmi `taken`.
[[nodiscard]] std::string model_name_for(std::string_view model_id,
                                         const std::vector<std::string>& taken);

/// Numéros saisis (« 1 3 », « 2,4 »), de 1 à `count`, sans doublon, dans
/// l'ordre donné ; rien si la saisie est invalide. Saisie vide : rien non
/// plus (l'appelant applique le choix par défaut).
[[nodiscard]] std::optional<std::vector<std::size_t>> parse_selection(std::string_view text,
                                                                      std::size_t count);

/// Configuration à enregistrer : les réglages de `base`, avec pour modèles
/// `chosen` dans l'ordre (le premier sert par défaut) et le mode « local
/// uniquement » choisi.
[[nodiscard]] core::AppConfig build_setup_config(const std::vector<SetupCandidate>& chosen,
                                                 bool local_only, const core::AppConfig& base = {});

/// Adresse de serveur saisie à la main : `http(s)://…`, sans espace,
/// caractère de contrôle ni identifiants ; « /v1 » ajouté s'il manque.
[[nodiscard]] std::optional<std::string> normalize_server_url(std::string_view text);

} // namespace avenoir::daemon
