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
#include <utility>
#include <vector>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

#include "scroll.hpp"

namespace avenoir::cli {

/// Symboles de l'invite (requête), des actions (outil) et des avertissements.
/// Choisis pour exister dans les polices de console courantes (Consolas,
/// Cascadia Mono…) et n'occuper qu'une colonne : « ❯ » manque à ces polices,
/// « ⚙ » et « ⚠ » y sont dessinés en emoji de double largeur, qui chevauchent
/// le texte suivant. Les mêmes sur tous les systèmes.
inline constexpr std::string_view kPromptGlyph = "›";
inline constexpr std::string_view kToolGlyph = "»";
inline constexpr std::string_view kWarningGlyph = "▲";

/// Accueil, tel que le démon le décrit.
struct WelcomeInfo {
    std::string llm;
    std::size_t tools = 0;
    std::string conversation;
    std::vector<std::pair<std::string, std::vector<std::string>>> groups; ///< Outils par famille.
    std::vector<std::pair<std::string, std::string>> sections;            ///< Modules, équipe…
};

/// Affichage de la CLI : un style par type d'élément (requête ›, réponse
/// ◆ Avenoir, action », confirmation, erreur…) et une toupie pendant que le
/// LLM réfléchit. Toutes les écritures passent par ici, sous un verrou : la
/// toupie (thread principal) et les actions (thread de la requête) ne
/// s'écrivent jamais en même temps.
///
/// Mode « plein écran » (terminal qui interprète les séquences de contrôle) :
/// l'écran alterné du terminal est divisé en trois. En haut, l'**en-tête**
/// fixe (le loup, le modèle, la conversation) ; en bas, la **barre de saisie**
/// fixe (bordure, invite, ligne d'état) ; entre les deux, la **conversation**,
/// dans une zone de défilement (DECSTBM). Ce qui en sort par le haut est gardé
/// ici (les terminaux ne le gardent pas) : la molette, PgUp/PgDn et Ctrl+↑/↓
/// y remontent. Sinon, mode « en ligne » : tout défile, la barre est tracée à
/// chaque saisie.
class Display {
  public:
    /// Passe en mode plein écran si le terminal interprète les séquences de
    /// contrôle (`supported`) et fait au moins 8 lignes.
    void enable_pinned_bar(bool supported);
    /// Rend au terminal son état normal (écran principal, souris, curseur).
    void shutdown();
    /// Vrai en mode plein écran.
    [[nodiscard]] bool full_screen();

    /// Avant une saisie : barre de saisie vide, curseur sur la ligne d'invite.
    void prepare_input();
    /// Après une saisie : la ligne saisie (`prompt` + `text`) passe dans
    /// l'historique, sans cadre, et la barre est vidée. Une requête ramène la
    /// conversation en bas.
    void finish_input(std::string_view prompt, bool entered, std::string_view text);
    /// Pendant la saisie : agrandit la barre si le texte demande plus de
    /// lignes (appelé à chaque modification de la saisie).
    void fit_input(std::string_view prompt, std::string_view line);
    /// Pendant la saisie : recale l'écran après un redimensionnement du
    /// terminal, réécrit l'invite et place le curseur sur la ligne où replxx
    /// l'attend (`before_cursor` : largeur du texte avant le curseur) ;
    /// replxx redessine ensuite la saisie.
    void redraw_for_resize(std::string_view prompt, std::size_t before_cursor);

    /// Fait défiler la conversation (molette, PgUp/PgDn, Ctrl+↑/↓) ; sans
    /// effet hors du mode plein écran ou quand un panneau est ouvert.
    void scroll(ScrollStep step);

    /// Bordure de la barre de saisie (ligne bleu nuit sur toute la largeur).
    [[nodiscard]] static std::string bar_rule();
    /// Bordure du bas de la barre : le modèle actif et la conversation.
    [[nodiscard]] std::string status_rule() const;

