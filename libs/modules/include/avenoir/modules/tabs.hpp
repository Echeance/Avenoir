#pragma once

#include <cstddef>
#include <string_view>

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
/// Octets d'une image (adresse `data:` entière) au plus.
inline constexpr std::size_t kMaxTabImage = std::size_t{256} * 1024;
/// Images au plus dans un onglet.
inline constexpr std::size_t kMaxTabImages = 4;
/// Séries d'un graphique au plus.
inline constexpr std::size_t kMaxChartSeries = 4;
/// Choix d'un formulaire au plus.
inline constexpr std::size_t kMaxFormOptions = 20;

/// Contenu d'un onglet renvoyé par le module (`avenoir/tab`), réduit à ce
/// qu'Avenoir sait dessiner : `{"components": [...]}` avec, pour chacun,
/// `heading` (text), `text` (text, tone : « muted » ou « warning »), `stat`
/// (label, value), `list` (items), `table` (columns, rows), `button` (label,
/// command, argument), `image` (src : `data:image/png|jpeg|gif|webp;base64,…`,
/// alt), `chart` (kind : « bar » ou « line », labels, series : name et
/// values numériques, title, unit) et `form` (command, label, placeholder,
/// options, submit : un champ qui remplit l'argument d'une commande du
/// module). Un composant inconnu ou mal formé est écarté ; les
/// textes sont nettoyés (caractères de contrôle, marques bidirectionnelles)
/// et bornés ; un bouton ne peut lancer qu'une commande du module
/// (`manifest.commands`), son argument est borné à 100 octets. Erreur si le
/// résultat n'est pas un objet avec une liste `components`.
[[nodiscard]] core::Result<nlohmann::json> validate_tab(const nlohmann::json& result,
                                                        const Manifest& manifest);

/// Données d'un onglet web, en JSON, au plus.
inline constexpr std::size_t kMaxWebTabData = std::size_t{64} * 1024;

/// Réponse du module pour un onglet web (`avenoir/tab`, `avenoir/tab_event`) :
/// `{"data": <JSON>}` gardé seulement si `data` tient en `kMaxWebTabData`
/// octets une fois sérialisé (sinon erreur) ; absent : `{"data": null}`.
[[nodiscard]] core::Result<nlohmann::json> validate_web_data(const nlohmann::json& result,
                                                             const Manifest& manifest);

/// Nom d'événement d'un onglet web : `[a-z0-9_-]`, 1 à 64.
[[nodiscard]] bool is_tab_event_name(std::string_view name);

} // namespace avenoir::modules
