#include "avenoir/ipc/peer.hpp"

#include <atomic>
#include <chrono>
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
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/core/error.hpp"

namespace avenoir::ipc {

namespace {

using nlohmann::json;

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kRequestFailed = -32000;
constexpr int kRequestCancelled = -32800;
constexpr std::string_view kCancelMethod = "$/cancelRequest";
constexpr std::chrono::milliseconds kPoll{100};

json error_object(const core::Error& error) {
    return {{"code", error.code == core::ErrorCode::Cancelled ? kRequestCancelled : kRequestFailed},
            {"message", error.message},
            {"data", {{"category", static_cast<int>(error.code)}}}};
}

/// Erreur reçue → `core::Error`, catégorie gardée si elle est connue.
core::Error error_from(const json& object) {
    core::Error error{.code = core::ErrorCode::Ipc, .message = "réponse d'erreur illisible"};
    if (!object.is_object()) {
        return error;
    }
    if (object.contains("message") && object["message"].is_string()) {
        error.message = object["message"].get<std::string>();
    }
    if (object.value("code", 0) == kRequestCancelled) {
        error.code = core::ErrorCode::Cancelled;
    } else if (object.contains("data") && object["data"].is_object() &&
               object["data"].contains("category") &&
               object["data"]["category"].is_number_integer()) {
        const int category = object["data"]["category"].get<int>();
        if (category >= 0 && category <= static_cast<int>(core::ErrorCode::Ipc)) {
            error.code = static_cast<core::ErrorCode>(category);
        }
    }
    return error;
}

} // namespace

Peer::Peer(std::unique_ptr<Connection> connection) : connection_(std::move(connection)) {}

Peer::~Peer() {
    close();
    if (reader_.joinable()) {
        reader_.request_stop();
        reader_.join();
    }
    std::map<std::string, Running> running;
    {
        const std::scoped_lock lock(mutex_);
        running = std::move(running_);
        running_.clear();
    }
    for (auto& [id, request] : running) {
        request.stop.request_stop();
    }
    running.clear(); // attend la fin de chaque requête
}

void Peer::on_request(std::string method, RequestHandler handler) {
    request_handlers_.insert_or_assign(std::move(method), std::move(handler));
}

void Peer::on_notification(std::string method, NotificationHandler handler) {
    notification_handlers_.insert_or_assign(std::move(method), std::move(handler));
}

void Peer::on_closed(std::function<void()> handler) {
    closed_handler_ = std::move(handler);
}

void Peer::start() {
    reader_ = std::jthread([this](const std::stop_token& stop) { read_loop(stop); });
}

void Peer::close() {
    if (!closed_.exchange(true)) {
        connection_->close();
    }
}

void Peer::send_quietly(const json& message) {
    if (auto sent = send(message); !sent) {
        spdlog::debug("message non transmis : {}", sent.error().message);
    }
}

core::Result<void> Peer::send(const json& message) {
    if (closed_) {
        return core::make_error(core::ErrorCode::Ipc, "connexion fermée");
    }
    return connection_->write_line(message.dump(-1, ' ', false, json::error_handler_t::replace));
}

core::Result<json> Peer::call(std::string_view method, json params, const std::stop_token& stop) {
    std::int64_t id = 0;
    std::future<core::Result<json>> answer;
    {
        const std::scoped_lock lock(mutex_);
        id = next_id_++;
        auto [entry, inserted] = pending_.try_emplace(id);
        answer = entry->second.get_future();
    }
    const json request = {
        {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
    if (auto sent = send(request); !sent) {
        const std::scoped_lock lock(mutex_);
        pending_.erase(id);
        return std::unexpected(std::move(sent.error()));
    }
    while (answer.wait_for(kPoll) != std::future_status::ready) {
        if (stop.stop_requested()) {
            {
                const std::scoped_lock lock(mutex_);
                pending_.erase(id);
            }
            if (auto sent = notify(kCancelMethod, {{"id", id}}); !sent) {
                spdlog::debug("annulation non transmise : {}", sent.error().message);
            }
            return core::make_error(core::ErrorCode::Cancelled, {});
        }
    }
    return answer.get();
}

core::Result<void> Peer::notify(std::string_view method, json params) {
    return send({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

void Peer::fail_pending(const std::string& reason) {
    const std::scoped_lock lock(mutex_);
    for (auto& [id, promise] : pending_) {
        promise.set_value(core::make_error(core::ErrorCode::Ipc, reason));
    }
    pending_.clear();
}

void Peer::read_loop(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        auto line = connection_->read_line(stop);
        if (!line) {
            if (line.error().code != core::ErrorCode::Cancelled) {
                spdlog::warn("connexion interrompue : {}", core::describe(line.error()));
            }
            break;
        }
        if (!*line) {
            break; // fermée de l'autre côté
        }
        json message = json::parse(**line, nullptr, /*allow_exceptions=*/false);
        if (message.is_discarded() || !message.is_object()) {
            send_quietly({{"jsonrpc", "2.0"},
                          {"id", nullptr},
                          {"error", {{"code", kParseError}, {"message", "JSON illisible"}}}});
            continue;
        }
        dispatch(std::move(message));
    }
    closed_ = true;
    connection_->close();
    fail_pending("connexion au démon fermée");
    {
        const std::scoped_lock lock(mutex_);
        for (auto& [id, request] : running_) {
            request.stop.request_stop();
        }
    }
    if (closed_handler_) {
        closed_handler_();
    }
}

void Peer::dispatch(json message) {
    const bool has_method = message.contains("method") && message["method"].is_string();
    const bool has_id = message.contains("id") && !message["id"].is_null();
    const json params = message.contains("params") ? message["params"] : json::object();

    if (has_method) {
        const auto method = message["method"].get<std::string>();
        if (has_id) {
            handle_request(message["id"], method, params);
            return;
        }
        if (method == kCancelMethod) {
            if (params.is_object() && params.contains("id")) {
                const std::scoped_lock lock(mutex_);
                if (const auto found = running_.find(params["id"].dump());
                    found != running_.end()) {
                    found->second.stop.request_stop();
                }
            }
            return;
        }
        if (const auto handler = notification_handlers_.find(method);
            handler != notification_handlers_.end()) {
            handler->second(params);
        }
        return;
    }

    // Réponse à l'un de nos appels.
    if (!has_id || !message["id"].is_number_integer()) {
        return;
    }
    const auto id = message["id"].get<std::int64_t>();
    std::promise<core::Result<json>> promise;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = pending_.find(id);
        if (found == pending_.end()) {
            return; // appel annulé entre-temps
        }
        promise = std::move(found->second);
        pending_.erase(found);
    }
    if (message.contains("error")) {
        promise.set_value(std::unexpected(error_from(message["error"])));
    } else {
        promise.set_value(message.contains("result") ? message["result"] : json());
    }
}

void Peer::reap_finished() {
    // Appelée verrou pris : retire les requêtes terminées (leur fil a fini).
    std::erase_if(running_, [](const auto& entry) { return entry.second.done->load(); });
}

void Peer::handle_request(const json& id, const std::string& method, const json& params) {
    const auto handler = request_handlers_.find(method);
    if (handler == request_handlers_.end()) {
        send_quietly(
            {{"jsonrpc", "2.0"},
             {"id", id},
             {"error", {{"code", kMethodNotFound}, {"message", "méthode inconnue : " + method}}}});
        return;
    }
    if (!id.is_number_integer() && !id.is_string()) {
        send_quietly({{"jsonrpc", "2.0"},
                      {"id", nullptr},
                      {"error", {{"code", kInvalidRequest}, {"message", "identifiant invalide"}}}});
        return;
    }
    const std::scoped_lock lock(mutex_);
    reap_finished();
    const std::string key = id.dump();
    if (running_.contains(key)) {
        return; // identifiant déjà en cours : ignoré
    }
    const auto done = std::make_shared<std::atomic<bool>>(false);
    Running& request = running_[key];
    request.done = done;
    const std::stop_token stop = request.stop.get_token();
    request.thread = std::jthread([this, id, params, stop, done, run = handler->second] {
        auto result = run(params, stop);
        json response = {{"jsonrpc", "2.0"}, {"id", id}};
        if (result) {
            response["result"] = std::move(*result);
        } else {
            response["error"] = error_object(result.error());
        }
        send_quietly(response);
        done->store(true);
    });
}

} // namespace avenoir::ipc
