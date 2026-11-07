#include "rpc_client.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <reproc/reproc.h>
#include <spdlog/spdlog.h>

#include "platform.hpp"

namespace avenoir::modules {

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

/// Taille maximale d'une ligne de stderr conservée dans le log.
constexpr std::size_t kMaxLogLine = 2000;

/// Code JSON-RPC : méthode inconnue.
constexpr int kMethodNotFound = -32601;

std::unexpected<core::Error> module_error(std::string message) {
    return core::make_error(core::ErrorCode::Module, std::move(message));
}

bool has_event(int events, int flag) {
    return (static_cast<unsigned>(events) & static_cast<unsigned>(flag)) != 0U;
}

/// Une ligne JSON ; un texte UTF-8 invalide est remplacé plutôt que de lever.
std::string dump(const json& message) {
    return message.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
}

std::string reproc_message(int code) {
    return reproc_strerror(code);
}

/// Supprime les caractères de contrôle d'un texte venant du module avant de
/// l'écrire dans un log (pas de séquence d'échappement du terminal).
std::string sanitize(std::string_view text) {
    std::string out;
    out.reserve(std::min(text.size(), kMaxLogLine));
    for (const char c : text.substr(0, kMaxLogLine)) {
        const auto byte = static_cast<unsigned char>(c);
        out += (byte < 0x20 && c != '\t') || byte == 0x7F ? '?' : c;
    }
    return out;
}

/// Message d'erreur lisible d'une réponse `error` JSON-RPC.
std::string describe_rpc_error(const json& error) {
    std::string message = "réponse d'erreur du module";
    if (error.is_object()) {
        if (const auto it = error.find("message"); it != error.end() && it->is_string()) {
            message = sanitize(it->get<std::string>());
        }
        if (const auto it = error.find("code"); it != error.end() && it->is_number_integer()) {
            message += " (code " + std::to_string(it->get<std::int64_t>()) + ")";
        }
    }
    return message;
}

} // namespace

RpcClient::RpcClient(Key /*key*/, ProcessSpec spec) : spec_(std::move(spec)) {}

void RpcClient::Destroy::operator()(reproc_t* process) const noexcept {
    reproc_destroy(process);
}

core::Result<std::unique_ptr<RpcClient>> RpcClient::start(ProcessSpec spec) {
    if (spec.arguments.empty() || spec.log == nullptr) {
        return module_error("lancement : programme ou log absent");
    }
    // Écrire vers un module arrêté ne doit pas tuer Avenoir (SIGPIPE).
    platform::ignore_broken_pipe();

    auto client = std::make_unique<RpcClient>(Key{}, std::move(spec));
    client->process_.reset(reproc_new());
    if (!client->process_) {
        return module_error("mémoire insuffisante");
    }

    // Tableaux terminés par nullptr, comme le veut l'API C.
    std::vector<const char*> argv;
    argv.reserve(client->spec_.arguments.size() + 1);
    for (const auto& argument : client->spec_.arguments) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    std::vector<std::string> variables;
    variables.reserve(client->spec_.environment.size());
    for (const auto& [name, value] : client->spec_.environment) {
        variables.push_back(std::format("{}={}", name, value));
    }
    std::vector<const char*> envp;
    envp.reserve(variables.size() + 1);
    for (const auto& variable : variables) {
        envp.push_back(variable.c_str());
    }
    envp.push_back(nullptr);
    const std::string directory = client->spec_.working_directory.string();

    reproc_options options{};
    options.working_directory = directory.c_str();
    // Environnement complet fourni par l'hôte : rien n'est hérité d'Avenoir.
    options.env.behavior = REPROC_ENV_EMPTY;
    options.env.extra = envp.data();
    options.redirect.in.type = REPROC_REDIRECT_PIPE;
    options.redirect.out.type = REPROC_REDIRECT_PIPE;
    options.redirect.err.type = REPROC_REDIRECT_PIPE;
    options.nonblocking = true;
    if (const int r = reproc_start(client->process_.get(), argv.data(), options); r < 0) {
        return module_error("impossible de lancer « " + client->spec_.arguments.front() +
                            " » : " + reproc_message(r));
    }
    client->reader_ =
        std::jthread([raw = client.get()](const std::stop_token& stop) { raw->read_loop(stop); });
    return client;
}

