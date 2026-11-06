#pragma once

#include <chrono>
#include <cstddef>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

namespace avenoir::llm::detail {

struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers; ///< Peuvent contenir une clé.
    std::string body;
    std::chrono::seconds timeout{180};
};

/// POST JSON. Renvoie le corps de la réponse si le statut est 2xx ; sinon une
/// erreur lisible (statut + message de l'API). Interrompu si `stop` est demandé.
/// Les en-têtes ne sont jamais recopiés dans les erreurs ni les logs.
[[nodiscard]] core::Result<nlohmann::json> post_json(const HttpRequest& request,
                                                     const std::stop_token& stop);

/// Tronque un texte pour l'inclure dans un message d'erreur.
[[nodiscard]] std::string excerpt(std::string_view text, std::size_t max_bytes = 500);

} // namespace avenoir::llm::detail
