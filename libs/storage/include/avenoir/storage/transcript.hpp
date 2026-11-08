#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "avenoir/core/message.hpp"

namespace avenoir::storage {

/// Transcription Markdown d'une conversation (/transcription, ou avant un
/// /reset ou un /supprimer) : un en-tête (titre, numéro, date, nombre
/// d'échanges), puis une rubrique « › Vous » ou « ◆ Avenoir » par message. Les
/// appels d'outils sont repliés (`<details>`) dans la réponse, avec leur
/// résultat.
///
/// Sûreté à l'ouverture dans un lecteur Markdown : les résultats d'outils
/// (données venues du web, de fichiers, de modules) sont mis dans des blocs de
/// code, jamais interprétés ; dans les messages, hors code, le HTML est
/// neutralisé et les images ne sont pas affichées (une image distante
/// trahirait l'ouverture du fichier). Le code des messages reste intact.
[[nodiscard]] std::string transcript_markdown(std::string_view title, std::int64_t id,
                                              std::chrono::local_time<std::chrono::minutes> date,
                                              std::span<const core::Message> messages);

/// Nom du fichier de transcription : « AAAA-MM-JJ-HHMM-n<id>-<titre>.md », où
/// le titre est réduit à [a-z0-9-] (accents retirés, 40 caractères au plus) :
/// jamais de séparateur de chemin ni de caractère interdit par un système de
/// fichiers.
[[nodiscard]] std::string transcript_file_name(std::chrono::local_time<std::chrono::minutes> date,
                                               std::int64_t id, std::string_view title);

} // namespace avenoir::storage
