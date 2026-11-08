#pragma once

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::cli {

/// Symboles de l'invite (requête), des actions (outil) et des avertissements.
/// Choisis pour exister dans les polices de console courantes (Consolas,
/// Cascadia Mono…) et n'occuper qu'une colonne : « ❯ » manque à ces polices,
/// « ⚙ » et « ⚠ » y sont dessinés en emoji de double largeur, qui chevauchent
/// le texte suivant. Les mêmes sur tous les systèmes.
inline constexpr std::string_view kPromptGlyph = "›";
inline constexpr std::string_view kToolGlyph = "»";
inline constexpr std::string_view kWarningGlyph = "▲";

/// Affichage de la CLI : un style par type d'élément (requête ›, réponse
/// ◆ Avenoir, action », confirmation, erreur…) et une toupie pendant que le
/// LLM réfléchit. Toutes les écritures passent par ici, sous un verrou : la
/// toupie (thread principal) et les actions (thread de la requête) ne
/// s'écrivent jamais en même temps.
///
/// Mode « barre fixe » (terminal en couleurs, hors Windows) : les 3 dernières
/// lignes du terminal forment la barre de saisie (bordure, invite, bordure) ;
/// tout le reste défile au-dessus, dans une zone de défilement (DECSTBM).
/// Sinon, mode « en ligne » : tout défile, la barre est tracée à chaque saisie.
class Display {
  public:
    /// Passe en mode barre fixe si le terminal interprète les séquences de
    /// contrôle (`supported`) et fait au moins 5 lignes.
    void enable_pinned_bar(bool supported);
    /// Rend au terminal son état normal (zone de défilement, curseur en bas).
    void shutdown();

    /// Avant une saisie : barre de saisie vide, curseur sur la ligne d'invite.
    void prepare_input();
    /// Après une saisie : la ligne saisie (`prompt` + `text`) passe dans
    /// l'historique, sans cadre, et la barre est vidée.
    void finish_input(std::string_view prompt, bool entered, std::string_view text);
    /// Pendant la saisie : agrandit la barre si le texte demande plus de
    /// lignes (appelé à chaque modification de la saisie).
    void fit_input(std::string_view prompt, std::string_view line);
    /// Pendant la saisie : recale la barre après un redimensionnement du
    /// terminal, réécrit l'invite et place le curseur sur la ligne où replxx
    /// l'attend (`before_cursor` : largeur du texte avant le curseur) ;
    /// replxx redessine ensuite la saisie.
    void redraw_for_resize(std::string_view prompt, std::size_t before_cursor);

    /// Bordure de la barre de saisie (ligne grise sur toute la largeur).
    [[nodiscard]] static std::string bar_rule();

    /// Vide l'écran et l'historique du terminal (/reset). Sans effet hors
    /// d'un terminal (sortie redirigée).
    void clear();
    void banner(std::string_view llm, std::size_t tools, std::string_view conversation);
    /// À la sortie : écran et historique du terminal vidés (dans un
    /// terminal), puis un cadre d'au revoir avec le pingouin.
    void farewell(std::string_view conversation);

    /// Requête de l'utilisateur (réaffichage d'une conversation).
    void user(std::string_view text);
    /// Réponse d'Avenoir, rendue en Markdown.
    void assistant(std::string_view markdown);
    /// Retient les appels d'outils d'une réponse ; affichés avec leur résultat.
    void tool_calls(std::span<const core::ToolCall> calls);
    /// « » outil arguments  ✓ » (ou ✗ et l'erreur).
    void tool_result(const core::ToolResultMessage& result);

    void info(std::string_view text);    ///< Message système, discret.
    void success(std::string_view text); ///< ✓ vert.
    void warning(std::string_view text); ///< ▲ jaune.
    void error(std::string_view text);   ///< ✗ rouge, sur stderr.
    void plain(std::string_view text);   ///< Texte déjà mis en forme.

    /// Cadre de confirmation d'une action (la question est posée ensuite).
    void confirmation(const core::ToolSpec& spec, const core::ToolCall& call);

    /// Toupie « Réflexion… » : `tick` est appelé régulièrement par le thread
    /// principal ; `suspend`/`resume` l'effacent pendant une saisie.
    void start_thinking();
    void stop_thinking();
    void tick();
    void suspend_thinking();
    void resume_thinking();

    /// Invite de saisie : « › » coloré, ou « > » sans couleurs.
    [[nodiscard]] static std::string prompt_marker();

  private:
    void write(std::string_view text, std::FILE* stream = stdout);
    void clear_spinner();
    void draw_bar(bool marker) const;
    [[nodiscard]] bool refresh_geometry();
    void replay();
    [[nodiscard]] std::size_t region_bottom() const;
    [[nodiscard]] std::size_t prompt_row() const;
    [[nodiscard]] std::size_t needed_input_rows() const;
    [[nodiscard]] static std::size_t width();

    std::mutex mutex_;
    bool thinking_ = false;
    bool shown_ = false;
    bool suspended_ = false;
    std::size_t frame_ = 0;
    bool pinned_ = false;
    bool reading_ = false;       ///< Une saisie est en cours dans la barre.
    std::size_t rows_ = 0;       ///< Hauteur du terminal (barre fixe).
    std::size_t columns_ = 0;    ///< Largeur du terminal (barre fixe).
    std::size_t input_rows_ = 1; ///< Lignes de saisie de la barre.
    std::size_t last_prompt_width_ = 0;
    std::size_t last_line_width_ = 0;
    std::deque<std::string> transcript_; ///< Fin de l'historique affiché (barre fixe).
    std::chrono::steady_clock::time_point since_;
    std::chrono::steady_clock::time_point last_frame_;
    std::vector<core::ToolCall> pending_;
};

/// Affichage partagé par toute la CLI.
[[nodiscard]] Display& display();

/// « « firefox » » pour un seul argument texte, le JSON compact sinon.
[[nodiscard]] std::string summarize_arguments(const nlohmann::json& arguments);

/// Réaffiche des messages comme dans la conversation, les `limit` derniers
/// seulement si demandé (avec une ligne qui signale les messages masqués).
void print_messages(std::span<const core::Message> messages, std::optional<std::size_t> limit);

} // namespace avenoir::cli
