#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Carte graphique partagée avec les jeux (décision 66) : repérer une
// application exigeante, pour décharger le modèle local ou passer au modèle
// léger le temps qu'elle tourne.
namespace avenoir::daemon {

/// Application qui demande la carte graphique.
struct DemandingApp {
    std::string name;               ///< Nom du programme.
    std::uint64_t video_memory = 0; ///< Mémoire vidéo occupée (Linux), en octets.
};

/// Application exigeante en cours, ou rien. Linux : le programme de
/// l'utilisateur qui occupe le plus de mémoire vidéo, s'il en occupe au
/// moins `threshold_bytes` (fdinfo de DRM ; Ollama, llama.cpp et Avenoir
/// exceptés). Windows : une application en plein écran (jeu Direct3D
/// compris), sauf le terminal et l'explorateur.
[[nodiscard]] std::optional<DemandingApp> demanding_app(std::uint64_t threshold_bytes);

/// Mémoire vidéo d'un fichier `fdinfo` de DRM (`drm-resident-vram`, sinon
/// `drm-memory-vram`), en octets, et son client (`drm-client-id`, pour ne
/// compter qu'une fois un client ouvert plusieurs fois) ; rien si ce n'est pas
/// un descripteur de carte graphique.
struct DrmUsage {
    std::string client;
    std::uint64_t video_memory = 0;
};
[[nodiscard]] std::optional<DrmUsage> parse_drm_fdinfo(std::string_view text);

/// Vrai pour les programmes jamais comptés comme exigeants : les serveurs de
/// modèles (Ollama, llama.cpp…) et Avenoir lui-même.
[[nodiscard]] bool is_model_server(std::string_view program);

} // namespace avenoir::daemon
