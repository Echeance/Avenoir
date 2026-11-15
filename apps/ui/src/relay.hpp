#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

// Relais entre la page (vue web) et le démon. La page n'est pas tenue pour
// sûre : chacun de ses messages est validé ici, et elle ne peut demander au
// démon que les appels d'une liste fermée (docs/PROJECT.md § 4.6, décision
// 68). L'interface est un client du démon comme la CLI : mêmes messages
// « ui », mêmes questions.
namespace avenoir::ui {

/// Ce que le relais demande au démon (un `ipc::Peer` ; doublure dans les tests).
struct DaemonLink {
    /// Appel bloquant ; `stop` l'annule chez le démon, dont la réponse est attendue.
    std::function<core::Result<nlohmann::json>(std::string_view method, nlohmann::json params,
                                               const std::stop_token& stop)>
        call;
};

/// Appels du démon que la page peut demander.
[[nodiscard]] bool is_allowed_call(std::string_view method);
/// Questions du démon relayées à la page.
[[nodiscard]] bool is_question(std::string_view method);
/// Réponse de la page à la question `method`, validée ; invalide : le refus
/// (`refusal`).
[[nodiscard]] nlohmann::json checked_answer(std::string_view method, const nlohmann::json& result);
/// Réponse par défaut (page fermée, abandon) : toujours un refus.
[[nodiscard]] nlohmann::json refusal(std::string_view method);

/// Taille maximale d'un message de la page.
inline constexpr std::size_t kMaxPageMessage = std::size_t{64} * 1024;
/// Appels de la page en cours au plus.
inline constexpr std::size_t kMaxRunningCalls = 8;
/// Hauteur demandée par la page pour sa fenêtre, bornée.
inline constexpr int kMinHeight = 24;
inline constexpr int kMaxHeight = 900;

class Relay {
  public:
    /// Message vers la page ; appelée depuis n'importe quel fil.
    using Post = std::function<void(const nlohmann::json& message)>;
    /// Hauteur demandée par la page (fil de la fenêtre).
    using Resize = std::function<void(int height)>;

    Relay(DaemonLink daemon, Post post, Resize resize = {});
    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;
    Relay(Relay&&) = delete;
    Relay& operator=(Relay&&) = delete;
    /// Annule les appels en cours, répond « non » aux questions, attend la fin.
    ~Relay();

    /// Message reçu de la page (texte JSON). Mal formé : écarté.
    void from_page(std::string_view text);

    /// Message « ui » du démon.
    void ui(const nlohmann::json& params);
    /// Question du démon : attend la réponse de la page (ou `stop`, ou la
    /// fermeture) ; rend toujours une réponse valide, le refus par défaut.
    [[nodiscard]] nlohmann::json ask(std::string_view method, const nlohmann::json& params,
                                     const std::stop_token& stop);
    /// Connexion au démon perdue.
    void disconnected();

  private:
    struct Running {
        std::stop_source stop;
        std::shared_ptr<std::atomic<bool>> done;
        std::jthread thread;
    };

    void start_call(std::uint64_t id, const std::string& method, nlohmann::json params);
    void reap();

    DaemonLink daemon_;
    Post post_;
    Resize resize_;
    std::mutex mutex_; ///< Protège ce qui suit.
    bool closing_ = false;
    std::map<std::uint64_t, Running> running_;
    std::uint64_t next_question_ = 1;
    std::map<std::uint64_t, std::pair<std::string, std::promise<nlohmann::json>>> questions_;
};

} // namespace avenoir::ui
