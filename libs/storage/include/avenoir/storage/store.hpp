#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/core/message.hpp"

namespace avenoir::storage {

struct ConversationInfo {
    std::int64_t id = 0;
    std::string title;
    std::string created_at; ///< Heure locale, « AAAA-MM-JJ HH:MM:SS ».
    std::string updated_at; ///< Heure locale de la dernière activité.
    std::size_t message_count = 0;
};

/// Base des conversations (SQLite). Chaque message est écrit dès qu'il est
/// ajouté. Toutes les requêtes sont paramétrées : un titre ou un message
/// n'est jamais inséré dans le texte SQL.
///
/// Utilisable depuis plusieurs threads, à condition de ne pas l'appeler en
/// parallèle (la CLI l'appelle depuis le thread principal ou depuis celui de
/// la requête en cours, jamais les deux à la fois).
class Store {
  public:
    /// Ouvre la base (la crée si besoin) et applique les migrations.
    [[nodiscard]] static core::Result<Store> open(const std::filesystem::path& path);
    /// Base en mémoire, pour les tests.
    [[nodiscard]] static core::Result<Store> open_in_memory();

    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    ~Store();

    [[nodiscard]] core::Result<ConversationInfo> create_conversation(std::string_view title);
    /// Conversations, de la plus récemment utilisée à la plus ancienne.
    [[nodiscard]] core::Result<std::vector<ConversationInfo>> list_conversations() const;
    [[nodiscard]] core::Result<std::optional<ConversationInfo>> conversation(std::int64_t id) const;
    [[nodiscard]] core::Result<std::optional<ConversationInfo>> most_recent() const;
    [[nodiscard]] core::Result<void> rename(std::int64_t id, std::string_view title);
    /// Supprime la conversation et tous ses messages.
    [[nodiscard]] core::Result<void> remove(std::int64_t id);

    [[nodiscard]] core::Result<void> append_message(std::int64_t conversation,
                                                    const core::Message& message);
    [[nodiscard]] core::Result<void> remove_last_message(std::int64_t conversation);
    [[nodiscard]] core::Result<std::vector<core::Message>>
    messages(std::int64_t conversation) const;

  private:
    struct Impl;
    explicit Store(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// Dossier de données d'Avenoir (`$XDG_DATA_HOME/avenoir` ou
/// `~/.local/share/avenoir`, `%LOCALAPPDATA%\avenoir`), créé si besoin.
[[nodiscard]] std::optional<std::filesystem::path> data_directory();

/// Titre automatique : les premiers mots de `text` (60 octets au plus, sans
/// couper un caractère UTF-8), « Nouvelle conversation » si vide.
[[nodiscard]] std::string title_from(std::string_view text);

namespace detail {

/// Sérialisation d'un message (exposée pour les tests).
[[nodiscard]] nlohmann::json message_to_json(const core::Message& message);
[[nodiscard]] core::Result<core::Message> message_from_json(const nlohmann::json& object);

} // namespace detail

} // namespace avenoir::storage