    /// Vide la conversation affichée (/reset) ; l'en-tête reste.
    void clear();
    /// Accueil : en plein écran, l'en-tête (le loup, le modèle, la
    /// conversation) et, dans la conversation, les outils par famille ;
    /// sinon, le titre et le cadre d'accueil, qui défilent.
    void banner(const WelcomeInfo& welcome);
    /// Modèle actif et conversation, après chaque saisie : l'en-tête et la
    /// ligne d'état suivent (/modele, /nouvelle, /ouvrir…).
    void set_status(std::string_view llm, std::string_view conversation);
    /// À la sortie : écran principal du terminal rendu, puis un cadre d'au
    /// revoir avec le loup.
    void farewell(std::string_view conversation);

    /// Panneau (menu des réglages…) dessiné à la place de la conversation,
    /// en plein écran : `lines` sur la hauteur de la zone, sans défilement.
    /// Ce qui s'écrit pendant ce temps rejoint l'historique, montré à la
    /// fermeture.
    void show_panel(const std::vector<std::string>& lines);
    void hide_panel();
    /// Taille de la zone d'un panneau (lignes, colonnes) ; 0 hors plein écran.
    [[nodiscard]] std::pair<std::size_t, std::size_t> panel_size();

    /// Requête de l'utilisateur (réaffichage d'une conversation).
    void user(std::string_view text);
    /// Réponse d'Avenoir, rendue en Markdown.
    void assistant(std::string_view markdown);
    /// Retient les appels d'outils d'une réponse ; affichés avec leur résultat.
    void tool_calls(std::span<const core::ToolCall> calls);
    /// « » outil arguments  ✓  1,2 s » (ou ✗ et l'erreur).
    void tool_result(const core::ToolResultMessage& result);

    void info(std::string_view text);
    /// Message discret qui peut arriver pendant la saisie : il rejoint
    /// l'historique sans déplacer le curseur de la saisie en cours.
    void notify(std::string_view text);
    void success(std::string_view text); ///< ✓ vert.
    void warning(std::string_view text); ///< ▲ jaune.
    void error(std::string_view text);   ///< ✗ rouge, sur stderr.
    void plain(std::string_view text);   ///< Texte déjà mis en forme.

    /// Cadre de confirmation d'une action (la question est posée ensuite).
    void confirmation(const core::ToolSpec& spec, const core::ToolCall& call);

    /// Toupie « Réflexion… » : `tick` est appelé régulièrement par le thread
    /// principal ; `suspend`/`resume` l'effacent pendant une saisie.
    void start_thinking();
    /// Libellé de la toupie (« Réflexion » par défaut, remis à chaque
    /// `start_thinking`), par exemple « Chargement du modèle « gemma » ».
    void set_activity(std::string_view activity);
    void stop_thinking();
    void tick();
    void suspend_thinking();
    void resume_thinking();

    /// Réponse au fil de l'eau (fil de la requête) : fragment de texte du
    /// modèle. Le premier efface la toupie et ouvre « ◆ Avenoir » ; chaque
    /// ligne complète est rendue aussitôt (Markdown), un bloc de code ou un
    /// tableau une fois complet.
    void stream_text(std::string_view fragment);
    /// Fin d'une réponse en flux : la fin de ligne en attente est rendue.
    /// `kept` faux : la réponse est écartée (action annoncée mais non faite),
    /// ce qui est signalé ; la toupie reprend.
    void stream_end(bool kept);
    /// Vrai si une réponse a été montrée au fil de l'eau depuis
    /// `start_thinking` (elle n'est alors pas réaffichée en entier).
    [[nodiscard]] bool streamed();

    /// Invite de saisie : « › » coloré, ou « > » sans couleurs.
    [[nodiscard]] static std::string prompt_marker();

  private:
    /// Appel d'outil en attente de son résultat, et l'heure de son annonce.
    struct PendingCall {
        core::ToolCall call;
        std::chrono::steady_clock::time_point since;
    };

