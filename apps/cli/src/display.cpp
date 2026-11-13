#include "display.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "avenoir/core/version.hpp"
#include "avenoir/terminal/markdown.hpp"
#include "avenoir/terminal/mascot.hpp"
#include "avenoir/terminal/style.hpp"

#include "console.hpp"
#include "wolf.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::paint;
using terminal::Style;

constexpr std::size_t kMaxWidth = 72;
constexpr Style kMuted{.color = Color::Gray};
// Thème d'Avenoir : bleus (nuit pour les cadres, vif pour les titres, clair
// pour les libellés) ; jaune et rouge gardent leur sens (attention, erreur).
constexpr Style kFrame{.color = Color::Navy};
constexpr Style kTitle{.color = Color::Azure, .bold = true};
constexpr Style kLabel{.color = Color::Sky};
constexpr Style kValue{.color = Color::Ice};
constexpr std::array<std::string_view, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                                       "⠴", "⠦", "⠧", "⠇", "⠏"};

/// Lignes logiques gardées pour remonter dans la conversation.
constexpr std::size_t kHistoryLines = 5000;

/// Hauteur minimale du plein écran (3 lignes de barre, 3 de conversation…).
constexpr std::size_t kMinPinnedRows = 8;

/// Lignes de conversation gardées visibles au moins sous l'en-tête.
constexpr std::size_t kMinViewRows = 6;

/// En-tête complet (grand loup) : taille du terminal nécessaire.
constexpr std::size_t kFullHeaderColumns = 82;
constexpr std::size_t kFullHeaderRows = 36;
/// En-tête compact (petit loup).
constexpr std::size_t kCompactHeaderColumns = 62;
constexpr std::size_t kCompactHeaderRows = 30;

/// Raccourcis rappelés dans l'en-tête.
constexpr std::string_view kHints =
    "/aide · /settings · molette ou PgUp/PgDn : défiler · Ctrl+C : arrêter · Ctrl+D : quitter";

/// Curseur en ligne `row`, colonne 1 (numérotation du terminal, depuis 1).
std::string go_to(std::size_t row) {
    return std::format("\x1b[{};1H", row);
}

/// Zone de défilement : lignes `top` à `bottom`.
std::string scroll_region(std::size_t top, std::size_t bottom) {
    return std::format("\x1b[{};{}r", top, bottom);
}

void emit(std::string_view text, std::FILE* stream = stdout) {
    (void)std::fwrite(text.data(), 1, text.size(), stream);
    (void)std::fflush(stream);
}

/// Première ligne d'un texte, raccourcie.
std::string first_line(std::string_view text, std::size_t width) {
    return terminal::ellipsize(text.substr(0, text.find('\n')), width);
}

std::string repeat(std::string_view piece, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += piece;
    }
    return out;
}

/// « 350 ms », « 1,2 s », « 42 s ».
std::string duration_label(std::chrono::steady_clock::duration elapsed) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (ms < 1000) {
        return std::format("{} ms", ms);
    }
    if (ms < 10000) {
        return std::format("{},{} s", ms / 1000, (ms % 1000) / 100);
    }
    return std::format("{} s", ms / 1000);
}

/// « AVENOIR » en grandes lettres (5 lignes, 42 colonnes), du bleu clair au
/// bleu nuit.
std::vector<std::string> title_art() {
    static const std::array<Color, 5> shades{Color::Ice, Color::Sky, Color::Azure, Color::Azure,
                                             Color::Navy};
    std::vector<std::string> lines;
    for (std::size_t row = 0; row < 5; ++row) {
        std::string line;
        for (const auto& letter : terminal::title_letters()) {
            line += std::string(letter.at(row)) + " ";
        }
        lines.push_back(paint(line, {.color = shades.at(row), .bold = true}));
    }
    return lines;
}

/// « nom : a, b, c », découpé à `width` colonnes avec un retrait.
std::vector<std::string> group_lines(const std::string& name, const std::vector<std::string>& items,
                                     std::size_t width) {
    std::string list;
    for (const auto& item : items) {
        list += (list.empty() ? "" : ", ") + item;
    }
    const std::string head = name + " : ";
    const std::size_t indent = std::min<std::size_t>(terminal::display_width(head), 14);
    const auto pieces = terminal::wrap(list, width > indent + 10 ? width - indent : 10);
    std::vector<std::string> lines;
    lines.reserve(pieces.size());
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        lines.push_back(i == 0 ? paint(head, kLabel) + pieces[i]
                               : std::string(indent, ' ') + pieces[i]);
    }
    return lines;
}

/// « 22 · système 12 · web 3 · … » : outils par famille.
std::string tools_summary(const WelcomeInfo& welcome) {
    std::string summary = std::to_string(welcome.tools);
    for (const auto& [name, items] : welcome.groups) {
        summary += std::format(" · {} {}", name, items.size());
    }
    return summary;
}

/// « Libellé       valeur », la valeur raccourcie à `width` colonnes au total.
std::string field(std::string_view label, std::string_view value, std::size_t width) {
    constexpr std::size_t kLabelWidth = 14;
    const std::size_t room = width > kLabelWidth + 4 ? width - kLabelWidth : 4;
    return paint(terminal::pad(label, kLabelWidth), kLabel) +
           paint(terminal::ellipsize(value, room), kValue);
}

