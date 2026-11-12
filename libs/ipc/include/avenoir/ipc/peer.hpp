#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/ipc/transport.hpp"

// JSON-RPC 2.0 dans les deux sens sur une `Connection` : chaque côté peut
// appeler l'autre (la CLI demande une requête ; pendant qu'elle tourne, le
// démon demande une confirmation à la CLI). Un fil lit les messages ; chaque
// requête reçue est traitée dans son propre fil, pour que la réponse à un
// appel en sens inverse puisse arriver pendant ce temps.
namespace avenoir::ipc {

class Peer {
  public:
    /// Traite une requête reçue. `stop` est demandé si l'appelant l'annule
    /// (`$/cancelRequest`) ou si la connexion se ferme.
    using RequestHandler = std::function<core::Result<nlohmann::json>(const nlohmann::json& params,
                                                                      const std::stop_token& stop)>;
    using NotificationHandler = std::function<void(const nlohmann::json& params)>;

    explicit Peer(std::unique_ptr<Connection> connection);
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&&) = delete;
    Peer& operator=(Peer&&) = delete;
    /// Ferme la connexion et attend la fin des fils.
    ~Peer();

    /// À déclarer avant `start`.
    void on_request(std::string method, RequestHandler handler);
    void on_notification(std::string method, NotificationHandler handler);
    /// Appelée une fois quand la connexion se ferme (de l'un ou l'autre côté).
    void on_closed(std::function<void()> handler);

    /// Lance le fil de lecture.
    void start();

    /// Appelle `method` de l'autre côté et attend sa réponse. Si `stop` est
    /// demandé, l'appel est annulé chez l'autre (`$/cancelRequest`) et
    /// l'erreur `Cancelled` rendue.
    [[nodiscard]] core::Result<nlohmann::json> call(std::string_view method, nlohmann::json params,
                                                    const std::stop_token& stop);

    /// Message sans réponse (texte qui arrive au fil de l'eau…).
    [[nodiscard]] core::Result<void> notify(std::string_view method, nlohmann::json params);

    /// Ferme la connexion : les appels en attente échouent.
    void close();
    [[nodiscard]] bool is_closed() const noexcept { return closed_; }

  private:
    void read_loop(const std::stop_token& stop);
    void dispatch(nlohmann::json message);
    void handle_request(const nlohmann::json& id, const std::string& method,
                        const nlohmann::json& params);
    void fail_pending(const std::string& reason);
    core::Result<void> send(const nlohmann::json& message);
    /// Envoi dont l'échec ne change rien (réponse à un pair parti) : noté seulement.
    void send_quietly(const nlohmann::json& message);
    void reap_finished();

    struct Running {
        std::stop_source stop;
        std::jthread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    std::unique_ptr<Connection> connection_;
    std::map<std::string, RequestHandler, std::less<>> request_handlers_;
    std::map<std::string, NotificationHandler, std::less<>> notification_handlers_;
    std::function<void()> closed_handler_;

    std::mutex mutex_; ///< Protège ce qui suit.
    std::int64_t next_id_ = 1;
    std::map<std::int64_t, std::promise<core::Result<nlohmann::json>>> pending_;
    std::map<std::string, Running> running_; ///< Requêtes reçues en cours, par id.

    std::atomic<bool> closed_{false};
    std::jthread reader_;
};

} // namespace avenoir::ipc
