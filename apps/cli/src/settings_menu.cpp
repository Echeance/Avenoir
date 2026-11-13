#include "settings_menu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avenoir/terminal/style.hpp"

#include "display.hpp"
#include "prompt.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::paint;
using terminal::Style;

constexpr Style kTitle{.color = Color::Azure, .bold = true};
constexpr Style kLabel{.color = Color::Sky};
constexpr Style kValue{.color = Color::Ice, .bold = true};
constexpr Style kMuted{.color = Color::Gray};
constexpr Style kFrame{.color = Color::Navy};
constexpr Style kWarn{.color = Color::Yellow};

/// Texte pour la recherche : minuscules, sans accents (« Équipe » → « equipe »).
std::string fold(std::string_view text) {
    static constexpr std::array<std::pair<std::string_view, char>, 30> kAccents{{
        {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"À", 'a'}, {"Â", 'a'}, {"Ä", 'a'},
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"É", 'e'}, {"È", 'e'},
        {"Ê", 'e'}, {"Ë", 'e'}, {"î", 'i'}, {"ï", 'i'}, {"Î", 'i'}, {"Ï", 'i'},
        {"ô", 'o'}, {"ö", 'o'}, {"Ô", 'o'}, {"Ö", 'o'}, {"ù", 'u'}, {"û", 'u'},
        {"ü", 'u'}, {"Ù", 'u'}, {"Û", 'u'}, {"Ü", 'u'}, {"ç", 'c'}, {"Ç", 'c'},
    }};
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        bool replaced = false;
        for (const auto& [accent, plain] : kAccents) {
            if (text.substr(i).starts_with(accent)) {
                out += plain;
                i += accent.size();
                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        ++i;
    }
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string rule(std::size_t width) {
    std::string line;
    for (std::size_t i = 0; i + 1 < width; ++i) {
        line += "─";
    }
    return paint(line, kFrame);
}

} // namespace

std::vector<SettingEntry> settings_from_json(const nlohmann::json& list) {
    std::vector<SettingEntry> entries;
    if (!list.is_array()) {
        return entries;
    }
    for (const auto& item : list) {
        if (!item.is_object() || !item.contains("key") || !item["key"].is_string()) {
            continue;
        }
        entries.push_back({.key = item["key"].get<std::string>(),
                           .value = item.value("value", ""),
                           .accepted = item.value("accepted", ""),
                           .description = item.value("description", ""),
                           .security = item.value("security", false)});
    }
    return entries;
}

std::string setting_family(std::string_view key) {
    const std::string_view prefix = key.substr(0, key.find('.'));
    if (prefix == "agent") {
        return "Assistant";
    }
    if (prefix == "permissions") {
        return "Permissions";
    }
    if (prefix == "web") {
        return "Recherche web";
    }
    if (prefix == "orchestration") {
        return "Équipe d'agents";
    }
    if (prefix == "modules") {
        return "Modules";
    }
    return std::string(prefix);
}