/// Petit loup avec `text` à sa droite, centré verticalement.
std::vector<std::string> wolf_card(const std::vector<std::string>& text) {
    const auto art = wolf_icon();
    const std::size_t art_width = kWolfIconWidth;
    std::vector<std::string> lines;
    const std::size_t rows = std::max(art.size(), text.size());
    const std::size_t first = (rows - text.size()) / 2;
    for (std::size_t row = 0; row < rows; ++row) {
        std::string line =
            " " + (row < art.size() ? art[row] : std::string(art_width, ' ')) + "   ";
        if (row >= first && row - first < text.size()) {
            line += text[row - first];
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

/// Largeur laissée au texte d'un cadre de `inner` colonnes, à droite du loup.
std::size_t card_text_width(std::size_t inner) {
    const std::size_t art = kWolfIconWidth + 4;
    return inner > art ? inner - art : 0;
}

/// Colonnes côte à côte : `left` complétée à `left_width`, puis `right`.
std::vector<std::string> side_by_side(const std::vector<std::string>& left, std::size_t left_width,
                                      const std::vector<std::string>& right, std::size_t gap) {
    std::vector<std::string> lines;
    const std::size_t rows = std::max(left.size(), right.size());
    for (std::size_t row = 0; row < rows; ++row) {
        std::string line =
            row < left.size() ? terminal::pad(left[row], left_width) : std::string(left_width, ' ');
        line += std::string(gap, ' ');
        if (row < right.size()) {
            line += right[row];
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

/// En-tête fixe du plein écran, selon la taille du terminal : le grand loup
/// et tout l'état ; le petit loup et l'essentiel ; ou une seule ligne.
std::vector<std::string> build_header(const WelcomeInfo& welcome, std::size_t columns,
                                      std::size_t rows) {
    const std::string version = std::string(core::version());
    if (columns >= kFullHeaderColumns && rows >= kFullHeaderRows) {
        const std::size_t inner = columns - 4;
        const std::size_t right_width = inner - kWolfArtWidth - 3;
        std::vector<std::string> right = title_art();
        right.push_back(
            paint(std::format("assistant de votre système · version {}", version), kMuted));
        right.emplace_back();
        right.push_back(field("Modèle", welcome.llm, right_width));
        right.push_back(field("Conversation", welcome.conversation, right_width));
        right.push_back(field("Outils", tools_summary(welcome), right_width));
        for (const auto& [title, text] : welcome.sections) {
            right.push_back(field(title, text, right_width));
        }
        right.emplace_back();
        for (const auto& piece : terminal::wrap(kHints, right_width)) {
            right.push_back(paint(piece, kMuted));
        }
        // Colonne de droite centrée sur la hauteur du loup.
        const auto art = wolf_art();
        if (right.size() < art.size()) {
            right.insert(right.begin(), (art.size() - right.size()) / 2, std::string{});
        }
        right.resize(std::min(right.size(), art.size()));
        const auto lines = side_by_side(art, kWolfArtWidth, right, 3);
        const auto boxed =
            terminal::box(paint("Avenoir " + version, kTitle), lines, Color::Navy, columns);
        return terminal::layout(boxed, 0);
    }
    if (columns >= kCompactHeaderColumns && rows >= kCompactHeaderRows) {
        const std::size_t art_width = kWolfIconWidth;
        const std::size_t right_width = columns - 4 - art_width - 3;
        const std::vector<std::string> right = {
            paint("AVENOIR", kTitle) +
                paint("  " + version + " · assistant de votre système", kMuted),
            field("Modèle", welcome.llm, right_width),
            field("Conversation", welcome.conversation, right_width),
            field("Outils", tools_summary(welcome), right_width),
            field("Modules",
                  welcome.sections.empty() ? std::string("aucun") : welcome.sections.front().second,
                  right_width),
            paint(terminal::ellipsize(kHints, right_width), kMuted),
        };
        const auto art = wolf_icon();
        const auto lines = side_by_side(art, art_width, right, 3);
        return terminal::layout(terminal::box({}, lines, Color::Navy, columns), 0);
    }
    // Une ligne, puis un trait.
    const std::string line = paint("◆ Avenoir", kTitle) + " " +
                             paint(terminal::ellipsize(welcome.llm + " · " + welcome.conversation,
                                                       columns > 14 ? columns - 12 : 2),
                                   kMuted);
    return {line, paint(repeat("─", columns > 1 ? columns - 1 : 1), kFrame)};
}

} // namespace

Display& display() {
    static Display instance;
    return instance;
}

std::string summarize_arguments(const nlohmann::json& arguments) {
    if (arguments.is_object() && arguments.empty()) {
        return {};
    }
    if (arguments.is_object() && arguments.size() == 1 && arguments.begin()->is_string()) {
        return "« " + arguments.begin()->get<std::string>() + " »";
    }
    return arguments.dump();
}

std::size_t Display::width() {
    return std::min(terminal::terminal_width(), kMaxWidth);
}

std::size_t Display::view_top() const {
    return header_.size() + 1;
}

std::size_t Display::region_bottom() const {
    return rows_ - input_rows_ - 2;
}

std::size_t Display::view_height() const {
    const std::size_t bottom = region_bottom();
    return bottom >= view_top() ? bottom - view_top() + 1 : 1;
}

std::size_t Display::max_scroll() const {
    // La dernière ligne de la zone reste libre (toupie, indicateur).
    const std::size_t content = view_height() > 1 ? view_height() - 1 : 1;
    return screen_.size() > content ? screen_.size() - content : 0;
}

std::size_t Display::prompt_row() const {
    return rows_ - input_rows_;
}

std::size_t Display::needed_input_rows() const {
    // Le curseur peut se trouver juste après le dernier caractère : une
    // position de plus que le texte.
    const std::size_t columns = std::max<std::size_t>(columns_, 1);
    const std::size_t rows = ((last_prompt_width_ + last_line_width_) / columns) + 1;
    // Au moins quelques lignes de conversation.
    const std::size_t room = rows_ > header_.size() + 6 ? rows_ - header_.size() - 6 : 1;
    return std::clamp<std::size_t>(rows, 1, std::max<std::size_t>(room, 1));
}

bool Display::full_screen() {
    const std::scoped_lock lock(mutex_);
    return pinned_;
}

std::vector<std::string> Display::append(std::string_view text) {
    // Appelé verrou pris : le texte rejoint l'historique ; ses lignes d'écran
    // à la largeur actuelle sont rendues pour l'affichage.
    std::vector<std::string> added;
    for (auto& line : terminal::layout(text, 0)) {
        auto rows = terminal::layout(line, layout_width_);
        line_rows_.push_back(rows.size());
        for (auto& row : rows) {
            screen_.push_back(row);
            added.push_back(std::move(row));
        }
        lines_.push_back(std::move(line));
    }
    while (lines_.size() > kHistoryLines) {
        for (std::size_t i = 0; i < line_rows_.front() && !screen_.empty(); ++i) {
            screen_.pop_front();
        }
        lines_.pop_front();
        line_rows_.pop_front();
    }
    return added;
}

void Display::relayout() {
    // Appelé verrou pris : lignes d'écran recalculées à la nouvelle largeur.
    layout_width_ = std::max<std::size_t>(columns_, 1);
    screen_.clear();
    line_rows_.clear();
    for (const auto& line : lines_) {
        const auto rows = terminal::layout(line, layout_width_);
        line_rows_.push_back(rows.size());
        std::ranges::copy(rows, std::back_inserter(screen_));
    }
    scroll_ = std::min(scroll_, max_scroll());
}

void Display::rebuild_header() {
    // Appelé verrou pris : en-tête à la taille actuelle, réduit si la
    // conversation n'aurait plus assez de place.
    if (!welcome_) {
        header_.clear();
        return;
    }
    WelcomeInfo info = *welcome_;
    {
        const std::scoped_lock lock(status_mutex_);
        if (!status_llm_.empty()) {
            info.llm = status_llm_;
        }
        if (!status_conversation_.empty()) {
            info.conversation = status_conversation_;
        }
    }
    header_ = build_header(info, columns_, rows_);
    if (rows_ < header_.size() + 3 + kMinViewRows) {
        header_ = build_header(info, columns_, 0); // une seule ligne
    }
    if (rows_ < header_.size() + 3 + 3) {
        header_.clear();
    }
}

void Display::emit_view(std::string out, std::FILE* stream) const {
    // Appelé verrou pris. Pendant une saisie, le curseur est rendu exactement
    // où replxx l'attend (ESC 7 / ESC 8) ; sinon, il revient après l'invite.
    if (reading_) {
        out = "\x1b"
              "7" +
              out +
              "\x1b"
              "8";
    } else {
        out += go_to(prompt_row()) + "\x1b[3G";
    }
    emit(out, stream);
}

void Display::write(std::string_view text, std::FILE* stream) {
    // Appelé verrou pris.
    clear_spinner();
    if (!pinned_) {
        emit(text, stream);
        return;
    }
    const auto rows = append(text);
    if (panel_) {
        return; // montré à la fermeture du panneau
    }
    if (scroll_ > 0) {
        // On lit plus haut : la vue reste immobile, l'indicateur compte.
        scroll_ = std::min(scroll_ + rows.size(), max_scroll());
        unseen_ += rows.size();
        emit_view(go_to(region_bottom()) + "\x1b[2K" + bottom_line(), stream);
        return;
    }
    // En bas : écrit sur la dernière ligne de la zone (toujours vide), que
    // chaque « \r\n » fait défiler vers le haut.
    std::string out = go_to(region_bottom());
    for (const auto& row : rows) {
        out += "\x1b[2K" + row + "\r\n";
    }
    emit_view(std::move(out), stream);
}

std::string Display::bottom_line() const {
    // Dernière ligne de la zone : vide, ou l'indicateur de défilement.
    if (scroll_ == 0) {
        return {};
    }
    std::string text = std::format(" ↓ {} ligne{} plus bas", scroll_, scroll_ > 1 ? "s" : "");
    if (unseen_ > 0) {
        text += std::format(" · {} nouvelle{}", unseen_, unseen_ > 1 ? "s" : "");
    }
    if (thinking_) {
        text += " · " + activity_ + "…";
    }
    text += " · molette, PgDn ou Entrée : revenir ";
    const std::size_t used = terminal::display_width(text) + 2;
    return paint("──", kFrame) + paint(text, kLabel) +
           paint(repeat("─", columns_ > used + 1 ? columns_ - used - 1 : 0), kFrame);
}

void Display::draw_view() const {
    // Appelé verrou pris, en plein écran : la conversation à sa position de
    // défilement, alignée en bas de la zone (comme quand elle défile).
    const std::size_t top = view_top();
    const std::size_t content = view_height() > 1 ? view_height() - 1 : 1;
    const std::size_t end = screen_.size() - std::min(scroll_, screen_.size());
    const std::size_t start = end > content ? end - content : 0;
    const std::size_t shown = end - start;
    std::string out;
    for (std::size_t i = 0; i < content; ++i) {
        out += go_to(top + i) + "\x1b[2K";
        const std::size_t blank = content - shown;
        if (i >= blank) {
            out += screen_[start + i - blank];
        }
    }
    out += go_to(region_bottom()) + "\x1b[2K" + bottom_line();
    emit_view(std::move(out));
}

void Display::draw_header() const {
    // Appelé verrou pris : l'en-tête, au-dessus de la zone de défilement.
    std::string out;
    for (std::size_t i = 0; i < header_.size(); ++i) {
        out += go_to(i + 1) + "\x1b[2K" + header_[i];
    }
    emit_view(std::move(out));
}

void Display::redraw_all() {
    // Appelé verrou pris : écran vidé, puis en-tête, conversation et zone de
    // défilement ; l'appelant redessine ensuite la barre.
    emit("\x1b[r\x1b[2J");
    draw_header();
    emit(scroll_region(view_top(), region_bottom()));
    if (panel_) {
        return; // le panneau est redessiné par son propriétaire
    }
    draw_view();
}

void Display::clear_spinner() {
    if (!shown_) {
        return;
    }
    shown_ = false;
    // La toupie occupe la dernière ligne de la zone (plein écran) ou la
    // ligne courante.
    if (pinned_) {
        emit_view(go_to(region_bottom()) + "\x1b[2K" + bottom_line());
    } else {
        emit("\r\x1b[2K");
    }
}

std::string Display::bar_rule() {
    // Une colonne de moins que l'écran : une ligne pleine ferait passer le
    // curseur à la ligne suivante et décalerait les calculs de replxx.
    const std::size_t columns = terminal::terminal_width();
    return paint(repeat("─", columns > 1 ? columns - 1 : 1), kFrame);
}

std::string Display::status_rule() const {
    std::string llm;
    std::string conversation;
    {
        const std::scoped_lock lock(status_mutex_);
        llm = status_llm_;
        conversation = status_conversation_;
    }
    const std::size_t columns = terminal::terminal_width();
    if (llm.empty() || columns < 24) {
        return bar_rule();
    }
    // « ── gemma : gemma4:26b-a4b, local ── « Titre » (n° 3) ─────… », une
    // colonne de moins que l'écran.
    llm = terminal::ellipsize(llm, columns / 2);
    std::string out = paint("── ", kFrame) + paint(llm, kLabel);
    std::size_t used = 4 + terminal::display_width(llm);
    if (!conversation.empty() && columns > used + 16) {
        conversation = terminal::ellipsize(conversation, columns - used - 10);
        out += paint(" ── ", kFrame) + paint(conversation, kMuted);
        used += 4 + terminal::display_width(conversation);
    }
    return out + paint(" " + repeat("─", columns > used + 2 ? columns - used - 2 : 0), kFrame);
}

void Display::draw_bar(bool marker) const {
    // Appelé verrou pris, en plein écran. Efface toute la barre (lignes de
    // saisie comprises), trace les deux bordures et place le curseur au
    // début de la ligne d'invite. L'invite est dessinée en gris quand Avenoir
    // ne lit pas (pendant une réflexion) ; sinon c'est replxx qui l'écrit.
    std::string out = scroll_region(view_top(), region_bottom()) + go_to(prompt_row() - 1) +
                      "\x1b[J" + bar_rule() + go_to(rows_) + status_rule() + go_to(prompt_row());
    if (marker) {
        out += paint(kPromptGlyph, {.color = Color::Gray}) + " ";
    }
    emit(out);
}

bool Display::refresh_geometry() {
    // Appelé verrou pris, en plein écran. Vrai si la taille a changé.
    const std::size_t rows = terminal::terminal_height();
    const std::size_t columns = terminal::terminal_width();
    if (rows == rows_ && columns == columns_) {
        return false;
    }
    rows_ = rows;
    columns_ = columns;
    if (rows_ < kMinPinnedRows) {
        // Terminal trop petit : retour à l'affichage qui défile.
        pinned_ = false;
        set_mouse_reporting(false);
        emit("\x1b[r\x1b[?1049l");
        return true;
    }
    input_rows_ = reading_ ? needed_input_rows() : 1;
    // Selon le terminal, un redimensionnement recoupe ou déplace les lignes
    // déjà affichées : l'écran est redessiné entièrement, à la nouvelle
    // taille ; l'appelant redessine ensuite la barre.
    rebuild_header();
    relayout();
    redraw_all();
    return true;
}

void Display::enable_pinned_bar(bool supported) {
    const std::scoped_lock lock(mutex_);
    const std::size_t rows = terminal::terminal_height();
    if (!supported || rows < kMinPinnedRows) {
        return;
    }
    rows_ = rows;
    columns_ = terminal::terminal_width();
    layout_width_ = std::max<std::size_t>(columns_, 1);
    input_rows_ = 1;
    pinned_ = true;
    // Écran alterné (le terminal retrouve son contenu à la sortie), molette
    // rapportée à Avenoir.
    emit("\x1b[?1049h\x1b[2J\x1b[H");
    set_mouse_reporting(true);
    redraw_all();
    draw_bar(/*marker=*/true);
}

void Display::shutdown() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (!pinned_) {
        return;
    }
    pinned_ = false;
    set_mouse_reporting(false);
    // Zone de défilement remise à tout l'écran, retour à l'écran principal.
    emit("\x1b[r\x1b[?1049l");
}

void Display::prepare_input() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (!pinned_) {
        return; // Hors plein écran (tube…) : rien à tracer.
    }
    reading_ = true;
    last_prompt_width_ = 0;
    last_line_width_ = 0;
    (void)refresh_geometry();
    if (pinned_) {
        draw_bar(/*marker=*/false);
    }
}

void Display::fit_input(std::string_view prompt, std::string_view line) {
    const std::scoped_lock lock(mutex_);
    if (!pinned_ || !reading_) {
        return;
    }
    last_prompt_width_ = terminal::display_width(prompt);
    last_line_width_ = terminal::display_width(line);
    const std::size_t needed = needed_input_rows();
    if (needed <= input_rows_) {
        return; // La barre ne rétrécit qu'après la saisie.
    }
    // La saisie a besoin de `grow` lignes de plus : la barre monte d'autant.
    // 1. La conversation défile de `grow` lignes vers le haut (CSI S : limité
    //    à la zone de défilement, le curseur ne bouge pas).
    // 2. Curseur mémorisé, nouvelle zone de défilement, bordure du haut et
    //    invite redessinées à leur nouvelle place, curseur restauré : rien ne
    //    défile entre la mémorisation et la restauration (comportement
    //    identique dans tous les terminaux).
    // 3. Le curseur remonte de `grow` lignes : replxx, qui se déplace en
    //    relatif depuis le curseur, réécrit alors la saisie sous la nouvelle
    //    invite et efface l'ancienne.
    const std::size_t grow = needed - input_rows_;
    input_rows_ = needed;
    std::string out = std::format("\x1b[{}S", grow);
    out += "\x1b"
           "7"; // ESC 7 : mémorise le curseur
    out += scroll_region(view_top(), region_bottom()) + go_to(prompt_row() - 1) + "\x1b[2K" +
           bar_rule() + go_to(prompt_row()) + "\x1b[2K" + std::string(prompt);
    out += "\x1b"
           "8"; // ESC 8 : le restaure
    out += std::format("\x1b[{}A", grow);
    emit(out);
    if (scroll_ > 0 || panel_) {
        scroll_ = std::min(scroll_, max_scroll());
        if (!panel_) {
            draw_view(); // la vue reste où on lisait
        }
    }
}

void Display::redraw_for_resize(std::string_view prompt, std::size_t before_cursor) {
    const std::scoped_lock lock(mutex_);
    if (!pinned_) {
        return;
    }
    (void)refresh_geometry();
    if (!pinned_) {
        return;
    }
    draw_bar(/*marker=*/false);
    // replxx redessine la saisie en remontant, depuis le curseur, du nombre de
    // lignes qui sépare le curseur de l'invite : on l'y place.
    const std::size_t offset =
        (terminal::display_width(prompt) + before_cursor) / std::max<std::size_t>(columns_, 1);
    std::string out(prompt);
    if (offset > 0) {
        out += std::format("\x1b[{}B", offset);
    }
    emit(out);
}

void Display::finish_input(std::string_view prompt, bool entered, std::string_view text) {
    const std::scoped_lock lock(mutex_);
    reading_ = false;
    if (!pinned_) {
        return;
    }
    const bool request = prompt == prompt_marker();
    bool redraw = false;
    if (input_rows_ > 1) {
        // Retour à une seule ligne de saisie : la zone de défilement
        // s'agrandit, la conversation est redessinée.
        input_rows_ = 1;
        redraw = true;
    }
    if (entered && request && scroll_ > 0) {
        // Une requête ramène la conversation en bas.
        scroll_ = 0;
        unseen_ = 0;
        redraw = true;
    }
    draw_bar(/*marker=*/true);
    if (redraw && !panel_) {
        draw_view();
    }
    if (entered && !text.empty() && !panel_) {
        // La saisie rejoint l'historique, sans cadre (pas celles d'un menu).
        write(std::string(request ? "\n" : "") + std::string(prompt) +
              paint(text, {.bold = request}) + "\n");
    }
}

void Display::scroll(ScrollStep step) {
    const std::scoped_lock lock(mutex_);
    if (!pinned_ || panel_) {
        return;
    }
    const std::size_t page = view_height() > 3 ? view_height() - 2 : 1;
    std::size_t up = 0;
    std::size_t down = 0;
    switch (step) {
    case ScrollStep::WheelUp:
        up = 3;
        break;
    case ScrollStep::WheelDown:
        down = 3;
        break;
    case ScrollStep::LineUp:
        up = 1;
        break;
    case ScrollStep::LineDown:
        down = 1;
        break;
    case ScrollStep::PageUp:
        up = page;
        break;
    case ScrollStep::PageDown:
        down = page;
        break;
    case ScrollStep::End:
        down = scroll_;
        break;
    }
    const std::size_t before = scroll_;
    scroll_ = std::min(scroll_ + up, max_scroll());
    scroll_ = scroll_ > down ? scroll_ - down : 0;
    if (scroll_ == 0) {
        unseen_ = 0;
    }
    if (scroll_ != before) {
        shown_ = false; // la toupie est redessinée au prochain tic
        draw_view();
    }
}

void Display::clear() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    lines_.clear();
    line_rows_.clear();
    screen_.clear();
    scroll_ = 0;
    unseen_ = 0;
    if (pinned_) {
        redraw_all();
        draw_bar(/*marker=*/true);
    } else if (terminal::enabled()) {
        emit("\x1b[2J\x1b[3J\x1b[H");
    }
}

void Display::banner(const WelcomeInfo& welcome) {
    {
        const std::scoped_lock lock(status_mutex_);
        status_llm_ = welcome.llm;
        status_conversation_ = welcome.conversation;
    }
    const std::scoped_lock lock(mutex_);
    welcome_ = welcome;
    if (pinned_) {
        // En-tête fixe, puis, dans la conversation, les outils par famille.
        rebuild_header();
        scroll_ = std::min(scroll_, max_scroll());
        redraw_all();
        draw_bar(/*marker=*/!reading_);
        const std::size_t text_width = std::min<std::size_t>(columns_, 140) - 4;
        std::string out = "\n" + paint("Outils disponibles", kTitle) +
                          paint(std::format("  {} au total", welcome.tools), kMuted) + "\n";
        for (const auto& [name, items] : welcome.groups) {
            for (const auto& line : group_lines(name, items, text_width)) {
                out += "  " + line + "\n";
            }
        }
        out += paint("  Réglages : /settings · commandes : /aide · défiler : molette, PgUp/PgDn, "
                     "Ctrl+↑/↓",
                     kMuted) +
               "\n";
        write(out);
        return;
    }

    // Hors plein écran : l'accueil défile avec le reste. Large et en
    // couleurs : le titre, puis le grand loup à côté des outils.
    const std::size_t total = std::min<std::size_t>(terminal::terminal_width(), 140);
    if (total < kFullHeaderColumns) {
        const std::size_t narrow = width();
        const std::size_t text_width = card_text_width(narrow - 4);
        const std::vector<std::string> text = {
            paint(std::format("Avenoir {}", core::version()), {.bold = true}),
            paint(terminal::ellipsize(std::format("{} · {} outil(s)", welcome.llm, welcome.tools),
                                      text_width),
                  kMuted),
            paint(terminal::ellipsize(std::format("Conversation : {}", welcome.conversation),
                                      text_width),
                  kMuted),
        };
        write("\n" + terminal::box({}, wolf_card(text), Color::Navy, narrow) + "\n");
        write(paint("  /aide pour les commandes · Ctrl+C arrête · Ctrl+D quitte", kMuted) + "\n");
        return;
    }
    const std::size_t right_width = total - 4 - kWolfArtWidth - 3;
    std::vector<std::string> right;
    right.push_back(field("Modèle", welcome.llm, right_width));
    right.push_back(field("Conversation", welcome.conversation, right_width));
    right.emplace_back();
    right.push_back(paint("Outils disponibles", kTitle));
    for (const auto& [name, items] : welcome.groups) {
        for (auto& line : group_lines(name, items, right_width)) {
            right.push_back(std::move(line));
        }
    }
    for (const auto& [title, text] : welcome.sections) {
        right.push_back(field(title, text, right_width));
    }
    right.emplace_back();
    for (const auto& piece : terminal::wrap(kHints, right_width)) {
        right.push_back(paint(piece, kMuted));
    }
    std::string out = "\n";
    for (const auto& line : title_art()) {
        out += "  " + line + "\n";
    }
    out += terminal::box(paint(std::format("Avenoir {}", core::version()), kTitle),
                         side_by_side(wolf_art(), kWolfArtWidth, right, 3), Color::Navy, total);
    write(out + "\n");
}

void Display::set_status(std::string_view llm, std::string_view conversation) {
    {
        const std::scoped_lock lock(status_mutex_);
        if (llm == status_llm_ && conversation == status_conversation_) {
            return;
        }
        status_llm_ = std::string(llm);
        status_conversation_ = std::string(conversation);
    }
    const std::scoped_lock lock(mutex_);
    if (!pinned_ || !welcome_) {
        return;
    }
    // L'en-tête suit ; sa hauteur peut changer : tout est redessiné.
    const std::size_t before = header_.size();
    rebuild_header();
    if (header_.size() != before) {
        scroll_ = std::min(scroll_, max_scroll());
        redraw_all();
    } else {
        draw_header();
    }
    draw_bar(/*marker=*/!reading_);
}

void Display::farewell(std::string_view conversation) {
    const std::size_t text_width = card_text_width(width() - 4);
    const std::vector<std::string> text = {
        paint("À bientôt !", {.bold = true}),
        paint(terminal::ellipsize(std::format("Conversation : {}", conversation), text_width),
              kMuted),
        paint(terminal::ellipsize("Elle reprendra au prochain lancement.", text_width), kMuted),
    };
    const auto lines = wolf_card(text);
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (pinned_) {
        // Retour à l'écran principal, tel qu'avant le lancement.
        pinned_ = false;
        set_mouse_reporting(false);
        emit("\x1b[r\x1b[?1049l");
    }
    write(terminal::box({}, lines, Color::Navy, width()) + "\n");
}

void Display::show_panel(const std::vector<std::string>& lines) {
    const std::scoped_lock lock(mutex_);
    if (!pinned_) {
        return;
    }
    clear_spinner();
    panel_ = true;
    const std::size_t top = view_top();
    const std::size_t height = view_height();
    std::string out;
    for (std::size_t i = 0; i < height; ++i) {
        out += go_to(top + i) + "\x1b[2K";
        if (i < lines.size()) {
            out += lines[i];
        }
    }
    emit_view(std::move(out));
}

void Display::hide_panel() {
    const std::scoped_lock lock(mutex_);
    if (!panel_) {
        return;
    }
    panel_ = false;
    if (pinned_) {
        scroll_ = 0;
        unseen_ = 0;
        draw_view();
    }
}

std::pair<std::size_t, std::size_t> Display::panel_size() {
    const std::scoped_lock lock(mutex_);
    if (!pinned_) {
        return {0, 0};
    }
    return {view_height(), columns_};
}

void Display::user(std::string_view text) {
    const std::string line =
        "\n" + paint(kPromptGlyph, kTitle) + " " + paint(text, {.bold = true}) + "\n";
    const std::scoped_lock lock(mutex_);
    write(line);
}

std::string Display::render_reply(std::string_view markdown) {
    const std::string bar = paint("│", kFrame);
    // Lignes découpées aux mots pour que chaque ligne garde la barre « │ ».
    const std::size_t columns = std::max<std::size_t>(terminal::terminal_width(), 20) - 3;
    std::string out;
    const auto lines = terminal::render_markdown(markdown);
    if (lines.empty()) {
        return bar + "\n"; // ligne vide entre deux paragraphes
    }
    for (const auto& line : lines) {
        for (const auto& piece : terminal::wrap(line, columns)) {
            out += bar;
            out += ' ';
            out += piece;
            out += '\n';
        }
    }
    return out;
}

void Display::assistant(std::string_view markdown) {
    const std::string out = "\n" + paint("◆ Avenoir", kTitle) + "\n" + render_reply(markdown);
    const std::scoped_lock lock(mutex_);
    write(out);
}

void Display::stream_text(std::string_view fragment) {
    const std::scoped_lock lock(mutex_);
    if (!streaming_) {
        streaming_ = true;
        streamed_ = true;
        suspended_ = true; // pas de toupie pendant que le texte arrive
        in_fence_ = false;
        stream_pending_.clear();
        stream_held_.clear();
        write("\n" + paint("◆ Avenoir", kTitle) + "\n");
    }
    stream_pending_.append(fragment);
    std::size_t start = 0;
    for (auto end = stream_pending_.find('\n'); end != std::string::npos;
         end = stream_pending_.find('\n', start)) {
        stream_line(std::string_view(stream_pending_).substr(start, end - start));
        start = end + 1;
    }
    stream_pending_.erase(0, start);
}

void Display::stream_line(std::string_view line) {
    // Appelé verrou pris. Un bloc de code (```) ou un tableau (lignes « | »)
    // n'est rendu qu'une fois complet : rendu ligne à ligne, il serait faux.
    const bool fence = line.starts_with("```");
    const bool table = line.starts_with('|');
    if (in_fence_ || fence) {
        stream_held_ += std::string(line) + "\n";
        if (fence) {
            in_fence_ = !in_fence_;
            if (!in_fence_) {
                write(render_reply(stream_held_));
                stream_held_.clear();
            }
        }
        return;
    }
    if (table) {
        stream_held_ += std::string(line) + "\n";
        return;
    }
    if (!stream_held_.empty()) { // fin d'un tableau
        write(render_reply(stream_held_));
        stream_held_.clear();
    }
    write(render_reply(line));
}

void Display::stream_flush() {
    // Appelé verrou pris : fin de ligne et bloc retenus rendus tels quels.
    if (!stream_pending_.empty()) {
        stream_held_ += stream_pending_;
        stream_pending_.clear();
    }
    if (!stream_held_.empty()) {
        write(render_reply(stream_held_));
        stream_held_.clear();
    }
    in_fence_ = false;
}

void Display::stream_end(bool kept) {
    const std::scoped_lock lock(mutex_);
    if (!streaming_) {
        return;
    }
    stream_flush();
    if (!kept) {
        write(paint("  ↻ Réponse écartée : elle annonçait une action qui n'a pas été faite. "
                    "Nouvelle tentative…",
                    kMuted) +
              "\n");
    }
    streaming_ = false;
    suspended_ = false; // la toupie reprend jusqu'à la réponse suivante
}

bool Display::streamed() {
    const std::scoped_lock lock(mutex_);
    return streamed_;
}

void Display::tool_calls(std::span<const core::ToolCall> calls) {
    const std::scoped_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& call : calls) {
        pending_.push_back({.call = call, .since = now});
    }
}

void Display::tool_result(const core::ToolResultMessage& result) {
    const std::scoped_lock lock(mutex_);
    const auto pending =
        std::ranges::find(pending_, result.call_id, [](const PendingCall& p) { return p.call.id; });
    if (pending == pending_.end()) {
        return;
    }
    const auto& call = pending->call;
    std::string line = "  " + paint(kToolGlyph, kLabel) + " " + call.name;
    if (const auto arguments = summarize_arguments(call.arguments); !arguments.empty()) {
        line += " " + paint(terminal::ellipsize(arguments, 40), kMuted);
    }
    if (result.is_error) {
        line += "  " + paint("✗", {.color = Color::Red, .bold = true}) + " " +
                paint(first_line(result.content, 50), {.color = Color::Red});
    } else {
        line += "  " + paint("✓", {.color = Color::Green, .bold = true});
    }
    // Durée de l'action, quand elle compte (pas au réaffichage d'une conversation).
    const auto elapsed = std::chrono::steady_clock::now() - pending->since;
    if (elapsed >= std::chrono::milliseconds(100)) {
        line += "  " + paint(duration_label(elapsed), kMuted);
    }
    pending_.erase(pending);
    write(line + "\n");
}

void Display::info(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(paint(text, kMuted) + "\n");
}

void Display::notify(std::string_view text) {
    // Pendant la saisie aussi : l'écriture rend le curseur où il était.
    const std::scoped_lock lock(mutex_);
    write(paint(text, kMuted) + "\n");
}

void Display::success(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(paint("✓", {.color = Color::Green, .bold = true}) + " " + std::string(text) + "\n");
}

void Display::warning(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(paint(std::string(kWarningGlyph) + " " + std::string(text), {.color = Color::Yellow}) +
          "\n");
}

void Display::error(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(paint("✗ Erreur : " + std::string(text), {.color = Color::Red}) + "\n", stderr);
}

void Display::plain(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(std::string(text) + "\n");
}

void Display::confirmation(const core::ToolSpec& spec, const core::ToolCall& call) {
    const std::size_t inner = width() - 4;
    std::vector<std::string> lines = {paint(spec.name, {.bold = true}) +
                                      "   risque : " + terminal::risk_badge(spec.risk)};
    for (auto& line : terminal::wrap(spec.description, inner)) {
        lines.push_back(std::move(line));
    }
    for (const auto& line : terminal::wrap(call.arguments.dump(), inner)) {
        lines.push_back(paint(line, kMuted));
    }
    const std::scoped_lock lock(mutex_);
    if (scroll_ > 0 && !panel_) {
        // Une confirmation se voit toujours : retour en bas.
        scroll_ = 0;
        unseen_ = 0;
        draw_view();
    }
    write("\n" +
          terminal::box(std::string(kWarningGlyph) + " Action à confirmer", lines, Color::Yellow,
                        width()) +
          "\n");
}

void Display::start_thinking() {
    const std::scoped_lock lock(mutex_);
    thinking_ = terminal::enabled(); // Pas d'animation hors d'un terminal.
    suspended_ = false;
    streamed_ = false;
    activity_ = "Réflexion";
    since_ = std::chrono::steady_clock::now();
    last_frame_ = {};
}

void Display::set_activity(std::string_view activity) {
    const std::scoped_lock lock(mutex_);
    activity_ = std::string(activity);
}

void Display::stop_thinking() {
    const std::scoped_lock lock(mutex_);
    if (streaming_) { // réponse en flux interrompue (Ctrl+C, erreur)
        stream_flush();
        streaming_ = false;
    }
    clear_spinner();
    thinking_ = false;
    if (pinned_ && scroll_ > 0 && !panel_) {
        emit_view(go_to(region_bottom()) + "\x1b[2K" + bottom_line());
    }
    (void)std::fflush(stdout);
}

std::string Display::spinner_text() const {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since_)
            .count();
    return std::format(
        "  {} {}", paint(kSpinner.at(frame_), {.color = Color::Azure}),
        paint(std::format("{}… {} s · Ctrl+C pour arrêter", activity_, seconds), kMuted));
}

