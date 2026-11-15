#pragma once

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/modules/manifest.hpp"

// Onglets déclaratifs des modules (docs/PROJECT.md § 2.7, décision 73) : le
// module décrit son contenu, Avenoir le dessine ; aucun code du module ne
// s'exécute dans l'interface. Ce qu'il renvoie est une donnée non fiable :
// validé ici avant d'aller plus loin.
namespace avenoir::modules {

/// Composants au plus dans un onglet.
inline constexpr std::size_t kMaxTabComponents = 100;
/// Lignes d'un tableau, éléments d'une liste au plus.
inline constexpr std::size_t kMaxTabRows = 50;
/// Colonnes d'un tableau au plus.
inline constexpr std::size_t kMaxTabColumns = 8;
/// Octets d'un texte au plus (au-delà : coupé).
inline constexpr std::size_t kMaxTabText = 500;

/// Contenu d'un onglet renvoyé par le module (`avenoir/tab`), réduit à ce
/// qu'Avenoir sait dessiner : `{"components": [...]}` avec, pour chacun,
/// `heading` (text), `text` (text, tone : « muted » ou « warning »), `stat`
/// (label, value), `list` (items), `table` (columns, rows), `button` (label,
/// command, argument). Un composant inconnu ou mal formé est écarté ; les
/// textes sont nettoyés (caractères de contrôle, marques bidirectionnelles)
/// et bornés ; un bouton ne peut lancer qu'une commande du module
/// (`manifest.commands`), son argument est borné à 100 octets. Erreur si le
/// résultat n'est pas un objet avec une liste `components`.
[[nodiscard]] core::Result<nlohmann::json> validate_tab(const nlohmann::json& result,
                                                        const Manifest& manifest);

} // namespace avenoir::modules
