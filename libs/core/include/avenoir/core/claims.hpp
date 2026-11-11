#pragma once

// Actions annoncées mais non faites : une réponse du modèle qui dit avoir agi
// (« j'ai lancé Firefox ») alors qu'aucun outil correspondant n'a été appelé
// pendant la requête (docs/PROJECT.md, décision 44).

#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/agent.hpp"

namespace avenoir::core {

/// Une famille d'actions : les phrases qui l'annoncent, et les outils qui la
/// font. Annoncée sans qu'aucun de ces outils ait été appelé = non faite.
struct ClaimRule {
    std::vector<std::string> tools;   ///< L'un d'eux appelé : l'annonce est fondée.
    std::vector<std::string> phrases; ///< En minuscules, apostrophe droite.
    std::string action;               ///< « éteindre ou redémarrer l'ordinateur ».
};

/// Minuscules (ASCII et capitales accentuées courantes), apostrophe
/// typographique remplacée par l'apostrophe droite, espaces insécables par
/// des espaces, `*` et `` ` `` du Markdown retirés : pour chercher une
/// phrase dans une réponse.
[[nodiscard]] std::string normalize_reply(std::string_view text);

/// Vérification de réponse (`Agent::add_reply_check`) à partir de règles.
[[nodiscard]] ReplyCheck make_claim_check(std::vector<ClaimRule> rules);

} // namespace avenoir::core
