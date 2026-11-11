#pragma once

// Instance SearXNG locale gérée par Avenoir (docs/PROJECT.md, décision 46) :
// l'image officielle `searxng/searxng`, lancée par podman (ou docker) à la
// première recherche, accessible seulement depuis la machine (127.0.0.1),
// arrêtée après une période d'inactivité ou quand Avenoir se ferme.
//
// L'image est téléchargée d'elle-même à la première recherche (annoncée,
// interrompue par Ctrl+C) ; si SearXNG ne peut pas tourner, la recherche
// passe par DuckDuckGo.

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "avenoir/core/error.hpp"

namespace avenoir::tools {

/// Sortie d'une commande : code de retour et sortie standard (bornée).
struct CommandResult {
    int exit_code = -1;
    std::string output;
};

/// Lance un programme par tableau d'arguments (jamais de shell).
using CommandRunner = std::function<core::Result<CommandResult>(
    const std::vector<std::string>& arguments, std::chrono::seconds timeout,
    const std::stop_token& stop)>;

/// Commande réelle (reproc) : sortie standard gardée (16 Ko), erreurs ignorées.
[[nodiscard]] CommandRunner default_command_runner();

struct SearxngOptions {
    /// Dossier de la configuration de l'instance : son `settings.yml` est
    /// monté en lecture seule dans le conteneur.
    std::filesystem::path config_dir;
    /// Arrêt après cette durée sans recherche.
    std::chrono::minutes idle{10};
    /// Annonce à l'utilisateur (téléchargement de l'image, long la première
    /// fois). Facultatif.
    std::function<void(std::string_view message)> notify;
    CommandRunner run;                                      ///< Vide : `default_command_runner()`.
    std::function<bool(const std::string& base_url)> ready; ///< Vide : GET `/healthz`.
    std::vector<std::string> runtimes{"podman", "docker"};
    std::chrono::milliseconds ready_timeout{30'000};
    std::chrono::milliseconds poll_interval{250};
    /// Vérification de l'inactivité ; zéro : aucun fil de surveillance
    /// (tests : `stop_if_idle` est appelé directement).
    std::chrono::milliseconds idle_check{15'000};
};

class SearxngHost {
  public:
    /// Image lancée, et nom du conteneur.
    static constexpr std::string_view kImage = "docker.io/searxng/searxng:latest";
    static constexpr std::string_view kContainer = "avenoir-searxng";

    explicit SearxngHost(SearxngOptions options);
    /// Arrête l'instance si elle tourne.
    ~SearxngHost();
    SearxngHost(const SearxngHost&) = delete;
    SearxngHost& operator=(const SearxngHost&) = delete;
    SearxngHost(SearxngHost&&) = delete;
    SearxngHost& operator=(SearxngHost&&) = delete;

    /// Adresse de l'instance (« http://127.0.0.1:43123 »), démarrée au
    /// besoin : runtime trouvé, configuration écrite, image téléchargée
    /// au besoin, conteneur lancé puis attendu. Compte comme une
    /// utilisation (repousse l'arrêt pour inactivité).
    [[nodiscard]] core::Result<std::string> ensure_started(const std::stop_token& stop);

    /// Arrête l'instance si elle tourne.
    void stop();

    /// Arrête l'instance si elle n'a pas servi depuis `idle` à `now`.
    /// Renvoie vrai si elle a été arrêtée.
    bool stop_if_idle(std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool running() const;

  private:
    SearxngOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::string runtime_;  ///< « podman » ou « docker », trouvé au premier démarrage.
    std::string base_url_; ///< Vide : arrêtée.
    std::chrono::steady_clock::time_point last_used_;
    std::jthread watcher_;

    [[nodiscard]] core::Result<std::string> start_locked(const std::stop_token& stop);
    void stop_locked();
    [[nodiscard]] core::Result<void> write_settings() const;
};

namespace detail {

/// Adresse publiée par `podman port avenoir-searxng 8080`
/// (« 127.0.0.1:43123 ») : « http://127.0.0.1:43123 », ou rien si
/// l'instance n'écoute pas sur la boucle locale.
[[nodiscard]] std::optional<std::string> searxng_base_url(std::string_view port_output);

/// `settings.yml` de l'instance : réglages par défaut de SearXNG, format JSON
/// activé, limiteur et proxy d'images coupés, clé secrète donnée.
[[nodiscard]] std::string searxng_settings(std::string_view secret_key);

} // namespace detail

} // namespace avenoir::tools
