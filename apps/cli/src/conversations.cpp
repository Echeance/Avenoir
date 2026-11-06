#include "conversations.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

namespace avenoir::cli {

namespace {

constexpr std::string_view kDefaultTitle = "Nouvelle conversation";

/// « « firefox » » pour un seul argument texte, le JSON compact sinon.
std::string summarize_arguments(const nlohmann::json& arguments) {
    if (arguments.is_object() && arguments.size() == 1 && arguments.begin()->is_string()) {
        return "« " + arguments.begin()->get<std::string>() + " »";
    }
    return arguments.dump();
}

/// Numéro de conversation (18 chiffres au plus : pas de débordement possible).
std::optional<std::int64_t> parse_id(std::string_view text) {
    if (text.empty() || text.size() > 18 ||
        !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::int64_t id = 0;
    for (const char c : text) {
        id = (id * 10) + (c - '0');
    }
    return id;
}

void warn_storage(const core::Error& error) {
    spdlog::warn("conversation non enregistrée : {}", core::describe(error));
}

/// Aplatit Result<optional<T>> : rien en cas d'erreur (signalée) ou d'absence.
std::optional<storage::ConversationInfo>
flatten(core::Result<std::optional<storage::ConversationInfo>> result) {
    if (!result) {
        warn_storage(result.error());
        return std::nullopt;
    }
    return std::move(*result);
}

} // namespace

void print_messages(std::span<const core::Message> messages, std::optional<std::size_t> limit) {
    if (limit && messages.size() > *limit) {
        std::println("… {} message(s) plus ancien(s) — /historique pour tout afficher",
                     messages.size() - *limit);
        messages = messages.last(*limit);
    }
    for (const auto& message : messages) {
        std::visit(
            [](const auto& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, core::UserMessage>) {
                    std::println("\n> {}", m.content);
                } else if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                    for (const auto& call : m.tool_calls) {
                        std::println("⚙ {} {}", call.name, summarize_arguments(call.arguments));
                    }
                    if (!m.text.empty()) {
                        std::println("{}", m.text);
                    }
                }
                // Les résultats bruts des outils ne sont pas réaffichés.
            },
            message);
    }
}

Conversations::Conversations(std::optional<storage::Store> store, core::Agent& agent,
                             Prompt& prompt)
    : store_(std::move(store)), agent_(&agent), prompt_(&prompt) {}

void Conversations::resume() {
    agent_->set_history_observer(core::HistoryObserver{
        .appended = [this](const core::Message& message) { record(message); },
        .removed_last = [this] { record_removal(); },
    });
    if (!store_) {
        return;
    }
    if (auto recent = flatten(store_->most_recent())) {
        switch_to(*recent);
    } else {
        start({}, /*announce=*/false);
    }
}

void Conversations::switch_to(const storage::ConversationInfo& conversation) {
    if (!store_) {
        return;
    }
    auto messages = store_->messages(conversation.id);
    if (!messages) {
        warn_storage(messages.error());
        return;
    }
    current_ = conversation.id;
    title_ = conversation.title;
    auto_title_ = conversation.message_count == 0 && conversation.title == kDefaultTitle;
    print_messages(*messages, kResumeMessages);
    agent_->load_history(std::move(*messages));
}

void Conversations::create(std::string_view title) {
    start(title, /*announce=*/true);
}

void Conversations::start(std::string_view title, bool announce) {
    agent_->reset();
    if (!store_) {
        std::println("Nouvelle conversation (non conservée : base indisponible).");
        return;
    }
    const std::string name = title.empty() ? std::string(kDefaultTitle) : std::string(title);
    auto created = store_->create_conversation(name);
    if (!created) {
        warn_storage(created.error());
        return;
    }
    current_ = created->id;
    title_ = created->title;
    auto_title_ = title.empty();
    if (announce) {
        std::println("Nouvelle conversation : {}", describe_current());
    }
}

