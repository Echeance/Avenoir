#include "app_index.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform.hpp"

namespace avenoir::tools {

namespace detail {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

/// « a;b;c; » → { « a », « b », « c » } (listes des .desktop).
std::vector<std::string> split_list(std::string_view value) {
    std::vector<std::string> out;
    while (!value.empty()) {
        const auto end = value.find(';');
        const auto item = trim(value.substr(0, end));
        if (!item.empty()) {
            out.emplace_back(item);
        }
        if (end == std::string_view::npos) {
            break;
        }
        value.remove_prefix(end + 1);
    }
    return out;
}

/// Nom de l'exécutable d'une ligne `Exec` (« /usr/bin/firefox %u » → « firefox »).
std::string exec_name(std::string_view exec) {
    exec = trim(exec);
    if (exec.starts_with('"')) {
        exec.remove_prefix(1);
        exec = exec.substr(0, exec.find('"'));
    } else {
        exec = exec.substr(0, exec.find(' '));
    }
    if (const auto slash = exec.rfind('/'); slash != std::string_view::npos) {
        exec.remove_prefix(slash + 1);
    }
    return std::string(exec);
}

/// Mots d'un texte normalisé par mot (« Éditeur de texte » → éditeur, de, texte).
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const bool separator = i == text.size() || text[i] == ' ' || text[i] == '-' ||
                               text[i] == '_' || text[i] == '.' || text[i] == '\'';
        if (!separator) {
            current += text[i];
            continue;
        }
        if (auto word = normalize_app_name(current); !word.empty()) {
            out.push_back(std::move(word));
        }
        current.clear();
    }
    return out;
}

} // namespace

std::optional<AppEntry> parse_desktop_entry(std::string_view text, std::string id,
                                            const std::vector<std::string>& languages) {
    // Valeurs de [Desktop Entry] : clé (avec « [langue] » éventuel) → valeur.
    std::vector<std::pair<std::string, std::string>> values;
    bool in_entry = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (line.starts_with('[')) {
            in_entry = line == "[Desktop Entry]";
            continue;
        }
        const auto equal = line.find('=');
        if (!in_entry || equal == std::string_view::npos) {
            continue;
        }
        values.emplace_back(std::string(trim(line.substr(0, equal))),
                            std::string(trim(line.substr(equal + 1))));
    }
    const auto plain = [&values](std::string_view key) -> std::optional<std::string> {
        for (const auto& [k, v] : values) {
            if (k == key) {
                return v;
            }
        }
        return std::nullopt;
    };
    // Clé localisée : « Name[fr_FR] », puis « Name[fr] », puis « Name ».
    const auto localized = [&](std::string_view key) -> std::optional<std::string> {
        for (const auto& language : languages) {
            if (auto value = plain(std::string(key) + "[" + language + "]")) {
                return value;
            }
        }
        return plain(key);
    };
    if (plain("Type") != "Application" || plain("NoDisplay") == "true" ||
        plain("Hidden") == "true") {
        return std::nullopt;
    }
    auto name = localized("Name");
    if (!name || name->empty()) {
        return std::nullopt;
    }
    AppEntry entry{.id = std::move(id), .name = std::move(*name), .aliases = {}};
    if (auto generic = localized("GenericName"); generic && !generic->empty()) {
        entry.aliases.push_back(std::move(*generic));
    }
    if (auto english = plain("Name"); english && *english != entry.name) {
        entry.aliases.push_back(std::move(*english));
    }
    if (auto keywords = localized("Keywords")) {
        std::ranges::move(split_list(*keywords), std::back_inserter(entry.aliases));
    }
    // Avenoir parle français : les traductions françaises servent aussi
    // d'alias, même si le système est dans une autre langue
    // (« navigateur » trouve « Web Browser »).
    for (const char* language : {"fr_FR", "fr"}) {
        for (const char* key : {"Name", "GenericName"}) {
            if (auto value = plain(std::string(key) + "[" + language + "]");
                value && !value->empty() && *value != entry.name &&
                !std::ranges::contains(entry.aliases, *value)) {
                entry.aliases.push_back(std::move(*value));
            }
        }
        if (auto keywords = plain(std::string("Keywords[") + language + "]")) {
            for (auto& keyword : split_list(*keywords)) {
                if (!std::ranges::contains(entry.aliases, keyword)) {
                    entry.aliases.push_back(std::move(keyword));
                }
            }
        }
    }
    if (auto exec = plain("Exec")) {
        if (auto program = exec_name(*exec); !program.empty()) {
            entry.aliases.push_back(std::move(program));
        }
    }
    return entry;
}

