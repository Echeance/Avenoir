#include "avenoir/modules/tabs.hpp"

#include <algorithm>
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
        const bool own =
            std::ranges::any_of(manifest.commands, [&command](const CommandDeclaration& c) {
                return c.name == command;
            });
        if (label && own) {
            json out{{"type", type}, {"label", *label}, {"command", command}};
            if (item.contains("argument") && item["argument"].is_string()) {
                out["argument"] = clean(item["argument"].get<std::string>(), 100);
            }
            return out;
        }
    }
    return nullptr;
}

} // namespace

core::Result<json> validate_tab(const json& result, const Manifest& manifest) {
    if (!result.is_object() || !result.contains("components") || !result["components"].is_array()) {
        return core::make_error(core::ErrorCode::Module,
                                "onglet du module « " + manifest.id + " » mal formé");
    }
    json components = json::array();
    for (const auto& item : result["components"]) {
        if (components.size() >= kMaxTabComponents) {
            break;
        }
        if (auto checked = component(item, manifest); !checked.is_null()) {
            components.push_back(std::move(checked));
        }
    }
    return json{{"components", std::move(components)}};
}

} // namespace avenoir::modules