RpcClient::~RpcClient() {
    if (reader_.joinable()) {
        reader_.request_stop();
        reader_.join();
    }
    if (!process_) {
        return;
    }
    {
        // Fin de l'entrée : un module bien écrit s'arrête de lui-même.
        const std::scoped_lock lock(write_mutex_);
        reproc_close(process_.get(), REPROC_STREAM_IN);
    }
    // Puis arrêt en douceur, puis forcé (MCP : stdin fermé, SIGTERM, SIGKILL).
    const reproc_stop_actions stop{
        .first = {.action = REPROC_STOP_WAIT, .timeout = 500},
        .second = {.action = REPROC_STOP_TERMINATE, .timeout = 1000},
        .third = {.action = REPROC_STOP_KILL, .timeout = 1000},
    };
    reproc_stop(process_.get(), stop);
}

bool RpcClient::alive() const {
    const std::scoped_lock lock(mutex_);
    return alive_;
}

std::string RpcClient::failure() const {
    const std::scoped_lock lock(mutex_);
    return failure_;
}

void RpcClient::fail(std::string reason) {
    {
        const std::scoped_lock lock(mutex_);
        if (!alive_) {
            return;
        }
        alive_ = false;
        failure_ = std::move(reason);
    }
    spec_.log->warn("{}", failure());
    changed_.notify_all();
}

core::Result<void> RpcClient::write_line(const std::string& line,
                                         std::chrono::steady_clock::time_point deadline) {
    const std::scoped_lock lock(write_mutex_);
    const auto* data = reinterpret_cast<const std::uint8_t*>(line.data()); // NOLINT
    std::size_t written = 0;
    while (written < line.size()) {
        const auto rest = std::span(data, line.size()).subspan(written);
        const int r = reproc_write(process_.get(), rest.data(), rest.size());
        if (r >= 0) {
            written += static_cast<std::size_t>(r);
            continue;
        }
        if (r != REPROC_EWOULDBLOCK) {
            return module_error("écriture vers le module impossible : " + reproc_message(r));
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return module_error("le module ne lit plus ses messages");
        }
        std::this_thread::sleep_for(2ms);
    }
    return {};
}

core::Result<nlohmann::json> RpcClient::request(std::string_view method, nlohmann::json params,
                                                std::chrono::milliseconds timeout,
                                                const std::stop_token& stop) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::int64_t id = 0;
    {
        const std::scoped_lock lock(mutex_);
        if (!alive_) {
            return module_error(failure_);
        }
        id = ++next_id_;
        pending_.emplace(id, Pending{});
    }
    const json message{
        {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
    if (auto written = write_line(dump(message), deadline); !written) {
        const std::scoped_lock lock(mutex_);
        pending_.erase(id);
        return std::unexpected(std::move(written.error()));
    }

    std::unique_lock lock(mutex_);
    const auto slot = pending_.find(id);
    const bool answered = changed_.wait_until(
        lock, stop, deadline, [this, slot] { return !alive_ || slot->second.done; });
    Pending pending = std::move(slot->second);
    pending_.erase(slot);

    if (!answered || !pending.done) {
        if (!alive_) {
            return module_error(failure_);
        }
        lock.unlock();
        // Le module peut abandonner le travail en cours (MCP).
        const bool cancelled = stop.stop_requested();
        if (auto sent =
                notify("notifications/cancelled",
                       {{"requestId", id},
                        {"reason", cancelled ? "annulé par l'utilisateur" : "délai dépassé"}});
            !sent) {
            spec_.log->debug("annulation non transmise : {}", sent.error().message);
        }
        if (cancelled) {
            return core::make_error(core::ErrorCode::Cancelled, {});
        }
        const std::string delay = timeout.count() % 1000 == 0
                                      ? std::to_string(timeout.count() / 1000) + " s"
                                      : std::to_string(timeout.count()) + " ms";
        return module_error("pas de réponse du module en " + delay + " (" + std::string(method) +
                            ")");
    }
    lock.unlock();

    if (const auto error = pending.response.find("error"); error != pending.response.end()) {
        return module_error(describe_rpc_error(*error));
    }
    const auto result = pending.response.find("result");
    if (result == pending.response.end()) {
        return module_error("réponse sans « result » ni « error »");
    }
    return *result;
}

core::Result<void> RpcClient::notify(std::string_view method, nlohmann::json params) {
    if (!alive()) {
        return module_error(failure());
    }
    const json message{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}};
    return write_line(dump(message), std::chrono::steady_clock::now() + 1s);
}

