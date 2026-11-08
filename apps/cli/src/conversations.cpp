#include "conversations.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

namespace {

constexpr std::string_view kDefaultTitle = "Nouvelle conversation";

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

Conversations::Conversations(std::optional<storage::Store> store, core::Agent& agent,
                             Prompt& prompt)
    : store_(std::move(store)), agent_(&agent), prompt_(&prompt) {}

void Conversations::resume() {
    if (!store_) {
        return;
    }
    if (auto recent = flatten(store_->most_recent())) {
        switch_to(*recent, /*show=*/false);
    } else {
        start({}, /*announce=*/false);
    }
}

void Conversations::show_recent() {
    print_messages(agent_->history(), kResumeMessages);
}

void Conversations::switch_to(const storage::ConversationInfo& conversation, bool show) {
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
    if (show) {
        print_messages(*messages, kResumeMessages);
    }
    agent_->load_history(std::move(*messages));
}

void Conversations::create(std::string_view title) {
    start(title, /*announce=*/true);
}

void Conversations::start(std::string_view title, bool announce) {
    agent_->reset();
    if (!store_) {
        display().info("Nouvelle conversation (non conservée : base indisponible).");
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
        display().success(std::format("Nouvelle conversation : {}", describe_current()));
    }
}

void Conversations::list() {
    if (!store_) {
        display().info("Conversations non conservées : base indisponible.");
        return;
    }
    auto conversations = store_->list_conversations();
    if (!conversations) {
        warn_storage(conversations.error());
        return;
    }
    using terminal::Color;
    for (const auto& c : *conversations) {
        const bool current = c.id == current_;
        display().plain(
            std::format("{} {}  {}  {}",
                        current ? terminal::paint("●", {.color = Color::Green, .bold = true}) : " ",
                        terminal::paint(std::format("{:>4}", c.id), {.color = Color::Gray}),
                        terminal::paint(terminal::ellipsize(c.title, 50), {.bold = current}),
                        terminal::paint(std::format("{} · {} message(s)",
                                                    c.updated_at.substr(0, 16), c.message_count),
                                        {.color = Color::Gray})));
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
    display().warning(std::format("Conversation introuvable : {} (voir /conversations)", target));
    return std::nullopt;
}

void Conversations::open(std::string_view target) {
    if (!store_) {
        display().info("Conversations non conservées : base indisponible.");
        return;
    }
    if (target.empty()) {
        display().info("Usage : /ouvrir <n° ou titre>");
        return;
    }
    if (const auto conversation = resolve(target)) {
        display().success(
            std::format("Conversation : « {} » (n° {})", conversation->title, conversation->id));
        switch_to(*conversation, /*show=*/true);
    }
}

void Conversations::rename(std::string_view title) {
    if (!store_) {
        display().info("Conversations non conservées : base indisponible.");
        return;
    }
    if (title.empty()) {
        display().info("Usage : /renommer <titre>");
        return;
    }
    if (auto renamed = store_->rename(current_, title); !renamed) {
        warn_storage(renamed.error());
        return;
    }
    title_ = std::string(title);
    auto_title_ = false;
    display().success(std::format("Conversation renommée : {}", describe_current()));
}

void Conversations::remove(std::string_view target) {
    if (!store_) {
        display().info("Conversations non conservées : base indisponible.");
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
        display().info("Rien n'a été supprimé.");
        return;
    }
    if (auto removed = store.remove(conversation->id); !removed) {
        warn_storage(removed.error());
        return;
    }
    display().success("Conversation supprimée.");
    if (conversation->id != current_) {
        return;
    }
    // La conversation en cours a disparu : reprise de la plus récente.
    agent_->reset();
    if (auto recent = flatten(store.most_recent())) {
        display().info(std::format("Conversation : « {} » (n° {})", recent->title, recent->id));
        switch_to(*recent, /*show=*/true);
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