void Display::tick() {
    const std::scoped_lock lock(mutex_);
    if (!thinking_ || suspended_) {
        return;
    }
    // Terminal redimensionné pendant la réflexion : écran recalé.
    if (pinned_ && refresh_geometry() && pinned_) {
        draw_bar(/*marker=*/true);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_frame_ < std::chrono::milliseconds(100)) {
        return;
    }
    last_frame_ = now;
    frame_ = (frame_ + 1) % kSpinner.size();
    if (!pinned_) {
        emit("\r\x1b[2K" + spinner_text());
        shown_ = true;
        return;
    }
    if (panel_) {
        return;
    }
    // Plein écran : la toupie s'affiche avec les réponses, sur la dernière
    // ligne de la zone ; en lisant plus haut, l'indicateur la remplace.
    if (scroll_ > 0) {
        emit_view(go_to(region_bottom()) + "\x1b[2K" + bottom_line());
        return;
    }
    emit_view(go_to(region_bottom()) + "\x1b[2K" + spinner_text());
    shown_ = true;
}

void Display::suspend_thinking() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    suspended_ = true;
}

void Display::resume_thinking() {
    const std::scoped_lock lock(mutex_);
    suspended_ = false;
}

std::string Display::prompt_marker() {
    return terminal::enabled() ? paint(kPromptGlyph, kTitle) + " " : "> ";
}

void print_messages(std::span<const core::Message> messages, std::optional<std::size_t> limit) {
    if (limit && messages.size() > *limit) {
        display().info(
            std::format("… {} message(s) plus ancien(s) — /historique pour tout afficher",
                        messages.size() - *limit));
        messages = messages.last(*limit);
    }
    for (const auto& message : messages) {
        std::visit(
            [](const auto& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, core::UserMessage>) {
                    display().user(m.content);
                } else if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                    display().tool_calls(m.tool_calls);
                    if (!m.text.empty()) {
                        display().assistant(m.text);
                    }
                } else {
                    display().tool_result(m);
                }
            },
            message);
    }
}

} // namespace avenoir::cli
