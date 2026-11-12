#include "ui.hpp"

#include <cstddef>
#include <format>
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
#include "avenoir/terminal/style.hpp"

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
    void banner(const Welcome& /*welcome*/) override {}
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

void RemoteUi::banner(const Welcome& welcome) {
    json groups = json::array();
    for (const auto& [name, items] : welcome.groups) {
        groups.push_back({{"name", name}, {"items", items}});
    }
    json sections = json::array();
    for (const auto& [title, text] : welcome.sections) {
        sections.push_back({{"title", title}, {"text", text}});
    }
    send(json{{"op", "banner"},
              {"llm", welcome.llm},
              {"tools", welcome.tools},
              {"conversation", welcome.conversation},
              {"groups", std::move(groups)},
              {"sections", std::move(sections)}});
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
    const std::scoped_lock lock(question_mutex_);
    auto answer = peer_->call("ask", {{"question", question}}, never());
    if (!answer || !answer->is_object() || !answer->contains("answer") ||
        !(*answer)["answer"].is_string()) {
        return std::nullopt;
    }
    return (*answer)["answer"].get<std::string>();
}

bool RemoteUi::ask_yes_no(std::string_view question) {
    const std::scoped_lock lock(question_mutex_);
    const auto answer = peer_->call("ask_yes_no", {{"question", question}}, never());
    return answer && answer->is_boolean() && answer->get<bool>();
}

bool RemoteUi::confirm(const core::ToolSpec& spec, const core::ToolCall& call) {
    const json params{{"spec",
                       {{"name", spec.name},
                        {"description", spec.description},
                        {"risk", core::to_string(spec.risk)}}},
                      {"call", call_to_json(call)}};
    const std::scoped_lock lock(question_mutex_);
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

namespace {

/// Interface par défaut (clients connectés, ou journal), quel que soit le fil.
Ui& background_display() {
    const std::scoped_lock lock(g_default_mutex);
    if (!g_default) {
        g_default = std::make_shared<LogUi>();
    }
    return *g_default;
}

} // namespace

void ProjectUi::note(std::string_view text) const {
    background_display().notify(std::format("[projet {}] {}", project_, text));
}

void ProjectUi::assistant(std::string_view markdown) {
    {
        const std::scoped_lock lock(mutex_);
        last_reply_ = std::string(markdown);
    }
    note(std::format("terminé : {} (/projet voir {})", terminal::ellipsize(markdown, 160),
                     project_));
}

void ProjectUi::tool_calls(std::span<const core::ToolCall> calls) {
    for (const auto& call : calls) {
        note(std::format("{} {} {}", kToolGlyph, call.name,
                         terminal::ellipsize(call.arguments.dump(), 80)));
    }
}

void ProjectUi::tool_result(const core::ToolResultMessage& result) {
    if (result.is_error) {
        note("✗ " + terminal::ellipsize(result.content, 120));
    }
}

std::optional<std::string> ProjectUi::ask(std::string_view question) {
    note(std::format("question sans réponse (arrière-plan) : {}",
                     terminal::ellipsize(question, 120)));
    return std::nullopt;
}

bool ProjectUi::ask_yes_no(std::string_view question) {
    note(std::format("refusé (arrière-plan, personne pour confirmer) : {}",
                     terminal::ellipsize(question, 120)));
    return false;
}

bool ProjectUi::confirm(const core::ToolSpec& spec, const core::ToolCall& /*call*/) {
    note(std::format("action « {} » refusée : une action à confirmer ne se fait pas en "
                     "arrière-plan ; refaites la demande au premier plan.",
                     spec.name));
    return false;
}

std::string ProjectUi::last_reply() const {
    const std::scoped_lock lock(mutex_);
    return last_reply_;
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
