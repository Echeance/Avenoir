#include "avenoir/storage/transcript.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

namespace avenoir::storage {

namespace {

constexpr std::size_t kMaxSlug = 40;

/// « 8 novembre 2026 à 12 h 50 » (« 1er » pour le premier du mois).
std::string french_date(std::chrono::local_time<std::chrono::minutes> time) {
    static constexpr std::array<std::string_view, 12> kMonths = {
        "janvier", "février", "mars",      "avril",   "mai",      "juin",
        "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
    const auto day_start = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{day_start};
    const std::chrono::hh_mm_ss clock{time - day_start};
    const unsigned day = static_cast<unsigned>(date.day());
    return std::format("{}{} {} {} à {} h {:02}", day, day == 1 ? "er" : "",
                       kMonths.at(static_cast<unsigned>(date.month()) - 1),
                       static_cast<int>(date.year()), clock.hours().count(),
                       clock.minutes().count());
}

/// Hors code : HTML neutralisé (balises et entités affichées telles quelles)
/// et images désactivées (« ![…] » devient un simple texte).
std::string neutralize(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '<') {
            out += "&lt;";
        } else if (c == '&') {
            out += "&amp;";
        } else if (c == '!' && i + 1 < text.size() && text[i + 1] == '[') {
            out += "\\!";
        } else {
            out += c;
        }
    }
    return out;
}

/// Une ligne hors bloc de code : les extraits de code (`…`) sont gardés tels
/// quels (rien n'y est interprété), le reste est neutralisé.
std::string safe_line(std::string_view line) {
    std::string out;
    std::size_t i = 0;
    while (i < line.size()) {
        const auto tick = line.find('`', i);
        if (tick == std::string_view::npos) {
            out += neutralize(line.substr(i));
            break;
        }
        out += neutralize(line.substr(i, tick - i));
        std::size_t run = tick;
        while (run < line.size() && line[run] == '`') {
            ++run;
        }
        const std::string_view ticks = line.substr(tick, run - tick);
        // Fermeture : la même suite d'accents graves, ni plus ni moins.
        std::size_t close = line.find(ticks, run);
        while (close != std::string_view::npos && close + ticks.size() < line.size() &&
               line[close + ticks.size()] == '`') {
            close = line.find(ticks, close + ticks.size() + 1);
        }
        if (close == std::string_view::npos) {
            out += ticks; // Accents graves seuls : du texte.
            i = run;
            continue;
        }
        out += line.substr(tick, close + ticks.size() - tick);
        i = close + ticks.size();
    }
    return out;
}

/// Clôture de bloc de code (« ``` » ou « ~~~ », 3 au moins, après au plus
/// 3 espaces) : le caractère et la longueur, ou rien.
std::optional<std::pair<char, std::size_t>> fence_of(std::string_view line) {
    std::size_t start = 0;
    while (start < line.size() && start < 3 && line[start] == ' ') {
        ++start;
    }
    if (start >= line.size() || (line[start] != '`' && line[start] != '~')) {
        return std::nullopt;
    }
    const char c = line[start];
    std::size_t end = start;
    while (end < line.size() && line[end] == c) {
        ++end;
    }
    if (end - start < 3) {
        return std::nullopt;
    }
    return std::pair{c, end - start};
}

/// Texte d'un message, en Markdown sûr : les blocs et extraits de code sont
/// gardés tels quels, le reste est neutralisé ; un bloc resté ouvert est
/// refermé (il avalerait la suite de la transcription). Sans blancs finaux.
std::string safe_markdown(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    std::string out;
    std::optional<std::pair<char, std::size_t>> open;
    while (true) {
        const auto end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        const auto fence = fence_of(line);
        if (open) {
            out += line;
            if (fence && fence->first == open->first && fence->second >= open->second &&
                line.find_first_not_of(" `~") == std::string_view::npos) {
                open.reset();
            }
        } else if (fence) {
            out += line;
            open = fence;
        } else {
            out += safe_line(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        out += '\n';
        text.remove_prefix(end + 1);
    }
    if (open) {
        out += "\n" + std::string(open->second, open->first);
    }
    return out;
}

/// Texte placé dans du HTML (`<summary>`).
std::string escape_html(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '&':
            out += "&amp;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

/// Bloc de code dont la clôture est plus longue que toute suite d'accents
/// graves du texte : le texte ne peut pas en sortir.
std::string code_block(std::string_view text) {
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char c : text) {
        run = c == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const std::string fence(std::max<std::size_t>(3, longest + 1), '`');
    std::string body(text);
    while (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }
    return fence + "text\n" + body + "\n" + fence + "\n";
}

/// Arguments lisibles : « city : Arles · days : 2 » pour un objet de valeurs
/// simples, le JSON compact sinon.
std::string describe_arguments(const nlohmann::json& arguments) {
    if (!arguments.is_object()) {
        return arguments.dump();
    }
    std::string out;
    for (const auto& [key, value] : arguments.items()) {
        if (value.is_structured()) {
            return arguments.dump();
        }
        if (!out.empty()) {
            out += " · ";
        }
        out += key + " : " + (value.is_string() ? value.get<std::string>() : value.dump());
    }
    return out;
}

/// Appel d'outil replié, avec son résultat s'il est connu.
std::string tool_block(const core::ToolCall& call, const core::ToolResultMessage* result) {
    std::string summary = "» <code>" + escape_html(call.name) + "</code>";
    if (const std::string arguments = describe_arguments(call.arguments); !arguments.empty()) {
        summary += " · " + escape_html(arguments);
    }
    if (result == nullptr) {
        summary += " · <i>sans résultat</i>";
    } else {
        summary += result->is_error ? " · ✗" : " · ✓";
    }
    std::string out = "<details>\n<summary>" + summary + "</summary>\n\n";
    if (result != nullptr) {
        out += code_block(result->content) + "\n";
    }
    return out + "</details>\n";
}

char unaccent(unsigned char second) {
    static const std::unordered_map<unsigned char, char> letters = {
        {0x80, 'a'}, {0x81, 'a'}, {0x82, 'a'}, {0x84, 'a'}, {0xA0, 'a'}, {0xA1, 'a'}, {0xA2, 'a'},
        {0xA4, 'a'}, {0x87, 'c'}, {0xA7, 'c'}, {0x88, 'e'}, {0x89, 'e'}, {0x8A, 'e'}, {0x8B, 'e'},
        {0xA8, 'e'}, {0xA9, 'e'}, {0xAA, 'e'}, {0xAB, 'e'}, {0x8E, 'i'}, {0x8F, 'i'}, {0xAE, 'i'},
        {0xAF, 'i'}, {0x94, 'o'}, {0x96, 'o'}, {0xB4, 'o'}, {0xB6, 'o'}, {0x99, 'u'}, {0x9B, 'u'},
        {0x9C, 'u'}, {0xB9, 'u'}, {0xBB, 'u'}, {0xBC, 'u'},
    };
    const auto it = letters.find(second);
    return it == letters.end() ? '\0' : it->second;
}

std::string slug(std::string_view title) {
    std::string out;
    bool dash = false;
    const auto push = [&](char c) {
        if (dash && !out.empty()) {
            out += '-';
        }
        dash = false;
        out += c;
    };
    for (std::size_t i = 0; i < title.size() && out.size() < kMaxSlug; ++i) {
        const auto byte = static_cast<unsigned char>(title[i]);
        if (byte >= 'A' && byte <= 'Z') {
            push(static_cast<char>(byte - 'A' + 'a'));
        } else if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')) {
            push(static_cast<char>(byte));
        } else if (byte == 0xC3 && i + 1 < title.size()) {
            const char letter = unaccent(static_cast<unsigned char>(title[i + 1]));
            ++i;
            if (letter != '\0') {
                push(letter);
            } else {
                dash = true;
            }
        } else if (byte < 0x80 || byte >= 0xC0) {
            // Espace, ponctuation ou début d'un autre caractère UTF-8 : séparateur.
            dash = true;
        }
    }
    return out.empty() ? std::string("conversation") : out;
}

} // namespace

std::string transcript_markdown(std::string_view title, std::int64_t id,
                                std::chrono::local_time<std::chrono::minutes> date,
                                std::span<const core::Message> messages) {
    // Résultats rangés par appel, pour les afficher avec l'appel qui les a produits.
    std::unordered_map<std::string, const core::ToolResultMessage*> results;
    std::unordered_set<std::string> calls;
    std::size_t exchanges = 0;
    for (const auto& message : messages) {
        if (const auto* result = std::get_if<core::ToolResultMessage>(&message)) {
            results.emplace(result->call_id, result);
        } else if (const auto* answer = std::get_if<core::AssistantMessage>(&message)) {
            for (const auto& call : answer->tool_calls) {
                calls.insert(call.id);
            }
        } else {
            ++exchanges;
        }
    }

    std::string out = std::format("# {}\n", safe_markdown(title));
    // Chaque bloc est séparé du précédent par une seule ligne vide.
    const auto add = [&out](std::string_view block) {
        out += '\n';
        out += block;
        if (!block.ends_with('\n')) {
            out += '\n';
        }
    };
    add(std::format("*Avenoir · conversation n° {} · {} · {} échange{}*", id, french_date(date),
                    exchanges, exchanges > 1 ? "s" : ""));

    bool in_answer = false; // Sous « ◆ Avenoir » : les messages suivants s'y ajoutent.
    for (const auto& message : messages) {
        std::visit(
            [&](const auto& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, core::UserMessage>) {
                    add("## › Vous");
                    add(safe_markdown(m.content));
                    in_answer = false;
                } else {
                    if (!in_answer) {
                        add("## ◆ Avenoir");
                        in_answer = true;
                    }
                }
                if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                    if (const std::string text = safe_markdown(m.text); !text.empty()) {
                        add(text);
                    }
                    for (const auto& call : m.tool_calls) {
                        const auto it = results.find(call.id);
                        add(tool_block(call, it == results.end() ? nullptr : it->second));
                    }
                } else if constexpr (std::is_same_v<T, core::ToolResultMessage>) {
                    if (calls.contains(m.call_id)) {
                        return;
                    }
                    add(code_block(m.content)); // Résultat sans appel connu : seul.
                }
            },
            message);
    }
    return out;
}

std::string transcript_file_name(std::chrono::local_time<std::chrono::minutes> date,
                                 std::int64_t id, std::string_view title) {
    return std::format("{:%Y-%m-%d-%H%M}-n{}-{}.md", date, id, slug(title));
}

} // namespace avenoir::storage
