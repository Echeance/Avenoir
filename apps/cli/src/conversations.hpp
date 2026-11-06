#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "avenoir/core/agent.hpp"
#include "avenoir/storage/store.hpp"

#include "display.hpp"
#include "prompt.hpp"

namespace avenoir::cli {

/// Nombre de messages réaffichés à la reprise d'une conversation.
inline constexpr std::size_t kResumeMessages = 50;

/// Conversation en cours et commandes de conversation de la CLI. Sans base
/// (ouverture impossible), tout fonctionne mais rien n'est conservé.
class Conversations {
  public:
    Conversations(std::optional<storage::Store> store, core::Agent& agent, Prompt& prompt);

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

    void create(std::string_view title);  ///< /nouvelle [titre] (et /reset)
    void list();                          ///< /conversations
    void open(std::string_view target);   ///< /ouvrir <n° ou titre>
    void rename(std::string_view title);  ///< /renommer <titre>
    void remove(std::string_view target); ///< /supprimer [n° ou titre]
    void show_history();                  ///< /historique

    /// « Titre » (n° 3), ou « conversation non conservée ».
    [[nodiscard]] std::string describe_current() const;

  private:
    void switch_to(const storage::ConversationInfo& conversation, bool show);
    void start(std::string_view title, bool announce);
    [[nodiscard]] std::optional<storage::ConversationInfo> resolve(std::string_view target);

    std::optional<storage::Store> store_;
    core::Agent* agent_;
    Prompt* prompt_;
    std::int64_t current_ = 0;
    std::string title_;
    bool auto_title_ = false; ///< Titre à déduire de la première question.
};

} // namespace avenoir::cli