std::vector<std::string> desktop_languages(std::string_view locale) {
    locale = locale.substr(0, locale.find_first_of(".@"));
    if (locale.empty() || locale == "C" || locale == "POSIX") {
        return {};
    }
    std::vector<std::string> languages{std::string(locale)};
    if (const auto underscore = locale.find('_'); underscore != std::string_view::npos) {
        languages.emplace_back(locale.substr(0, underscore));
    }
    return languages;
}

std::string normalize_app_name(std::string_view text) {
    // Lettres accentuées courantes (UTF-8) : sans leur accent.
    static constexpr std::array<std::pair<std::string_view, char>, 24> kAccents{{
        {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"É", 'e'}, {"È", 'e'},
        {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"À", 'a'}, {"î", 'i'}, {"ï", 'i'},
        {"ô", 'o'}, {"ö", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ç", 'c'},
        {"Ç", 'c'}, {"œ", 'o'}, {"ñ", 'n'}, {"Ê", 'e'}, {"Ô", 'o'}, {"Î", 'i'},
    }};
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        bool replaced = false;
        for (const auto& [accent, letter] : kAccents) {
            if (text.substr(i).starts_with(accent)) {
                out += letter;
                i += accent.size();
                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }
        const char c = text[i++];
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   (static_cast<unsigned char>(c) & 0x80U) != 0) {
            out += c; // autres lettres non latines gardées telles quelles
        }
    }
    return out;
}

int match_score(const AppEntry& entry, std::string_view query) {
    const std::string wanted = normalize_app_name(query);
    if (wanted.empty()) {
        return 0;
    }
    const std::string name = normalize_app_name(entry.name);
    if (name == wanted) {
        return 100;
    }
    int best = 0;
    for (const auto& alias : entry.aliases) {
        if (normalize_app_name(alias) == wanted) {
            best = std::max(best, 90);
        }
    }
    if (normalize_app_name(entry.id) == wanted) {
        best = std::max(best, 90);
    }
    if (name.starts_with(wanted) && wanted.size() >= 3) {
        best = std::max(best, 70);
    }
    // Tous les mots de la demande dans le nom (« studio code »).
    const auto query_words = words(query);
    const auto contains_all = [&query_words](std::string_view text) {
        const auto text_words = words(text);
        return !query_words.empty() && std::ranges::all_of(query_words, [&](const auto& w) {
            return std::ranges::any_of(text_words,
                                       [&w](const auto& t) { return t.starts_with(w); });
        });
    };
    if (contains_all(entry.name)) {
        best = std::max(best, 60);
    }
    for (const auto& alias : entry.aliases) {
        if (contains_all(alias)) {
            best = std::max(best, 40);
        }
    }
    return best;
}

std::vector<AppEntry> search_apps(const std::vector<AppEntry>& apps, std::string_view query,
                                  std::size_t limit) {
    std::vector<std::pair<int, const AppEntry*>> scored;
    for (const auto& app : apps) {
        if (const int score = match_score(app, query); score > 0) {
            scored.emplace_back(score, &app);
        }
    }
    std::ranges::stable_sort(scored, [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second->name < b.second->name;
    });
    std::vector<AppEntry> out;
    for (const auto& [score, app] : scored) {
        if (out.size() >= limit) {
            break;
        }
        out.push_back(*app);
    }
    return out;
}

AppMatch pick_app(const std::vector<AppEntry>& apps, std::string_view query) {
    std::vector<std::pair<int, const AppEntry*>> scored;
    for (const auto& app : apps) {
        if (const int score = match_score(app, query); score > 0) {
            scored.emplace_back(score, &app);
        }
    }
    std::ranges::stable_sort(scored,
                             [](const auto& a, const auto& b) { return a.first > b.first; });
    AppMatch match;
    if (scored.empty()) {
        return match;
    }
    const int top = scored.front().first;
    const auto ties =
        std::ranges::count_if(scored, [top](const auto& s) { return s.first == top; });
    // Une seule réponse claire : nom exact, ou meilleure sans ex æquo assez sûre.
    if (ties == 1 && top >= 60) {
        match.best = *scored.front().second;
        return match;
    }
    if (ties > 1 && top == 100) {
        match.best = *scored.front().second; // même nom affiché : la première suffit
        return match;
    }
    for (const auto& [score, app] : scored) {
        if (match.candidates.size() >= 8) {
            break;
        }
        match.candidates.push_back(*app);
    }
    return match;
}

} // namespace detail

std::vector<AppEntry> AppIndex::apps(bool refresh) {
    static std::mutex mutex;
    static std::optional<std::vector<AppEntry>> cache;
    const std::scoped_lock lock(mutex);
    if (!cache || refresh) {
        cache = platform::installed_apps();
    }
    return *cache;
}

} // namespace avenoir::tools
