#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

// Recherche des serveurs de modèles (Ollama, LM Studio, llama.cpp server…)
// pour la création guidée de la configuration.
namespace avenoir::llm {

/// Ports par défaut : Ollama, LM Studio, llama.cpp server.
inline constexpr std::array<std::uint16_t, 3> kDefaultServerPorts{11434, 1234, 8080};

/// Serveur compatible OpenAI trouvé, avec les modèles qu'il sert.
struct DiscoveredServer {
    std::string base_url; ///< « http://192.168.1.20:11434/v1 »
    std::vector<std::string> models;
};

/// Lit la réponse de `GET …/v1/models` (`{"data": [{"id": …}]}`). Les
/// identifiants sont des données d'un serveur : seuls les noms courts et
/// imprimables sont gardés (pas de séquence d'échappement vers le terminal),
/// 100 au plus. Rien si la réponse n'a pas ce format.
[[nodiscard]] std::optional<std::vector<std::string>>
parse_model_list(const nlohmann::json& response);

/// Modèles servis à `base_url` (« http://hôte:port/v1 »). Erreur si le
/// serveur est injoignable ou ne parle pas l'API OpenAI.
[[nodiscard]] core::Result<std::vector<std::string>>
list_server_models(const std::string& base_url, std::chrono::milliseconds timeout);

/// Adresse IPv4 privée (10/8, 172.16/12, 192.168/16) ?
[[nodiscard]] bool is_private_ipv4(std::string_view address);

/// Les 253 autres adresses du réseau /24 de `address` (privée), sans elle-même
/// ni l'adresse de diffusion. Vide si `address` n'est pas une IPv4 privée.
[[nodiscard]] std::vector<std::string> subnet_neighbours(std::string_view address);

/// Adresse IPv4 privée de cette machine sur le réseau local, ou rien (pas de
/// réseau, adresse publique : on ne sonde alors jamais le réseau).
[[nodiscard]] std::optional<std::string> local_network_address();

/// Question posée à un serveur : modèles servis à une adresse `…/v1`.
using ServerProbe = std::function<core::Result<std::vector<std::string>>(const std::string& url)>;

/// Serveurs de la machine elle-même (127.0.0.1, ports par défaut).
[[nodiscard]] std::vector<DiscoveredServer> discover_local_servers(const ServerProbe& probe = {});

/// Serveurs des machines `hosts`, ports par défaut, sondés en parallèle avec un
/// délai court. Arrêt dès que `stop` est demandé. Résultats dans l'ordre de
/// `hosts` puis des ports.
[[nodiscard]] std::vector<DiscoveredServer> discover_servers(const std::vector<std::string>& hosts,
                                                             const std::stop_token& stop,
                                                             const ServerProbe& probe = {});

} // namespace avenoir::llm
