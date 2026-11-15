#include "avenoir/modules/tabs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace avenoir::modules {

namespace {

using nlohmann::json;

/// Texte affichable : contrôles C0 et C1, marques bidirectionnelles et
/// caractères invalides retirés, coupé à `max` octets sans couper un caractère.
std::string clean(std::string_view text, std::size_t max = kMaxTabText) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size() && out.size() < max) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t length = 1;
        if (lead >= 0xF0U) {
            length = 4;
        } else if (lead >= 0xE0U) {
            length = 3;
        } else if (lead >= 0xC0U) {
            length = 2;
        }
        if (at + length > text.size() || (lead >= 0x80U && lead < 0xC0U)) {
            ++at; // octet isolé : écarté
            continue;
        }
        const std::string_view character = text.substr(at, length);
        at += length;
        if (length == 1 && (lead < 0x20U || lead == 0x7FU) && lead != '\n') {
            continue;
        }
        // C1 (U+0080 à U+009F) et marques bidirectionnelles (U+200E, U+200F,
        // U+202A à U+202E, U+2066 à U+2069).
        if (length == 2 && lead == 0xC2U && static_cast<unsigned char>(character[1]) < 0xA0U) {
            continue;
        }
        if (length == 3 && lead == 0xE2U) {
            const auto second = static_cast<unsigned char>(character[1]);
            const auto third = static_cast<unsigned char>(character[2]);
            const bool marks = second == 0x80U && (third == 0x8EU || third == 0x8FU ||
                                                   (third >= 0xAAU && third <= 0xAEU));
            const bool isolates = second == 0x81U && third >= 0xA6U && third <= 0xA9U;
            if (marks || isolates) {
                continue;
            }
        }
        if (out.size() + length > max) {
            break;
        }
        out += character;
    }
    return out;
}

std::optional<std::string> text_field(const json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string()) {
        return std::nullopt;
    }
    std::string text = clean(object[key].get<std::string>());
    return text.empty() ? std::nullopt : std::optional{std::move(text)};
}

/// Liste de textes, ou `null` si `value` n'est pas une liste.
json strings(const json& value, std::size_t max) {
    if (!value.is_array()) {
        return nullptr;
    }
    json out = json::array();
    for (const auto& item : value) {
        if (out.size() >= max) {
            break;
        }
        if (item.is_string()) {
            out.push_back(clean(item.get<std::string>()));
        } else if (item.is_number()) {
            out.push_back(item.dump());
        } else {
            out.push_back("");
        }
    }
    return out;
}

bool own_command(const Manifest& manifest, std::string_view command) {
    return std::ranges::any_of(manifest.commands, [command](const CommandDeclaration& declared) {
        return declared.name == command;
    });
}

/// Image en adresse `data:` base64 d'un format matriciel connu (pas de SVG :
/// rien d'actif), bornée ; `null` sinon.
json image(const json& item) {
    if (!item.contains("src") || !item["src"].is_string()) {
        return nullptr;
    }
    const auto& src = item["src"].get_ref<const std::string&>();
    if (src.size() > kMaxTabImage) {
        return nullptr;
    }
    constexpr std::array<std::string_view, 4> kPrefixes{
        "data:image/png;base64,", "data:image/jpeg;base64,", "data:image/gif;base64,",
        "data:image/webp;base64,"};
    std::size_t prefix = 0;
    for (const std::string_view known : kPrefixes) {
        if (src.starts_with(known)) {
            prefix = known.size();
        }
    }
    if (prefix == 0 || src.size() == prefix) {
        return nullptr;
    }
    const bool base64 = std::ranges::all_of(std::string_view(src).substr(prefix), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '+' || c == '/' || c == '=';
    });
    if (!base64) {
        return nullptr;
    }
    json out{{"type", "image"}, {"src", src}};
    if (auto alt = text_field(item, "alt")) {
        out["alt"] = *alt;
    }
    return out;
}

/// Graphique en barres ou en courbes : une valeur par étiquette et par série
/// (manquante ou non numérique : `null`).
json chart(const json& item) {
    const std::string kind = item.value("kind", "bar");
    auto labels = strings(item.value("labels", json()), kMaxTabRows);
    const json series = item.value("series", json());
    if ((kind != "bar" && kind != "line") || !labels.is_array() || labels.empty() ||
        !series.is_array()) {
        return nullptr;
    }
    json out_series = json::array();
    for (const auto& one : series) {
        if (out_series.size() >= kMaxChartSeries) {
            break;
        }
        if (!one.is_object() || !one.contains("values") || !one["values"].is_array()) {
            continue;
        }
        json values = json::array();
        for (std::size_t at = 0; at < labels.size(); ++at) {
            const json& value = at < one["values"].size() ? one["values"][at] : json();
            values.push_back(value.is_number() ? json(value.get<double>()) : json());
        }
        out_series.push_back(json{{"name", one.contains("name") && one["name"].is_string()
                                               ? clean(one["name"].get<std::string>(), 100)
                                               : std::string{}},
                                  {"values", std::move(values)}});
    }
    if (out_series.empty()) {
        return nullptr;
    }
    json out{{"type", "chart"},
             {"kind", kind},
             {"labels", std::move(labels)},
             {"series", std::move(out_series)}};
    if (auto title = text_field(item, "title")) {
        out["title"] = *title;
    }
    if (item.contains("unit") && item["unit"].is_string()) {
        out["unit"] = clean(item["unit"].get<std::string>(), 20);
    }
    return out;
}

