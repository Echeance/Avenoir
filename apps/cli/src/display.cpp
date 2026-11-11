#include "display.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "avenoir/core/version.hpp"
#include "avenoir/terminal/markdown.hpp"
#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::paint;
using terminal::Style;

constexpr std::size_t kMaxWidth = 72;
constexpr Style kMuted{.color = Color::Gray};
constexpr std::array<std::string_view, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                                       "⠴", "⠦", "⠧", "⠇", "⠏"};

/// Morceaux d'historique retenus pour le réafficher après un redimensionnement.
constexpr std::size_t kTranscriptChunks = 400;

/// Hauteur minimale pour la barre fixe (3 lignes de barre + 2 d'historique).
constexpr std::size_t kMinPinnedRows = 5;

/// Curseur en ligne `row`, colonne 1 (numérotation du terminal, depuis 1).
std::string go_to(std::size_t row) {
    return std::format("\x1b[{};1H", row);
}

/// Zone de défilement : lignes 1 à `bottom`.
std::string scroll_region(std::size_t bottom) {
    return std::format("\x1b[1;{}r", bottom);
}

void emit(std::string_view text) {
    (void)std::fwrite(text.data(), 1, text.size(), stdout);
    (void)std::fflush(stdout);
}

/// Première ligne d'un texte, raccourcie.
std::string first_line(std::string_view text, std::size_t width) {
    return terminal::ellipsize(text.substr(0, text.find('\n')), width);
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

std::size_t Display::region_bottom() const {
    return rows_ - input_rows_ - 2;
}

std::size_t Display::prompt_row() const {
    return rows_ - input_rows_;
}

std::size_t Display::needed_input_rows() const {
    // Le curseur peut se trouver juste après le dernier caractère : une
    // position de plus que le texte.
    const std::size_t columns = std::max<std::size_t>(columns_, 1);
    const std::size_t rows = ((last_prompt_width_ + last_line_width_) / columns) + 1;
    return std::clamp<std::size_t>(rows, 1, rows_ - 3); // au moins 1 ligne d'historique
}

void Display::write(std::string_view text, std::FILE* stream) {
    // Appelé verrou pris.
    clear_spinner();
    if (!pinned_) {
        (void)std::fwrite(text.data(), 1, text.size(), stream);
        (void)std::fflush(stream);
        return;
    }
    // Barre fixe : on écrit en bas de la zone de défilement (dont la dernière
    // ligne est toujours vide, chaque écriture finissant par « \n »), puis le
    // curseur revient dans la barre, après l'invite. Le texte est retenu pour
    // pouvoir le réafficher après un redimensionnement.
    transcript_.emplace_back(text);
    while (transcript_.size() > kTranscriptChunks) {
        transcript_.pop_front();
    }
    (void)std::fflush(stdout);
    const std::string out =
        go_to(region_bottom()) + std::string(text) + go_to(prompt_row()) + "\x1b[3G";
    (void)std::fwrite(out.data(), 1, out.size(), stream);
    (void)std::fflush(stream);
}

void Display::clear_spinner() {
    if (!shown_) {
        return;
    }
    shown_ = false;
    // La toupie occupe la dernière ligne de l'historique (barre fixe) ou la
    // ligne courante.
    if (pinned_) {
        emit(go_to(region_bottom()) + "\x1b[2K" + go_to(prompt_row()) + "\x1b[3G");
    } else {
        emit("\r\x1b[2K");
    }
}

std::string Display::bar_rule() {
    // Une colonne de moins que l'écran : une ligne pleine ferait passer le
    // curseur à la ligne suivante et décalerait les calculs de replxx.
    const std::size_t columns = terminal::terminal_width();
    std::string line;
    for (std::size_t i = 0; i + 1 < columns; ++i) {
        line += "─";
    }
    return paint(line, kMuted);
}

void Display::draw_bar(bool marker) const {
    // Appelé verrou pris, en mode barre fixe. Efface toute la barre (lignes
    // de saisie comprises), trace les deux bordures et place le curseur au
    // début de la ligne d'invite. L'invite est dessinée en gris quand Avenoir
    // ne lit pas (pendant une réflexion) ; sinon c'est replxx qui l'écrit.
    std::string out = scroll_region(region_bottom()) + go_to(prompt_row() - 1) + "\x1b[J" +
                      bar_rule() + go_to(rows_) + bar_rule() + go_to(prompt_row());
    if (marker) {
        out += paint(kPromptGlyph, {.color = Color::Gray}) + " ";
    }
    emit(out);
}

bool Display::refresh_geometry() {
    // Appelé verrou pris, en mode barre fixe. Vrai si la taille a changé.
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
        emit("\x1b[r\x1b[2J\x1b[H");
        return true;
    }
    input_rows_ = reading_ ? needed_input_rows() : 1;
    // Selon le terminal, un redimensionnement recoupe ou déplace les lignes
    // déjà affichées (Alacritty réorganise le texte à la nouvelle largeur) :
    // les restes de l'ancienne barre se retrouveraient n'importe où. L'écran
    // est donc vidé, puis la fin de l'historique réaffichée à la nouvelle
    // largeur ; l'appelant redessine ensuite la barre.
    replay();
    return true;
}