void Conversations::list() {
    if (!store_) {
        std::println("Conversations non conservées : base indisponible.");
        return;
    }
    auto conversations = store_->list_conversations();
    if (!conversations) {
        warn_storage(conversations.error());
        return;
    }
    for (const auto& c : *conversations) {
        std::println("{} {:>4}  {}  ({}, {} message(s))", c.id == current_ ? "▶" : " ", c.id,
                     c.title, c.updated_at.substr(0, 16), c.message_count);
    }
}

std::optional<storage::ConversationInfo> Conversations::resolve(std::string_view target) {
    if (!store_) {
        return std::nullopt;
    }
    if (const auto id = parse_id(target)) {
        if (auto found = flatten(store_->conversation(*id))) {
            return found;
        }
    } else {
        auto conversations = store_->list_conversations();
        if (conversations) {
            for (const auto& c : *conversations) {
                if (c.title == target) {
                    return c;
                }
            }
        }
    }
    std::println("Conversation introuvable : {} (voir /conversations)", target);
    return std::nullopt;
}

void Conversations::open(std::string_view target) {
    if (!store_) {
        std::println("Conversations non conservées : base indisponible.");
        return;
    }
    if (target.empty()) {
        std::println("Usage : /ouvrir <n° ou titre>");
        return;
    }
    if (const auto conversation = resolve(target)) {
        std::println("Conversation : {} — {}", conversation->id, conversation->title);
        switch_to(*conversation);
    }
}

void Conversations::rename(std::string_view title) {
    if (!store_) {
        std::println("Conversations non conservées : base indisponible.");
        return;
    }
    if (title.empty()) {
        std::println("Usage : /renommer <titre>");
        return;
    }
    if (auto renamed = store_->rename(current_, title); !renamed) {
        warn_storage(renamed.error());
        return;
    }
    title_ = std::string(title);
    auto_title_ = false;
    std::println("Conversation renommée : {}", describe_current());
}

void Conversations::remove(std::string_view target) {
    if (!store_) {
        std::println("Conversations non conservées : base indisponible.");
        return;
    }
    storage::Store& store = *store_;
    const std::optional<storage::ConversationInfo> conversation =
        target.empty() ? flatten(store.conversation(current_)) : resolve(target);
    if (!conversation) {
        return;
    }
    const std::string question = std::format("Supprimer « {} » et ses {} message(s) ? [o/N] ",
                                             conversation->title, conversation->message_count);
    if (!prompt_->ask_yes_no(question)) {
        std::println("Rien n'a été supprimé.");
        return;
    }
    if (auto removed = store.remove(conversation->id); !removed) {
        warn_storage(removed.error());
        return;
    }
    std::println("Conversation supprimée.");
    if (conversation->id != current_) {
        return;
    }
    // La conversation en cours a disparu : reprise de la plus récente.
    agent_->reset();
    if (auto recent = flatten(store.most_recent())) {
        std::println("Conversation : {}", recent->title);
        switch_to(*recent);
    } else {
        create({});
    }
}

void Conversations::show_history() {
    print_messages(agent_->history(), std::nullopt);
}

std::string Conversations::describe_current() const {
    if (!store_ || current_ == 0) {
        return "conversation non conservée";
    }
    return std::format("« {} » (n° {})", title_, current_);
}

void Conversations::record(const core::Message& message) {
    if (!store_ || current_ == 0) {
        return;
    }
    if (auto appended = store_->append_message(current_, message); !appended) {
        warn_storage(appended.error());
        return;
    }
    // Titre automatique : les premiers mots de la première question.
    if (const auto* user = std::get_if<core::UserMessage>(&message);
        auto_title_ && user != nullptr) {
        const std::string title = storage::title_from(user->content);
        if (const auto renamed = store_->rename(current_, title); renamed) {
            title_ = title;
        }
        auto_title_ = false;
    }
}

void Conversations::record_removal() {
    if (!store_ || current_ == 0) {
        return;
    }
    if (auto removed = store_->remove_last_message(current_); !removed) {
        warn_storage(removed.error());
    }
}

} // namespace avenoir::cli
