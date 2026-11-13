#include "avenoir/terminal/markdown.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/terminal/style.hpp"

namespace avenoir::terminal {

namespace {

constexpr Style kCode{.color = Color::Cyan};
constexpr Style kMuted{.color = Color::Gray};

/// Fin du délimiteur fermant `marker` à partir de `from`, ou npos.
std::size_t find_closing(std::string_view text, std::size_t from, std::string_view marker) {
    const std::size_t end = text.find(marker, from);
    return end == from ? std::string_view::npos : end; // contenu vide refusé
}

/// `style` complété par `base` : effets cumulés, couleur propre d'abord.
Style combined(Style style, Style base) {
    return {.color = style.color != Color::Default ? style.color : base.color,
            .bold = style.bold || base.bold,
            .dim = style.dim || base.dim,
            .italic = style.italic || base.italic,
            .underline = style.underline || base.underline};
}

/// Éléments en ligne : gras, italique, code, liens, imbricables (« [Lire sur
/// *LeMagIT*](…) », « **gras *italique*** »). Un seul parcours : chaque
/// style ouvert retient où il se ferme ; chaque morceau est peint avec tous
/// les styles en cours et `base` (titre, citation), si bien qu'un style
/// intérieur ne coupe pas celui qui l'entoure.
std::string render_inline(std::string_view text, Style base = {}) {
    constexpr auto kNone = std::string_view::npos;
    std::string out;
    std::string plain; // texte en attente, peint d'un seul trait
    std::size_t bold_end = kNone;
    std::size_t italic_end = kNone;
    std::size_t link_end = kNone; // position du « ](»
    std::size_t url_end = kNone;  // position de la « ) »
    const auto current = [&] {
        return combined({.bold = bold_end != kNone,
                         .italic = italic_end != kNone,
                         .underline = link_end != kNone},
                        base);
    };
    const auto flush = [&] {
        out += paint(plain, current());
        plain.clear();
    };
    std::size_t i = 0;
    while (i < text.size()) {
        const std::string_view rest = text.substr(i);

        // Fermetures des styles ouverts.
        if (i == bold_end) {
            flush();
            bold_end = kNone;
            i += 2;
            continue;
        }
        if (i == italic_end) {
            flush();
            italic_end = kNone;
            i += 1;
            continue;
        }
        if (i == link_end) {
            flush();
            link_end = kNone;
            out += paint(" ", base);
            out += paint("(" + std::string(text.substr(i + 2, url_end - i - 2)) + ")",
                         combined(kMuted, base));
            i = url_end + 1;
            continue;
        }

        // Ouvertures.
        if (rest.starts_with('`')) {
            if (const auto end = find_closing(text, i + 1, "`"); end != kNone) {
                flush();
                out += paint(text.substr(i + 1, end - i - 1), combined(kCode, current()));
                i = end + 1;
                continue;
            }
        }
        if (bold_end == kNone && rest.starts_with("**")) {
            if (auto end = find_closing(text, i + 2, "**"); end != kNone) {
                // « *** » : le gras se ferme sur les deux dernières étoiles.
                while (end + 2 < text.size() && text[end + 2] == '*') {
                    ++end;
                }
                flush();
                bold_end = end;
                i += 2;
                continue;
            }
        }
        // Italique : « *mot* » seulement (pas « 2 * 3 » ni les puces).
        if (italic_end == kNone && rest.starts_with('*') && rest.size() > 1 && rest[1] != ' ' &&
            rest[1] != '*') {
            if (const auto end = find_closing(text, i + 1, "*");
                end != kNone && text[end - 1] != ' ' && end != bold_end) {
                flush();
                italic_end = end;
                i += 1;
                continue;
            }
        }
        if (link_end == kNone && rest.starts_with('[')) {
            const auto close = text.find("](", i);
            const auto paren = close == kNone ? close : text.find(')', close);
            if (close != kNone && paren != kNone) {
                flush();
                link_end = close;
                url_end = paren;
                i += 1;
                continue;
            }
        }
        plain += text[i];
        ++i;
    }
    flush();
    return out;
}

std::string_view trim_left(std::string_view line, std::size_t& indent) {
    indent = 0;
    while (indent < line.size() && line[indent] == ' ') {
        ++indent;
    }
    return line.substr(indent);
}

bool is_ordered_item(std::string_view text, std::size_t& marker_length) {
    std::size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        ++digits;
    }
    if (digits == 0 || digits + 1 >= text.size() || text[digits] != '.' ||
        text[digits + 1] != ' ') {
        return false;
    }
    marker_length = digits + 2;
    return true;
}

} // namespace

std::vector<std::string> render_markdown(std::string_view text) {
    std::vector<std::string> lines;
    bool in_code = false;

    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        std::size_t indent = 0;
        const std::string_view content = trim_left(line, indent);
        const std::string margin(indent, ' ');

        if (content.starts_with("```")) {
            in_code = !in_code; // La ligne de délimitation n'est pas affichée.
            continue;
        }
        if (in_code) {
            lines.push_back("  " + paint(line, kCode));
            continue;
        }
        if (content == "---" || content == "***" || content == "___") {
            lines.push_back(paint("────────────────────", kMuted));
            continue;
        }
        if (content.starts_with('#')) {
            std::size_t level = 0;
            while (level < content.size() && content[level] == '#') {
                ++level;
            }
            if (level < content.size() && content[level] == ' ') {
                lines.push_back(render_inline(content.substr(level + 1),
                                              {.color = Color::White, .bold = true}));
                continue;
            }
        }
        if (content.starts_with("- ") || content.starts_with("* ") || content.starts_with("+ ")) {
            lines.push_back(margin + paint("•", {.color = Color::White}) + " " +
                            render_inline(content.substr(2)));
            continue;
        }
        if (std::size_t marker = 0; is_ordered_item(content, marker)) {
            lines.push_back(margin + paint(content.substr(0, marker - 1), {.color = Color::White}) +
                            " " + render_inline(content.substr(marker)));
            continue;
        }
        if (content.starts_with("> ") || content == ">") {
            lines.push_back(
                margin + paint("▌", kMuted) + " " +
                render_inline(content.substr(content.size() > 1 ? 2 : 1), {.italic = true}));
            continue;
        }
        lines.push_back(margin + render_inline(content));
    }

    // Pas de ligne vide finale due au « \n » de fin.
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

} // namespace avenoir::terminal