void Display::replay() {
    // Appelé verrou pris, en mode barre fixe.
    const std::size_t height = region_bottom();
    const std::size_t columns = std::max<std::size_t>(columns_, 1);
    // Morceaux les plus récents qui tiennent dans la zone de défilement.
    std::size_t lines = 0;
    auto first = transcript_.end();
    while (first != transcript_.begin() && lines < height) {
        --first;
        std::string_view chunk = *first;
        while (!chunk.empty()) {
            const auto end = chunk.find('\n');
            const std::size_t width = terminal::display_width(
                chunk.substr(0, end == std::string_view::npos ? chunk.size() : end));
            lines += std::max<std::size_t>(1, (width + columns - 1) / columns);
            if (end == std::string_view::npos) {
                break;
            }
            chunk.remove_prefix(end + 1);
        }
    }
    std::string out = "\x1b[r\x1b[2J" + scroll_region(height) + go_to(height);
    for (auto chunk = first; chunk != transcript_.end(); ++chunk) {
        out += *chunk;
    }
    emit(out);
}

void Display::enable_pinned_bar(bool supported) {
    const std::scoped_lock lock(mutex_);
    const std::size_t rows = terminal::terminal_height();
    if (!supported || rows < kMinPinnedRows) {
        return;
    }
    rows_ = rows;
    columns_ = terminal::terminal_width();
    input_rows_ = 1;
    pinned_ = true;
    // Fait défiler le contenu actuel hors de l'écran (il reste dans
    // l'historique du terminal), puis réserve les lignes de la barre.
    emit(std::string(rows_, '\n'));
    draw_bar(/*marker=*/true);
}

void Display::shutdown() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (!pinned_) {
        return;
    }
    pinned_ = false;
    // Zone de défilement remise à tout l'écran, barre effacée, curseur en bas.
    emit(std::string("\x1b[r") + go_to(prompt_row() - 1) + "\x1b[J");
}

void Display::prepare_input() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (!pinned_) {
        return; // Hors barre fixe (tube…) : rien à tracer.
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
    // 1. L'historique défile de `grow` lignes vers le haut (CSI S : limité à
    //    la zone de défilement, le curseur ne bouge pas).
    // 2. Curseur mémorisé, nouvelle zone de défilement, bordure du haut et
    //    invite redessinées à leur nouvelle place, curseur restauré : rien ne
    //    défile entre la mémorisation et la restauration (comportement
    //    identique dans tous les terminaux).
    // 3. Le curseur remonte de `grow` lignes : replxx, qui se déplace en
    //    relatif depuis le curseur, réécrit alors la saisie sous la nouvelle
    //    invite et efface l'ancienne.
    const std::size_t grow = needed - input_rows_;
    input_rows_ = needed;
    // La zone de défilement en place est l'ancienne : CSI S y fait défiler
    // l'historique sans déplacer le curseur.
    std::string out = std::format("\x1b[{}S", grow);
    // Changer de zone de défilement ramène le curseur en haut : il est
    // mémorisé juste avant, puis restauré (plus rien ne défile entre-temps).
    out += "\x1b"
           "7"; // ESC 7 : mémorise le curseur
    out += scroll_region(region_bottom()) + go_to(prompt_row() - 1) + "\x1b[2K" + bar_rule() +
           go_to(prompt_row()) + "\x1b[2K" + std::string(prompt);
    out += "\x1b"
           "8"; // ESC 8 : le restaure
    out += std::format("\x1b[{}A", grow);
    emit(out);
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
    if (input_rows_ > 1) {
        // Retour à une seule ligne de saisie : la zone de défilement
        // s'agrandit et l'historique redescend d'autant (SD).
        const std::size_t shrink = input_rows_ - 1;
        input_rows_ = 1;
        emit(scroll_region(region_bottom()) + std::format("\x1b[{}T", shrink));
    }
    draw_bar(/*marker=*/true);
    if (entered && !text.empty()) {
        // La saisie rejoint l'historique, sans cadre.
        const bool request = prompt == prompt_marker();
        write(std::string(request ? "\n" : "") + std::string(prompt) +
              paint(text, {.bold = request}) + "\n");
    }
}

