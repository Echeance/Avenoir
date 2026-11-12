#pragma once

#include <optional>
#include <span>
#include <string>

#include <nlohmann/json.hpp>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

// Provenance des arguments (docs/PROJECT.md § 3, directive 5 : les données ne
// sont pas des instructions). Une action dont un argument vient d'un résultat
// d'outil — une adresse lue dans une page, un nom glissé dans une réponse de
// module — et non d'une demande de l'utilisateur se fait confirmer, même si
// son niveau de risque l'autorise d'ordinaire : c'est ainsi qu'une injection
// ferait agir le modèle.
namespace avenoir::core {

/// Premier argument de `call` (texte) qui apparaît dans un résultat d'outil de
/// `history` sans apparaître dans aucun message de l'utilisateur, ou rien.
/// Ne sont pas examinés : les outils en lecture seule, les valeurs d'une
/// liste fermée (`enum` du schéma), les textes trop courts (moins de 3
/// caractères) ou trop longs (plus de 200 : consignes, messages). Pour une
/// adresse web, son domaine cité par l'utilisateur suffit.
///
/// `requester` : pour un agent qui travaille pour un autre (agent d'une
/// équipe), le texte des demandes de l'utilisateur ; ses propres messages
/// (la tâche écrite par le coordinateur) comptent alors comme des données.
[[nodiscard]] std::optional<std::string>
untrusted_argument(const ToolSpec& spec, const nlohmann::json& arguments,
                   std::span<const Message> history,
                   const std::optional<std::string>& requester = std::nullopt);

} // namespace avenoir::core
