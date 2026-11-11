#pragma once

#include <optional>
#include <string>

// Code propre à chaque système (src/platform/<os>.cpp).
namespace avenoir::llm::platform {

/// Adresse IPv4 de la machine sur l'interface de la route par défaut
/// (« 192.168.1.12 »), sans rien envoyer sur le réseau ; rien si la machine
/// n'a pas de route ou si le système n'est pas pris en charge.
[[nodiscard]] std::optional<std::string> default_route_ipv4();

} // namespace avenoir::llm::platform
