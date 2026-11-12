#include "ui.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/storage/store.hpp"

namespace avenoir::daemon {

namespace {

using nlohmann::json;

/// Interface de la requête en cours dans ce fil.
thread_local Ui* t_current = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

std::mutex g_default_mutex;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::shared_ptr<Ui> g_default; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// Sans aucun client : le journal du démon.
class LogUi final : public Ui {
  public:
    void info(std::string_view text) override { spdlog::info("{}", text); }
    void notify(std::string_view text) override { spdlog::info("{}", text); }
    void success(std::string_view text) override { spdlog::info("{}", text); }
    void warning(std::string_view text) override { spdlog::warn("{}", text); }
    void error(std::string_view text) override { spdlog::error("{}", text); }
    void plain(std::string_view text) override { spdlog::info("{}", text); }
    void user(std::string_view /*text*/) override {}
    void assistant(std::string_view /*markdown*/) override {}
    void tool_calls(std::span<const core::ToolCall> /*calls*/) override {}
    void tool_result(const core::ToolResultMessage& /*result*/) override {}
    void messages(std::span<const core::Message> /*messages*/,
                  std::optional<std::size_t> /*limit*/) override {}
    void clear() override {}
    void banner(std::string_view /*llm*/, std::size_t /*tools*/,
                std::string_view /*conversation*/) override {}
    void start_thinking() override {}
    void set_activity(std::string_view /*activity*/) override {}
    void stop_thinking() override {}
    void stream_text(std::string_view /*fragment*/) override {}
    void stream_end(bool /*kept*/) override {}
    [[nodiscard]] bool streamed() override { return false; }
    [[nodiscard]] std::optional<std::string> ask(std::string_view /*question*/) override {
        return std::nullopt;
    }
    [[nodiscard]] bool ask_yes_no(std::string_view /*question*/) override { return false; }
    [[nodiscard]] bool confirm(const core::ToolSpec& /*spec*/,
                               const core::ToolCall& /*call*/) override {
        return false;
    }
};

json call_to_json(const core::ToolCall& call) {
    return {{"id", call.id}, {"name", call.name}, {"arguments", call.arguments}};
}

/// Les questions n'ont pas de délai : l'utilisateur prend son temps. Elles
/// échouent si le client se déconnecte.
const std::stop_token& never() {
    static const std::stop_source source;
    static const std::stop_token token = source.get_token();
    return token;
}

} // namespace

void RemoteUi::send(std::string_view op, std::string_view text) {
    send(json{{"op", op}, {"text", text}});
}

void RemoteUi::send(json params) {
    if (auto sent = peer_->notify("ui", std::move(params)); !sent) {
        spdlog::debug("affichage non transmis : {}", sent.error().message);
    }
}

void RemoteUi::tool_calls(std::span<const core::ToolCall> calls) {
    json list = json::array();
    for (const auto& call : calls) {
        list.push_back(call_to_json(call));
    }
    send(json{{"op", "tool_calls"}, {"calls", std::move(list)}});
}

void RemoteUi::tool_result(const core::ToolResultMessage& result) {
    send(json{{"op", "tool_result"},
              {"message", storage::detail::message_to_json(core::Message{result})}});
}

void RemoteUi::messages(std::span<const core::Message> messages, std::optional<std::size_t> limit) {
    json list = json::array();
    for (const auto& message : messages) {
        list.push_back(storage::detail::message_to_json(message));
    }
    json params{{"op", "messages"}, {"messages", std::move(list)}};
    if (limit) {
        params["limit"] = *limit;
    }
    send(std::move(params));
}

void RemoteUi::banner(std::string_view llm, std::size_t tools, std::string_view conversation) {
    send(json{{"op", "banner"}, {"llm", llm}, {"tools", tools}, {"conversation", conversation}});
}

void RemoteUi::start_thinking() {
    {
        const std::scoped_lock lock(mutex_);
        streamed_ = false;
    }
    send("start_thinking", "");
}

void RemoteUi::stream_text(std::string_view fragment) {
    {
        const std::scoped_lock lock(mutex_);
        streamed_ = true;
    }
    send("stream_text", fragment);
}

void RemoteUi::stream_end(bool kept) {
    send(json{{"op", "stream_end"}, {"kept", kept}});
}

bool RemoteUi::streamed() {
    const std::scoped_lock lock(mutex_);
    return streamed_;
}

std::optional<std::string> RemoteUi::ask(std::string_view question) {
    auto answer = peer_->call("ask", {{"question", question}}, never());
    if (!answer || !answer->is_object() || !answer->contains("answer") ||
        !(*answer)["answer"].is_string()) {
        return std::nullopt;
    }
    return (*answer)["answer"].get<std::string>();
}

bool RemoteUi::ask_yes_no(std::string_view question) {
    const auto answer = peer_->call("ask_yes_no", {{"question", question}}, never());
    return answer && answer->is_boolean() && answer->get<bool>();
}

bool RemoteUi::confirm(const core::ToolSpec& spec, const core::ToolCall& call) {
    const json params{{"spec",
                       {{"name", spec.name},
                        {"description", spec.description},
                        {"risk", core::to_string(spec.risk)}}},
                      {"call", call_to_json(call)}};
    const auto answer = peer_->call("confirm", params, never());
    return answer && answer->is_boolean() && answer->get<bool>(); // client parti : refusé
}

void BroadcastUi::broadcast(std::string_view text) {
    const auto clients = clients_ ? clients_() : std::vector<std::shared_ptr<Ui>>{};
    if (clients.empty()) {
        spdlog::info("{}", text);
        return;
    }
    for (const auto& client : clients) {
        client->notify(text);
    }
}

Ui& display() {
    if (t_current != nullptr) {
        return *t_current;
    }
    const std::scoped_lock lock(g_default_mutex);
    if (!g_default) {
        g_default = std::make_shared<LogUi>();
    }
    return *g_default;
}

void set_default_ui(std::shared_ptr<Ui> ui) {
    const std::scoped_lock lock(g_default_mutex);
    g_default = std::move(ui);
}

UiScope::UiScope(Ui& ui) : previous_(std::exchange(t_current, &ui)) {}

UiScope::~UiScope() {
    t_current = previous_;
}

} // namespace avenoir::daemon
