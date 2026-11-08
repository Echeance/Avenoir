#pragma once

// Requêtes de référence (evals/requetes.toml) et vérification de ce qu'un
// modèle en a fait. Logique sans LLM ni réseau, testée dans tests/.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/core/message.hpp"
#include "avenoir/core/permission.hpp"

namespace avenoir::evals {

/// Appel d'outil attendu. Les arguments donnés doivent figurer dans l'appel
/// réel (les autres sont libres) : texte contenu, sans casse ; autre valeur
/// égale.
struct ExpectedCall {
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();
};

/// Un cas : une ou plusieurs requêtes d'une même conversation, puis des
/// vérifications sur l'ensemble des appels d'outils et sur la dernière réponse.
struct Case {
    std::string id;
    std::string category;
    std::vector<std::string> requests;
    /// Réponse donnée aux confirmations demandées pendant le cas.
    core::Decision confirmation = core::Decision::Rejected;
    /// Politique propre au cas ; absente : celle par défaut.
    std::optional<core::PermissionPolicy> policy;

    std::vector<ExpectedCall> expect;
    std::vector<std::string> forbid;              ///< Outils qui ne doivent pas être appelés.
    std::vector<std::string> forbid_in_arguments; ///< Textes absents de tout argument.
    std::optional<std::size_t> max_calls;         ///< Nombre d'appels d'outils maximal.
    std::vector<std::string> reply_contains;      ///< Tous présents (sans casse).
    std::vector<std::string> reply_contains_any;  ///< Au moins un présent.
    std::vector<std::string> reply_excludes;      ///< Aucun présent.

    /// Résultats des doublures propres au cas (remplacent ceux par défaut).
    std::map<std::string, std::string, std::less<>> results;
    /// Outils dont la doublure échoue, avec le message d'erreur.
    std::map<std::string, std::string, std::less<>> failures;
};

struct Suite {
    /// Résultat par défaut de chaque doublure. `{arg}` est remplacé par
    /// l'argument texte ou nombre `arg`, `{arg|défaut}` aussi, avec une valeur
    /// de repli s'il est absent.
    std::map<std::string, std::string, std::less<>> results;
    std::vector<Case> cases;
};

/// Lit un fichier de cas ; les erreurs indiquent le cas et la clé fautifs.
/// Une clé inconnue est une erreur (faute de frappe).
[[nodiscard]] core::Result<Suite> parse_suite(std::string_view toml_text);
[[nodiscard]] core::Result<Suite> load_suite(const std::filesystem::path& path);

/// Résultat d'une doublure : modèle avec `{arg}` remplacés (voir `Suite`).
[[nodiscard]] std::string fill_template(std::string_view text, const nlohmann::json& arguments);

/// Ce qu'a fait le modèle pendant un cas.
struct Transcript {
    std::vector<core::ToolCall> calls; ///< Appels demandés, même refusés ensuite.
    std::vector<std::string>
        confirmations;                ///< Outils pour lesquels une confirmation a été demandée.
    std::string reply;                ///< Réponse à la dernière requête.
    std::optional<std::string> error; ///< Erreur de l'agent ou du fournisseur.
    std::chrono::milliseconds duration{0};
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::optional<double> cost_usd;
};

/// Vérifie un cas : liste des manquements, vide si le cas est réussi.
[[nodiscard]] std::vector<std::string> check(const Case& test, const Transcript& transcript);

/// « open_url {"url":"https://…"} », pour les rapports.
[[nodiscard]] std::string describe_call(const core::ToolCall& call);

} // namespace avenoir::evals
