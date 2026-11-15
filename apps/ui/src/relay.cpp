#include "relay.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
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
#include <spdlog/spdlog.h>

namespace avenoir::ui {

namespace {

using nlohmann::json;

std::optional<std::uint64_t> id_of(const json& message) {
    if (!message.contains("id") || !message["id"].is_number_unsigned()) {
        return std::nullopt;
    }
    const auto id = message["id"].get<std::uint64_t>();
    return id <= (std::uint64_t{1} << 53U) ? std::optional{id} : std::nullopt;
}

} // namespace

bool is_allowed_call(std::string_view method) {
    // Session et requêtes, puis données de l'application, en lecture seule.
    constexpr std::array<std::string_view, 16> kCalls{
        "session/open",       "input",       "session/describe", "settings/list",
        "journal/list",       "models/list", "permissions/list", "modules/list",
        "conversations/list", "tabs/list",   "tabs/content",     "tabs/event",
        "undo/list",          "logs/list",   "status/list",      "update/list"};
    return std::ranges::find(kCalls, method) != kCalls.end();
}

bool is_question(std::string_view method) {
    constexpr std::array<std::string_view, 4> kQuestions{"ask", "ask_yes_no", "confirm",
                                                         "ask_secret"};
    return std::ranges::find(kQuestions, method) != kQuestions.end();
}

json refusal(std::string_view method) {
    if (method == "ask") {
        return json{{"answer", nullptr}};
    }
    if (method == "ask_secret") {
        return nullptr;
    }
    return false; // confirm, ask_yes_no
}

json checked_answer(std::string_view method, const json& result) {
    if (method == "ask_yes_no" && result.is_boolean()) {
        return result;
    }
    if (method == "confirm" && (result.is_boolean() || result == "always" || result == "never")) {
        return result;
    }
    if (method == "ask" && result.is_object() && result.contains("answer") &&
        (result["answer"].is_string() || result["answer"].is_null())) {
        return json{{"answer", result["answer"]}};
    }
    if (method == "ask_secret" && (result.is_string() || result.is_null())) {
        return result;
    }
    return refusal(method);
}

Relay::Relay(DaemonLink daemon, Post post, Resize resize, OpenApp open_app)
    : daemon_(std::move(daemon)), post_(std::move(post)), resize_(std::move(resize)),
      open_app_(std::move(open_app)) {}

Relay::~Relay() {
    try {
        std::map<std::uint64_t, Running> running;
        {
            const std::scoped_lock lock(mutex_);
            closing_ = true;
            running = std::move(running_);
            running_.clear();
            for (auto& [id, question] : questions_) {
                question.second.set_value(refusal(question.first));
            }
            questions_.clear();
        }
        for (auto& [id, call] : running) {
            call.stop.request_stop();
        }
        running.clear(); // attend la fin des appels
    } catch (...) {
        // Rien ne sort d'un destructeur ; les fils restants sont attendus
        // par les jthread.
        spdlog::error("interface : arrêt du relais incomplet");
    }
}

void Relay::reap() {
    // Appelé verrou pris : appels terminés retirés (leur fil est fini).
    std::erase_if(running_, [](const auto& item) { return item.second.done->load(); });
}

void Relay::start_call(std::uint64_t id, const std::string& method, json params) {
    const std::scoped_lock lock(mutex_);
    reap();
    if (closing_ || running_.contains(id) || running_.size() >= kMaxRunningCalls) {
        post_(json{{"type", "failure"}, {"id", id}, {"message", "trop d'appels en cours"}});
        return;
    }
    Running call;
    call.done = std::make_shared<std::atomic<bool>>(false);
    const std::stop_token stop = call.stop.get_token();
    call.thread = std::jthread([this, id, method, params = std::move(params), stop,
                                done = call.done] mutable {
        auto result = daemon_.call(method, std::move(params), stop);
        if (result && result->is_object() && method == "tabs/list") {
            remember_tabs(*result);
        }
        if (result && result->is_object()) {
            post_(json{{"type", "reply"}, {"id", id}, {"result", std::move(*result)}});
        } else if (result) {
            post_(json{{"type", "reply"}, {"id", id}, {"result", json::object()}});
        } else {
            post_(
                json{{"type", "failure"}, {"id", id}, {"message", core::describe(result.error())}});
        }
        done->store(true);
    });
    running_.emplace(id, std::move(call));
}

void Relay::remember_tabs(json& result) {
    // Le chemin des fichiers d'un onglet web sert à l'hôte, pas à la page.
    if (!result.contains("tabs") || !result["tabs"].is_array()) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    for (auto& tab : result["tabs"]) {
        if (!tab.is_object() || !tab.contains("directory")) {
            continue;
        }
        if (tab["directory"].is_string() && !tab.value("module", std::string{}).empty()) {
            module_roots_.insert_or_assign(
                tab["module"].get<std::string>(),
                std::filesystem::path(tab["directory"].get<std::string>()));
        }
        tab.erase("directory");
    }
}

std::optional<std::filesystem::path> Relay::module_root(std::string_view module) const {
    const std::scoped_lock lock(mutex_);
    const auto found = module_roots_.find(module);
    if (found == module_roots_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void Relay::from_page(std::string_view text) {
    if (text.size() > kMaxPageMessage) {
        spdlog::warn("interface : message de la page trop long, écarté");
        return;
    }
    const json message = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
        spdlog::warn("interface : message de la page mal formé, écarté");
        return;
    }
    const std::string type = message["type"].get<std::string>();
    if (type == "ready") {
        post_(json{{"type", "status"}, {"connected", true}, {"message", "connecté"}});
        return;
    }
    if (type == "open_app") {
        // Une fois toutes les 2 s au plus : un double clic n'ouvre qu'une fenêtre.
        const auto now = std::chrono::steady_clock::now();
        if (open_app_ && now - last_open_ > std::chrono::seconds{2}) {
            last_open_ = now;
            open_app_();
        }
        return;
    }
    if (type == "resize") {
        if (resize_ && message.contains("height") && message["height"].is_number_integer()) {
            const auto height = message["height"].get<std::int64_t>();
            resize_(static_cast<int>(std::clamp<std::int64_t>(height, kMinHeight, kMaxHeight)));
        }
        return;
    }
    const auto id = id_of(message);
    if (!id) {
        spdlog::warn("interface : message « {} » sans identifiant valide, écarté", type);
        return;
    }
    if (type == "call") {
        const std::string method = message.contains("method") && message["method"].is_string()
                                       ? message["method"].get<std::string>()
                                       : std::string{};
        if (!is_allowed_call(method) || !message.contains("params") ||
            !message["params"].is_object()) {
            spdlog::warn("interface : appel « {} » refusé à la page", method);
            post_(json{{"type", "failure"}, {"id", *id}, {"message", "appel refusé"}});
            return;
        }
        start_call(*id, method, message["params"]);
        return;
    }
    if (type == "cancel") {
        const std::scoped_lock lock(mutex_);
        if (const auto found = running_.find(*id); found != running_.end()) {
            found->second.stop.request_stop();
        }
        return;
    }
    if (type == "answer") {
        const std::scoped_lock lock(mutex_);
        if (const auto found = questions_.find(*id); found != questions_.end()) {
            found->second.second.set_value(checked_answer(
                found->second.first, message.contains("result") ? message["result"] : json{}));
            questions_.erase(found);
        }
        return;
    }
    spdlog::warn("interface : message « {} » inconnu, écarté", type);
}

void Relay::ui(const json& params) {
    if (params.is_object()) {
        post_(json{{"type", "ui"}, {"params", params}});
    }
}

json Relay::ask(std::string_view method, const json& params, const std::stop_token& stop) {
    using namespace std::chrono_literals;
    if (!is_question(method)) {
        return refusal(method);
    }
    std::uint64_t id = 0;
    std::future<json> answer;
    {
        const std::scoped_lock lock(mutex_);
        if (closing_) {
            return refusal(method);
        }
        id = next_question_++;
        std::promise<json> promise;
        answer = promise.get_future();
        questions_.emplace(id, std::pair{std::string(method), std::move(promise)});
    }
    post_(json{{"type", "question"},
               {"id", id},
               {"method", method},
               {"params", params.is_object() ? params : json::object()}});
    while (answer.wait_for(100ms) != std::future_status::ready) {
        if (!stop.stop_requested()) {
            continue;
        }
        // Question retirée par le démon (requête annulée) : la carte se ferme.
        const std::scoped_lock lock(mutex_);
        if (questions_.erase(id) > 0) {
            post_(json{{"type", "dismiss"}, {"id", id}});
            return refusal(method);
        }
    }
    return answer.get();
}

void Relay::disconnected() {
    {
        const std::scoped_lock lock(mutex_);
        for (auto& [id, question] : questions_) {
            question.second.set_value(refusal(question.first));
            post_(json{{"type", "dismiss"}, {"id", id}});
        }
        questions_.clear();
    }
    post_(
        json{{"type", "status"}, {"connected", false}, {"message", "connexion à Avenoir perdue"}});
}

} // namespace avenoir::ui
