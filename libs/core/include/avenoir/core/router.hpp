#pragma once

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

namespace avenoir::core {

/// Commande reconnue par la voie rapide : un outil et ses arguments.
struct FastRoute {
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();

    bool operator==(const FastRoute&) const = default;
};

/// Voie rapide : reconnaît les commandes simples et fréquentes par des règles
/// et les exécute sans appeler le LLM (latence quasi nulle, hors ligne).
///
/// Les règles doivent rester **strictes** : en cas de doute, la requête part
/// au LLM plutôt que d'être mal interprétée.
class FastRouter {
  public:
    /// Règles par défaut (français) : URL, lancement d'application, heure.
    [[nodiscard]] static Result<FastRouter> with_defaults();

    /// Ajoute une règle (insensible à la casse, sur la phrase entière). Si
    /// `arg_name` n'est pas vide, le premier groupe capturant de `pattern`
    /// remplit cet argument de l'outil. La première règle qui correspond gagne.
    [[nodiscard]] Result<void> add_rule(std::string_view pattern, std::string tool,
                                        std::string arg_name = {});

    [[nodiscard]] std::optional<FastRoute> route(std::string_view input) const;

  private:
    struct Rule {
        std::regex regex;
        std::string tool;
        std::string arg_name;
    };

    std::vector<Rule> rules_;
};

} // namespace avenoir::core
