#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <replxx.hxx>

namespace avenoir::cli {

/// Saisie de ligne (replxx) : édition, historique conservé entre deux
/// lancements, complétion des commandes, Ctrl+C distingué de Ctrl+D.
class Prompt {
  public:
    enum class Status : unsigned char {
        Line,        ///< Une ligne a été saisie.
        Interrupted, ///< Ctrl+C : saisie abandonnée.
        EndOfInput,  ///< Ctrl+D ou fin de l'entrée.
    };

    struct Input {
        Status status = Status::EndOfInput;
        std::string text;
    };

    /// `commands` : commandes proposées à la complétion (Tab).
    /// `history_file` : fichier d'historique, ou rien pour ne pas le conserver.
    Prompt(std::vector<std::string> commands, std::optional<std::filesystem::path> history_file);

    /// Lit une ligne. `remember` : l'ajouter à l'historique (faux pour les
    /// réponses aux confirmations).
    [[nodiscard]] Input read(const std::string& prompt, bool remember);

    /// Points d'accroche de la barre de saisie (affichage).
    struct Hooks {
        std::function<void()> before;                                       ///< avant la saisie
        std::function<void(const std::string& prompt, const Input&)> after; ///< après
        std::function<void(const std::string& prompt, const std::string& line)>
            changed; ///< à chaque modification du texte
        /// Terminal redimensionné pendant la saisie : invite et largeur du
        /// texte avant le curseur.
        std::function<void(const std::string& prompt, std::size_t before_cursor)> resized;
        std::function<std::string()> border; ///< bordure du bas pendant la frappe
        /// Pendant la saisie, appelée toutes les 3 s depuis un autre thread :
        /// messages à montrer (ex. modèle déchargé par Ollama), ou aucun.
        std::function<std::vector<std::string>()> poll;
        /// Montre un message de `poll` ; appelée dans le thread de replxx,
        /// jamais en même temps que l'affichage de la saisie.
        std::function<void(const std::string& message)> notify;
    };
    void set_hooks(Hooks hooks);

    /// Pose une question fermée ; seuls « o », « oui », « y », « yes » valent
    /// oui. Ctrl+C vaut non et lève une demande d'interruption.
    [[nodiscard]] bool ask_yes_no(const std::string& question);

  private:
    [[nodiscard]] Input read_line(const std::string& prompt, bool remember);
    /// Fait retaper `text` à replxx, curseur à la position `cursor` (Windows).
    void retype(const std::string& text, int cursor);
    /// Thread de surveillance : simule une touche quand la taille change.
    void watch_size(const std::stop_token& stop);
    /// Thread qui appelle `hooks_.poll` et fait montrer ses messages.
    void watch_events(const std::stop_token& stop);

    replxx::Replxx replxx_;
    std::vector<std::string> commands_;
    std::optional<std::filesystem::path> history_file_;
    Hooks hooks_;
    std::string current_prompt_; ///< Invite de la saisie en cours.
    std::string pending_text_;   ///< Texte à rétablir après un redimensionnement.
    int pending_cursor_ = -1;
    bool restoring_ = false; ///< Rétablissement du texte pas encore traité.
    bool restart_ = false;   ///< Saisie à relancer après un redimensionnement (Windows).
    std::mutex notes_mutex_;
    std::vector<std::string> notes_; ///< Messages de `poll` pas encore montrés.
};

} // namespace avenoir::cli