/// Formulaire d'un champ qui remplit l'argument d'une commande du module.
json form(const json& item, const Manifest& manifest) {
    const std::string command = item.value("command", "");
    auto label = text_field(item, "label");
    if (!label || !own_command(manifest, command)) {
        return nullptr;
    }
    json out{{"type", "form"},
             {"command", command},
             {"label", *label},
             {"submit", text_field(item, "submit").value_or("Envoyer")}};
    if (auto placeholder = text_field(item, "placeholder")) {
        out["placeholder"] = *placeholder;
    }
    if (auto options = strings(item.value("options", json()), kMaxFormOptions);
        options.is_array() && !options.empty()) {
        for (auto& option : options) {
            option = clean(option.get<std::string>(), 100);
        }
        out["options"] = std::move(options);
    }
    return out;
}

/// Composant validé, ou `null` s'il est écarté.
json component(const json& item, const Manifest& manifest) {
    if (!item.is_object() || !item.contains("type") || !item["type"].is_string()) {
        return nullptr;
    }
    const std::string type = item["type"].get<std::string>();
    if (type == "heading") {
        if (auto text = text_field(item, "text")) {
            return json{{"type", type}, {"text", *text}};
        }
    } else if (type == "text") {
        if (auto text = text_field(item, "text")) {
            json out{{"type", type}, {"text", *text}};
            const std::string tone = item.value("tone", "");
            if (tone == "muted" || tone == "warning") {
                out["tone"] = tone;
            }
            return out;
        }
    } else if (type == "stat") {
        auto label = text_field(item, "label");
        auto value = text_field(item, "value");
        if (label && value) {
            return json{{"type", type}, {"label", *label}, {"value", *value}};
        }
    } else if (type == "list") {
        if (auto items = strings(item.value("items", json()), kMaxTabRows); !items.is_null()) {
            return json{{"type", type}, {"items", std::move(items)}};
        }
    } else if (type == "table") {
        auto columns = strings(item.value("columns", json()), kMaxTabColumns);
        const json rows = item.value("rows", json());
        if (columns.is_array() && !columns.empty() && rows.is_array()) {
            json out_rows = json::array();
            for (const auto& row : rows) {
                if (out_rows.size() >= kMaxTabRows) {
                    break;
                }
                auto cells = strings(row, columns.size());
                if (cells.is_array()) {
                    while (cells.size() < columns.size()) {
                        cells.push_back("");
                    }
                    out_rows.push_back(std::move(cells));
                }
            }
            return json{
                {"type", type}, {"columns", std::move(columns)}, {"rows", std::move(out_rows)}};
        }
    } else if (type == "button") {
        auto label = text_field(item, "label");
        const std::string command = item.value("command", "");
        if (label && own_command(manifest, command)) {
            json out{{"type", type}, {"label", *label}, {"command", command}};
            if (item.contains("argument") && item["argument"].is_string()) {
                out["argument"] = clean(item["argument"].get<std::string>(), 100);
            }
            return out;
        }
    } else if (type == "image") {
        return image(item);
    } else if (type == "chart") {
        return chart(item);
    } else if (type == "form") {
        return form(item, manifest);
    }
    return nullptr;
}

} // namespace

core::Result<json> validate_web_data(const json& result, const Manifest& manifest) {
    if (!result.is_object()) {
        return core::make_error(core::ErrorCode::Module,
                                "onglet du module « " + manifest.id + " » mal formé");
    }
    json data = result.contains("data") ? result["data"] : json();
    const std::string serialized = data.dump(-1, ' ', false, json::error_handler_t::replace);
    if (serialized.size() > kMaxWebTabData) {
        return core::make_error(core::ErrorCode::Module, "données de l'onglet du module « " +
                                                             manifest.id + " » trop grandes");
    }
    return json{{"data", std::move(data)}};
}

bool is_tab_event_name(std::string_view name) {
    return !name.empty() && name.size() <= 64 && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

core::Result<json> validate_tab(const json& result, const Manifest& manifest) {
    if (!result.is_object() || !result.contains("components") || !result["components"].is_array()) {
        return core::make_error(core::ErrorCode::Module,
                                "onglet du module « " + manifest.id + " » mal formé");
    }
    json components = json::array();
    std::size_t images = 0;
    for (const auto& item : result["components"]) {
        if (components.size() >= kMaxTabComponents) {
            break;
        }
        auto checked = component(item, manifest);
        if (checked.is_null() || (checked["type"] == "image" && ++images > kMaxTabImages)) {
            continue;
        }
        components.push_back(std::move(checked));
    }
    return json{{"components", std::move(components)}};
}

} // namespace avenoir::modules