std::vector<std::string> setting_choices(std::string_view accepted) {
    std::vector<std::string> choices;
    std::size_t start = 0;
    while (start <= accepted.size()) {
        const auto bar = accepted.find(" | ", start);
        const std::string_view piece = trim(accepted.substr(
            start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
        start = bar == std::string_view::npos ? accepted.size() + 1 : bar + 3;
        // Une valeur à taper (nombre, adresse) : pas de liste.
        if (piece.empty() || piece.contains("…") || piece.contains(" à ")) {
            return {};
        }
        choices.emplace_back(piece);
    }
    return choices.size() >= 2 ? choices : std::vector<std::string>{};
}

std::vector<std::size_t> filter_settings(const std::vector<SettingEntry>& entries,
                                         std::string_view filter) {
    const std::string needle = fold(trim(filter));
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (needle.empty() || fold(entry.key).contains(needle) ||
            fold(entry.description).contains(needle) ||
            fold(setting_family(entry.key)).contains(needle)) {
            found.push_back(i);
        }
    }
    return found;
}

std::vector<std::string> render_menu(const MenuView& view, std::size_t height, std::size_t width) {
    std::vector<std::string> lines;
    lines.push_back(" " + paint("◆ " + view.title, kTitle));
    lines.push_back(rule(width));

    // Bas du panneau : détails, résultat, touches.
    std::vector<std::string> bottom;
    bottom.push_back(rule(width));
    for (const auto& line : view.details) {
        bottom.push_back(" " + line);
    }
    if (!view.status.empty()) {
        bottom.push_back(" " + view.status);
    }
    bottom.push_back(" " + paint(view.keys, kMuted));

    // Liste, avec l'en-tête de chaque famille.
    std::vector<std::string> rows;
    std::size_t selected_row = 0;
    std::string family;
    for (std::size_t i = 0; i < view.items.size(); ++i) {
        if (i < view.families.size() && view.families[i] != family) {
            family = view.families[i];
            rows.push_back(" " + paint(family, kTitle));
        }
        if (i == view.selected) {
            selected_row = rows.size();
            rows.push_back(paint(" › ", kTitle) + view.items[i]);
        } else {
            rows.push_back("   " + view.items[i]);
        }
    }
    if (view.items.empty()) {
        rows.push_back("   " + paint("Aucun réglage ne correspond.", kMuted));
    }
    const std::size_t room =
        height > lines.size() + bottom.size() ? height - lines.size() - bottom.size() : 1;
    // Fenêtre de la liste : la sélection reste visible, vers le milieu.
    std::size_t first = 0;
    if (rows.size() > room && selected_row >= room / 2) {
        first = std::min(selected_row - (room / 2), rows.size() - room);
    }
    for (std::size_t i = first; i < rows.size() && i < first + room; ++i) {
        lines.push_back(rows[i]);
    }
    while (lines.size() + bottom.size() < height) {
        lines.emplace_back();
    }
    for (auto& line : bottom) {
        lines.push_back(std::move(line));
    }
    lines.resize(std::min(lines.size(), height));
    return lines;
}

namespace {

/// Ligne d'un réglage dans la liste.
std::string item_line(const SettingEntry& entry, std::size_t width) {
    const std::string mark = entry.security ? paint("▲", kWarn) : " ";
    const std::size_t key_width = 32;
    const std::size_t value_width = 14;
    std::string line =
        mark + " " + terminal::pad(paint(entry.key, kLabel), key_width) +
        terminal::pad(paint(terminal::ellipsize(entry.value, value_width - 2), kValue),
                      value_width);
    const std::size_t used = 4 + 2 + key_width + value_width;
    if (width > used + 10) {
        line += paint(terminal::ellipsize(entry.description, width - used - 2), kMuted);
    }
    return line;
}

/// Détails du réglage sélectionné, sous la liste.
std::vector<std::string> details_of(const SettingEntry& entry, std::size_t width) {
    std::vector<std::string> details;
    details.push_back(paint(entry.key, kTitle) + paint("  valeur actuelle : ", kMuted) +
                      paint(entry.value, kValue));
    for (const auto& piece : terminal::wrap(entry.description, width > 8 ? width - 4 : 4)) {
        details.push_back(piece);
    }
    details.push_back(paint("Valeurs acceptées : ", kMuted) + entry.accepted);
    if (entry.security) {
        details.push_back(paint("▲ Réglage de sécurité : l'affaiblir demande de taper « Je sais "
                                "ce que je fais ».",
                                kWarn));
    }
    return details;
}

void show(const MenuView& view) {
    const auto [height, width] = display().panel_size();
    display().show_panel(render_menu(view, height, width));
}

/// Nouvelle valeur d'un réglage : dans une liste, ou tapée. Rien si
/// l'utilisateur renonce.
std::optional<std::string> choose_value(Prompt& prompt, const SettingEntry& entry) {
    const auto [height, width] = display().panel_size();
    (void)height;
    MenuView view{.title = entry.key,
                  .items = {},
                  .families = {},
                  .selected = 0,
                  .details = details_of(entry, width),
                  .status = {},
                  .keys = {}};
    const auto choices = setting_choices(entry.accepted);
    if (!choices.empty()) {
        for (std::size_t i = 0; i < choices.size(); ++i) {
            view.items.push_back(choices[i] == entry.value
                                     ? paint(choices[i], kValue) + paint("  (actuelle)", kMuted)
                                     : choices[i]);
            if (choices[i] == entry.value) {
                view.selected = i;
            }
        }
        view.keys = "↑↓ choisir · Entrée valider · Ctrl+C retour";
        show(view);
        const Prompt::Menu menu{
            .move =
                [&view, &choices](int delta) {
                    const auto last = static_cast<long>(choices.size()) - 1;
                    view.selected = static_cast<std::size_t>(
                        std::clamp(static_cast<long>(view.selected) + delta, 0L, last));
                    show(view);
                },
            .filter = [](const std::string& /*filter*/) {},
        };
        const auto input = prompt.read_menu("  Valeur › ", menu);
        if (input.status != Prompt::Status::Line) {
            return std::nullopt;
        }
        return choices[view.selected];
    }
    // Valeur à taper : la saisie commence par la valeur actuelle.
    view.keys = "Entrée valider · Ctrl+C retour";
    show(view);
    prompt.type_ahead(entry.value == "aucune" ? std::string{} : entry.value);
    const auto input = prompt.read("  Nouvelle valeur › ", /*remember=*/false);
    if (input.status != Prompt::Status::Line) {
        return std::nullopt;
    }
    std::string value(trim(input.text));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

void run_settings_menu(Prompt& prompt, const SettingsBackend& backend) {
    auto entries = backend.fetch();
    if (!entries) {
        return;
    }
    if (!display().full_screen()) {
        display().info("Le menu des réglages demande un terminal plein écran : voir /reglages "
                       "et /reglage <clé> <valeur>.");
        return;
    }
    std::string status;
    while (true) {
        std::string filter;
        std::vector<std::size_t> visible = filter_settings(*entries, filter);
        std::size_t selected = 0;
        const auto draw = [&] {
            const auto [height, width] = display().panel_size();
            (void)height;
            MenuView view{.title = std::format("Réglages d'Avenoir  ·  {} réglage{}",
                                               visible.size(), visible.size() > 1 ? "s" : ""),
                          .items = {},
                          .families = {},
                          .selected = selected,
                          .details = {},
                          .status = status,
                          .keys = "↑↓ choisir · Entrée modifier · tapez pour filtrer · Ctrl+C "
                                  "fermer"};
            for (const auto index : visible) {
                const auto& entry = (*entries)[index];
                view.items.push_back(item_line(entry, width));
                view.families.push_back(setting_family(entry.key));
            }
            if (!visible.empty()) {
                view.details = details_of((*entries)[visible[selected]], width);
            }
            show(view);
        };
        draw();
        const Prompt::Menu menu{
            .move =
                [&](int delta) {
                    if (visible.empty()) {
                        return;
                    }
                    const auto last = static_cast<long>(visible.size()) - 1;
                    selected = static_cast<std::size_t>(
                        std::clamp(static_cast<long>(selected) + delta, 0L, last));
                    draw();
                },
            .filter =
                [&](const std::string& text) {
                    filter = text;
                    visible = filter_settings(*entries, filter);
                    selected = 0;
                    draw();
                },
        };
        const auto input = prompt.read_menu("  Filtrer › ", menu);
        if (input.status != Prompt::Status::Line) {
            break; // Ctrl+C, Ctrl+D : menu fermé
        }
        if (visible.empty()) {
            continue;
        }
        const SettingEntry entry = (*entries)[visible[selected]];
        const auto value = choose_value(prompt, entry);
        if (!value || *value == entry.value) {
            status = paint("Rien n'a changé.", kMuted);
            continue;
        }
        // Le changement passe par le démon (/reglage) : sceau, accord pour
        // un affaiblissement, audit. Ses messages vont dans la conversation.
        display().hide_panel();
        backend.apply(entry.key, *value);
        auto refreshed = backend.fetch();
        if (!refreshed) {
            break;
        }
        entries = std::move(refreshed);
        const auto now = std::ranges::find(*entries, entry.key, &SettingEntry::key);
        if (now != entries->end() && now->value != entry.value) {
            status = paint("✓ ", {.color = Color::Green, .bold = true}) +
                     std::format("{} : {} → {}", entry.key, entry.value, now->value) +
                     paint("  · /redemarrer pour l'appliquer", kMuted);
        } else {
            status = paint("Rien n'a changé (voir la conversation).", kWarn);
        }
    }
    display().hide_panel();
}

} // namespace avenoir::cli