void RpcClient::read_loop(const std::stop_token& stop) {
    bool out_open = true;
    bool err_open = true;
    std::array<std::uint8_t, 8192> buffer{};

    const auto read = [&](REPROC_STREAM stream, std::string& sink, bool& open) {
        const int r = reproc_read(process_.get(), stream, buffer.data(), buffer.size());
        if (r < 0) {
            if (r != REPROC_EWOULDBLOCK) {
                open = false; // REPROC_EPIPE : le flux est fermé.
            }
            return;
        }
        const auto received = std::span(buffer).first(static_cast<std::size_t>(r));
        sink.append(received.begin(), received.end());
    };

    while (!stop.stop_requested() && out_open) {
        reproc_event_source source{
            .process = process_.get(),
            .interests =
                static_cast<int>(static_cast<unsigned>(REPROC_EVENT_OUT) |
                                 (err_open ? static_cast<unsigned>(REPROC_EVENT_ERR) : 0U)),
            .events = 0,
        };
        const int r = reproc_poll(&source, 1, 100);
        if (r < 0) {
            break; // Plus aucun flux à surveiller.
        }
        if (has_event(source.events, REPROC_EVENT_ERR)) {
            read(REPROC_STREAM_ERR, err_buffer_, err_open);
            std::size_t end = 0;
            while ((end = err_buffer_.find('\n')) != std::string::npos) {
                spec_.log->info("{}", sanitize(std::string_view(err_buffer_).substr(0, end)));
                err_buffer_.erase(0, end + 1);
            }
            if (err_buffer_.size() > kMaxLogLine) {
                spec_.log->info("{}", sanitize(err_buffer_));
                err_buffer_.clear();
            }
        }
        if (has_event(source.events, REPROC_EVENT_OUT)) {
            read(REPROC_STREAM_OUT, out_buffer_, out_open);
            std::size_t end = 0;
            while ((end = out_buffer_.find('\n')) != std::string::npos) {
                handle_line(std::string_view(out_buffer_).substr(0, end));
                out_buffer_.erase(0, end + 1);
            }
            if (out_buffer_.size() > kMaxMessageBytes) {
                fail("message trop long reçu du module : arrêté");
                return;
            }
        }
    }
    if (!stop.stop_requested()) {
        fail("le module s'est arrêté");
    }
}

void RpcClient::handle_line(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return;
    }
    const json message = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (message.is_discarded() || !message.is_object()) {
        spec_.log->warn("message illisible ignoré : {}", sanitize(line.substr(0, 200)));
        return;
    }
    handle_message(message);
}

void RpcClient::handle_message(const nlohmann::json& message) {
    const auto id = message.find("id");
    const auto method = message.find("method");

    if (method != message.end()) {
        if (!method->is_string()) {
            return;
        }
        if (id != message.end()) {
            // Requête du module vers l'hôte : pas encore prise en charge
            // (appels entre modules, échantillonnage MCP…).
            const json reply{{"jsonrpc", "2.0"},
                             {"id", *id},
                             {"error",
                              {{"code", kMethodNotFound},
                               {"message", "méthode non prise en charge par Avenoir"}}}};
            if (auto sent = write_line(dump(reply), std::chrono::steady_clock::now() + 1s); !sent) {
                spec_.log->debug("refus non transmis : {}", sent.error().message);
            }
            return;
        }
        const auto name = method->get<std::string>();
        if (name == "notifications/message") {
            const auto params = message.value("params", json::object());
            const json data = params.is_object() ? params.value("data", json()) : json();
            spec_.log->info("{}",
                            sanitize(data.is_string() ? data.get<std::string>() : data.dump()));
        } else {
            spec_.log->debug("notification ignorée : {}", sanitize(name));
        }
        return;
    }

    if (id == message.end() || !id->is_number_integer()) {
        spec_.log->warn("message sans identifiant valide ignoré");
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        const auto it = pending_.find(id->get<std::int64_t>());
        if (it == pending_.end() || it->second.done) {
            return; // Réponse tardive ou inventée : ignorée.
        }
        it->second.done = true;
        it->second.response = message;
    }
    changed_.notify_all();
}

} // namespace avenoir::modules