    void write(std::string_view text, std::FILE* stream = stdout);
    void emit_view(std::string out, std::FILE* stream = stdout) const;
    void clear_spinner();
    void draw_bar(bool marker) const;
    void draw_header() const;
    void draw_view() const;
    [[nodiscard]] std::string bottom_line() const;
    void redraw_all();
    [[nodiscard]] bool refresh_geometry();
    void rebuild_header();
    void relayout();
    [[nodiscard]] std::vector<std::string> append(std::string_view text);
    [[nodiscard]] std::size_t view_top() const;
    [[nodiscard]] std::size_t region_bottom() const;
    [[nodiscard]] std::size_t view_height() const;
    [[nodiscard]] std::size_t max_scroll() const;
    [[nodiscard]] std::size_t prompt_row() const;
    [[nodiscard]] std::size_t needed_input_rows() const;
    [[nodiscard]] std::string spinner_text() const;
    [[nodiscard]] static std::size_t width();
    /// Rend une ligne (ou un bloc) de réponse, préfixée de la barre « │ ».
    [[nodiscard]] static std::string render_reply(std::string_view markdown);
    void stream_line(std::string_view line);
    void stream_flush();

    std::mutex mutex_;
    mutable std::mutex status_mutex_; ///< À part : la ligne d'état est lue verrou principal pris.
    std::string status_llm_;          ///< Modèle actif (ligne d'état, en-tête).
    std::string status_conversation_; ///< Conversation en cours.
    std::optional<WelcomeInfo> welcome_; ///< Dernier accueil (en-tête redessiné).
    bool thinking_ = false;
    std::string activity_ = "Réflexion";
    bool shown_ = false;
    bool suspended_ = false;
    std::size_t frame_ = 0;
    bool pinned_ = false;
    bool reading_ = false;       ///< Une saisie est en cours dans la barre.
    bool panel_ = false;         ///< Un panneau cache la conversation.
    std::size_t rows_ = 0;       ///< Hauteur du terminal (plein écran).
    std::size_t columns_ = 0;    ///< Largeur du terminal (plein écran).
    std::size_t input_rows_ = 1; ///< Lignes de saisie de la barre.
    std::size_t last_prompt_width_ = 0;
    std::size_t last_line_width_ = 0;
    std::vector<std::string> header_;   ///< En-tête fixe, à la largeur actuelle.
    std::deque<std::string> lines_;     ///< Historique : lignes logiques (plein écran).
    std::deque<std::size_t> line_rows_; ///< Lignes d'écran de chacune.
    std::deque<std::string> screen_;    ///< Historique en lignes d'écran.
    std::size_t layout_width_ = 0;      ///< Largeur de découpe de `screen_`.
    std::size_t scroll_ = 0;            ///< Lignes d'écran remontées (0 : en bas).
    std::size_t unseen_ = 0;            ///< Lignes arrivées pendant qu'on remontait.
    std::chrono::steady_clock::time_point since_;
    std::chrono::steady_clock::time_point last_frame_;
    std::vector<PendingCall> pending_;
    bool streaming_ = false;     ///< Une réponse en flux est ouverte.
    bool streamed_ = false;      ///< Une réponse a été montrée en flux (depuis start_thinking).
    bool in_fence_ = false;      ///< Dans un bloc de code ``` en cours.
    std::string stream_pending_; ///< Fin de ligne reçue, pas encore rendue.
    std::string stream_held_;    ///< Bloc de code ou tableau retenu jusqu'à sa fin.
};

/// Affichage partagé par toute la CLI.
[[nodiscard]] Display& display();

/// « « firefox » » pour un seul argument texte, le JSON compact sinon.
[[nodiscard]] std::string summarize_arguments(const nlohmann::json& arguments);

/// Réaffiche des messages comme dans la conversation, les `limit` derniers
/// seulement si demandé (avec une ligne qui signale les messages masqués).
void print_messages(std::span<const core::Message> messages, std::optional<std::size_t> limit);

} // namespace avenoir::cli
