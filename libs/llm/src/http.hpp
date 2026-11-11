#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
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

/// POST en flux (Server-Sent Events) : chaque ligne `data: …` reçue est
/// donnée à `on_data`, sans le préfixe, dès son arrivée ; `on_data` renvoie
/// une erreur pour tout arrêter. Statut non 2xx : erreur lisible, comme
/// `post_json`. Interrompu si `stop` est demandé.
[[nodiscard]] core::Result<void>
post_stream(const HttpRequest& request,
            const std::function<core::Result<void>(std::string_view data)>& on_data,
            const std::stop_token& stop);

/// GET JSON, court délai (questions rapides à un serveur local, comme la
/// liste des modèles chargés). Mêmes règles d'erreur que `post_json`.
[[nodiscard]] core::Result<nlohmann::json> get_json(const std::string& url,
                                                    std::chrono::milliseconds timeout);

/// Tronque un texte pour l'inclure dans un message d'erreur.
[[nodiscard]] std::string excerpt(std::string_view text, std::size_t max_bytes = 500);

} // namespace avenoir::llm::detail
