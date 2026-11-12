#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "avenoir/core/agent.hpp"
#include "avenoir/storage/store.hpp"

#include "ui.hpp"

namespace avenoir::daemon {

/// Chemin en UTF-8, sous Windows aussi (`string()` y rendrait la page de code ANSI).
[[nodiscard]] inline std::string utf8_path(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// Nombre de messages réaffichés à la reprise d'une conversation.
inline constexpr std::size_t kResumeMessages = 50;

/// Conversation en cours et commandes de conversation de la CLI. Sans base
/// (ouverture impossible), tout fonctionne mais rien n'est conservé.
class Conversations {
  public:
    Conversations(std::optional<storage::Store> store, core::Agent& agent);

    /// Reprend la conversation la plus récente (ou en crée une) et la charge
    /// dans l'agent, sans l'afficher (voir `show_recent`).
    void resume();
    /// Réaffiche les derniers messages de la conversation en cours.
    void show_recent();

    /// Enregistre un message ajouté à l'historique (appelé par l'observateur
    /// de l'agent, depuis le thread de la requête).
    void record(const core::Message& message);
    /// Enregistre le retrait du dernier message.
    void record_removal();

    void create(std::string_view title); ///< /nouvelle [titre]
    /// Issue d'un /reset réussi.
    struct Cleared {
        std::optional<std::filesystem::path> transcript; ///< Transcription gardée.
    };
    /// /reset : efface les messages de la conversation en cours, qui garde
    /// son numéro et son titre. S'il y en a, demande confirmation puis propose
    /// d'en garder une transcription Markdown (si elle échoue, rien n'est
    /// effacé). Vide si rien n'a été effacé ; sinon l'appelant vide l'écran.
    [[nodiscard]] std::optional<Cleared> clear();
    void list();                          ///< /conversations
    void open(std::string_view target);   ///< /ouvrir <n° ou titre>
    void rename(std::string_view title);  ///< /renommer <titre>
    void remove(std::string_view target); ///< /supprimer [n° ou titre]
    void show_history();                  ///< /historique
    void transcribe() const;              ///< /transcription

    /// Base d'Avenoir, ou `nullptr` si elle n'a pas pu être ouverte.
    [[nodiscard]] storage::Store* store() noexcept { return store_ ? &*store_ : nullptr; }

    /// « Titre » (n° 3), ou « conversation non conservée ».
    [[nodiscard]] std::string describe_current() const;

  private:
    void switch_to(const storage::ConversationInfo& conversation, bool show);
    void start(std::string_view title, bool announce);
    /// Écrit la transcription dans `transcriptions/` du dossier de données.
    [[nodiscard]] static std::expected<std::filesystem::path, std::string>
    write_transcript(std::int64_t id, std::string_view title,
                     std::span<const core::Message> messages);
    [[nodiscard]] std::optional<storage::ConversationInfo> resolve(std::string_view target);

    std::optional<storage::Store> store_;
    core::Agent* agent_;
    std::int64_t current_ = 0;
    std::string title_;
    bool auto_title_ = false; ///< Titre à déduire de la première question.
};

} // namespace avenoir::daemon