void Display::clear() {
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    transcript_.clear();
    if (pinned_) {
        // Zone de défilement remise à tout l'écran le temps de l'effacer.
        emit("\x1b[r\x1b[2J\x1b[3J\x1b[H");
        draw_bar(/*marker=*/true);
    } else if (terminal::enabled()) {
        emit("\x1b[2J\x1b[3J\x1b[H");
    }
}

namespace {

/// Cadre avec le pingouin en pixel art (14×14 pixels, soit 14 colonnes sur
/// 7 lignes) et `text` à sa droite, centré verticalement ; sans couleurs, le
/// pingouin en ASCII. `text` : 3 lignes déjà mises en forme.
std::vector<std::string> penguin_card(const std::vector<std::string>& text) {
    // K : corps bleu nuit, W : blanc, O : orange, « . » : transparent.
    static const std::vector<std::string_view> penguin = {
        "....KKKKKK....", "...KKKKKKKK...", "..KKWWKKWWKK..", "..KKWKKKKWKK..", "..KKKKOOKKKK..",
        ".KKKWWOOWWKKK.", "KKKWWWWWWWWKKK", "KKWWWWWWWWWWKK", "KKWWWWWWWWWWKK", ".KWWWWWWWWWWK.",
        ".KKWWWWWWWWKK.", "..KKKWWWWKKK..", "..OOOO..OOOO..", "..............",
    };
    static const std::vector<std::pair<char, terminal::Rgb>> palette = {
        {'K', {.r = 52, .g = 63, .b = 92}},
        {'W', {.r = 240, .g = 240, .b = 235}},
        {'O', {.r = 245, .g = 160, .b = 40}},
    };
    std::vector<std::string> lines;
    const auto art = terminal::pixel_art(penguin, palette);
    if (!art.empty()) {
        const std::size_t first = (art.size() - text.size()) / 2;
        for (std::size_t row = 0; row < art.size(); ++row) {
            std::string line = " " + art[row] + "   ";
            if (row >= first && row - first < text.size()) {
                line += text[row - first];
            }
            lines.push_back(std::move(line));
        }
    } else {
        static const std::vector<std::string_view> ascii = {" (o<   ", " //\\   ", " V_/_  "};
        for (std::size_t row = 0; row < ascii.size(); ++row) {
            lines.push_back(std::string(ascii[row]) + (row < text.size() ? text[row] : ""));
        }
    }
    return lines;
}

/// Largeur laissée au texte d'un cadre de `inner` colonnes : à droite du
/// pingouin (18 colonnes) ou de l'ASCII (7 colonnes).
std::size_t card_text_width(std::size_t inner) {
    const std::size_t art = terminal::enabled() ? 18 : 7;
    return inner > art ? inner - art : 0;
}

} // namespace

void Display::banner(std::string_view llm, std::size_t tools, std::string_view conversation) {
    const std::size_t text_width = card_text_width(width() - 4);
    const std::vector<std::string> text = {
        paint(std::format("Avenoir {}", core::version()), {.bold = true}),
        paint(terminal::ellipsize(std::format("{} · {} outil(s)", llm, tools), text_width), kMuted),
        paint(terminal::ellipsize(std::format("Conversation : {}", conversation), text_width),
              kMuted),
    };
    const auto lines = penguin_card(text);
    const std::scoped_lock lock(mutex_);
    write("\n" + terminal::box({}, lines, Color::White, width()) + "\n");
    write(paint("  /aide pour les commandes · Ctrl+C arrête · Ctrl+D quitte", kMuted) + "\n");
}

