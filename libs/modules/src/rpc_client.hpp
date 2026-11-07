#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

#include "avenoir/core/error.hpp"

// API C de reproc (voir rpc_client.cpp).
struct reproc_t;

namespace avenoir::modules {

/// Processus à lancer : rien ne passe par un shell.
struct ProcessSpec {
    std::vector<std::string> arguments; ///< Programme (chemin résolu) puis arguments.
    std::filesystem::path working_directory;
    std::map<std::string, std::string> environment; ///< Environnement **complet** du processus.
    std::shared_ptr<spdlog::logger> log;            ///< Reçoit le stderr du module (non nul).
};

/// Taille maximale d'un message reçu : au-delà, le module est arrêté.
inline constexpr std::size_t kMaxMessageBytes = std::size_t{8} << 20U;

/// Client JSON-RPC 2.0 sur l'entrée et la sortie standard d'un processus,
/// une ligne par message (transport stdio de MCP).
///
/// Un thread lit les sorties du module : réponses associées aux requêtes en
/// attente par leur `id`, stderr renvoyé vers le log du module. Les écritures
/// se font depuis le thread appelant, sous verrou. L'API C de reproc n'est
/// pas déclarée sûre entre threads, mais le thread de lecture ne touche
/// qu'aux tubes de sortie (`reproc_poll` sans `REPROC_EVENT_IN`,
/// `reproc_read`) et les écritures qu'au tube d'entrée (`reproc_write`).
/// La surcouche C++ ne convient pas : son `poll` déplace l'objet processus.
///
/// Le module est **non fiable** : messages bornés en taille, réponses sans
/// `id` connu ignorées, requêtes du module refusées (pas encore d'appels du
/// module vers l'hôte), délai maximal par requête.
class RpcClient {
    /// Réserve le constructeur à `start` tout en permettant std::make_unique.
    struct Key {
        explicit Key() = default;
    };

  public:
    [[nodiscard]] static core::Result<std::unique_ptr<RpcClient>> start(ProcessSpec spec);
    RpcClient(Key /*key*/, ProcessSpec spec);

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;
    RpcClient(RpcClient&&) = delete;
    RpcClient& operator=(RpcClient&&) = delete;
    /// Ferme l'entrée du module, puis le termine (et le tue s'il insiste).
    ~RpcClient();

    /// Envoie une requête et attend sa réponse (`result`), au plus `timeout`.
    /// `stop` interrompt l'attente : le module reçoit alors
    /// `notifications/cancelled`. Une réponse `error` devient une erreur.
    [[nodiscard]] core::Result<nlohmann::json> request(std::string_view method,
                                                       nlohmann::json params,
                                                       std::chrono::milliseconds timeout,
                                                       const std::stop_token& stop = {});

    /// Envoie une notification (sans réponse).
    [[nodiscard]] core::Result<void> notify(std::string_view method, nlohmann::json params);

    /// Faux dès que le module s'est arrêté ou a été arrêté.
    [[nodiscard]] bool alive() const;
    /// Pourquoi le module s'est arrêté (vide s'il tourne).
    [[nodiscard]] std::string failure() const;

  private:
    struct Pending {
        bool done = false;
        nlohmann::json response;
    };

    /// Thread de lecture.
    void read_loop(const std::stop_token& stop);
    void handle_line(std::string_view line);
    void handle_message(const nlohmann::json& message);
    /// Marque le module comme arrêté et réveille les requêtes en attente.
    void fail(std::string reason);
    /// Écrit une ligne entière, sans bloquer au-delà de `deadline`.
    [[nodiscard]] core::Result<void> write_line(const std::string& line,
                                                std::chrono::steady_clock::time_point deadline);

    struct Destroy {
        void operator()(reproc_t* process) const noexcept;
    };

    ProcessSpec spec_;
    std::unique_ptr<reproc_t, Destroy> process_;
    std::mutex write_mutex_; ///< Une seule écriture à la fois sur l'entrée du module.

    mutable std::mutex mutex_; ///< Protège ce qui suit.
    std::condition_variable_any changed_;
    std::map<std::int64_t, Pending> pending_;
    std::int64_t next_id_ = 0;
    bool alive_ = true;
    std::string failure_;

    std::string out_buffer_; ///< Sortie standard en attente d'une fin de ligne.
    std::string err_buffer_; ///< Sortie d'erreur en attente d'une fin de ligne.
    std::jthread reader_;    ///< Dernier membre : arrêté et joint en premier.
};

} // namespace avenoir::modules