void Display::farewell(std::string_view conversation) {
    const std::size_t text_width = card_text_width(width() - 4);
    const std::vector<std::string> text = {
        paint("À bientôt !", {.bold = true}),
        paint(terminal::ellipsize(std::format("Conversation : {}", conversation), text_width),
              kMuted),
        paint(terminal::ellipsize("Elle reprendra au prochain lancement.", text_width), kMuted),
    };
    const auto lines = penguin_card(text);
    const std::scoped_lock lock(mutex_);
    clear_spinner();
    if (pinned_) {
        pinned_ = false;
        emit("\x1b[r"); // zone de défilement rendue à tout l'écran
    }
    if (terminal::enabled()) {
        // Écran et historique du terminal vidés : on quitte sur un terminal propre.
        emit("\x1b[2J\x1b[3J\x1b[H");
    }
    write("\n" + terminal::box({}, lines, Color::White, width()) + "\n\n");
}

void Display::user(std::string_view text) {
    const std::string line = "\n" + paint(kPromptGlyph, {.color = Color::Cyan, .bold = true}) +
                             " " + paint(text, {.bold = true}) + "\n";
    const std::scoped_lock lock(mutex_);
    write(line);
}

std::string Display::render_reply(std::string_view markdown) {
    const std::string bar = paint("│", {.color = Color::White, .dim = true});
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
    const std::string out = "\n" + paint("◆ Avenoir", {.color = Color::White, .bold = true}) +
                            "\n" + render_reply(markdown);
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
        write("\n" + paint("◆ Avenoir", {.color = Color::White, .bold = true}) + "\n");
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
    pending_.insert(pending_.end(), calls.begin(), calls.end());
}

void Display::tool_result(const core::ToolResultMessage& result) {
    const std::scoped_lock lock(mutex_);
    const auto call = std::ranges::find(pending_, result.call_id, &core::ToolCall::id);
    if (call == pending_.end()) {
        return;
    }
    std::string line = "  " + paint(kToolGlyph, {.color = Color::Yellow}) + " " + call->name;
    if (const auto arguments = summarize_arguments(call->arguments); !arguments.empty()) {
        line += " " + paint(terminal::ellipsize(arguments, 40), kMuted);
    }
    if (result.is_error) {
        line += "  " + paint("✗", {.color = Color::Red, .bold = true}) + " " +
                paint(first_line(result.content, 50), {.color = Color::Red});
    } else {
        line += "  " + paint("✓", {.color = Color::Green, .bold = true});
    }
    pending_.erase(call);
    write(line + "\n");
}

void Display::info(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    write(paint(text, kMuted) + "\n");
}

void Display::notify(std::string_view text) {
    const std::scoped_lock lock(mutex_);
    const std::string line = paint(text, kMuted) + "\n";
    if (!pinned_ || !reading_) {
        write(line);
        return;
    }
    // Barre fixe pendant la saisie : écrit en bas de l'historique (sa dernière
    // ligne est toujours vide), puis rend le curseur exactement où il était
    // (ESC 7 / ESC 8), là où replxx l'attend.
    transcript_.emplace_back(line);
    while (transcript_.size() > kTranscriptChunks) {
        transcript_.pop_front();
    }
    emit("\x1b"
         "7" +
         go_to(region_bottom()) + line +
         "\x1b"
         "8");
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
    (void)std::fflush(stdout);
}

void Display::tick() {
    const std::scoped_lock lock(mutex_);
    if (!thinking_ || suspended_) {
        return;
    }
    // Terminal redimensionné pendant la réflexion : barre recalée.
    if (pinned_ && refresh_geometry() && pinned_) {
        draw_bar(/*marker=*/true);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_frame_ < std::chrono::milliseconds(100)) {
        return;
    }
    last_frame_ = now;
    frame_ = (frame_ + 1) % kSpinner.size();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - since_).count();
    const std::string spinner = std::format(
        "  {} {}", paint(kSpinner.at(frame_), {.color = Color::White}),
        paint(std::format("{}… {} s · Ctrl+C pour arrêter", activity_, seconds), kMuted));
    // Barre fixe : la toupie s'affiche avec les réponses, sur la dernière
    // ligne de l'historique ; le curseur revient ensuite dans la barre.
    if (pinned_) {
        emit(go_to(region_bottom()) + "\x1b[2K" + spinner + go_to(prompt_row()) + "\x1b[3G");
    } else {
        emit("\r\x1b[2K" + spinner);
    }
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
    return terminal::enabled() ? paint(kPromptGlyph, {.color = Color::Cyan, .bold = true}) + " "
                               : "> ";
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
