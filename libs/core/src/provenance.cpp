#include "avenoir/core/provenance.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::core {

namespace {

constexpr std::size_t kMinChecked = 3;
constexpr std::size_t kMaxChecked = 200;

/// Minuscules ASCII (les autres octets, UTF-8 compris, sont gardés).
std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// Domaine d'une adresse http(s) (« pirate.example »), sans « www. », sinon vide.
std::string host_of(std::string_view url) {
    for (const std::string_view scheme : {"https://", "http://"}) {
        if (url.starts_with(scheme)) {
            url.remove_prefix(scheme.size());
            url = url.substr(0, url.find_first_of("/?#:"));
            // « www.sncf-connect.com » : l'utilisateur écrit « sncf-connect.com ».
            if (url.starts_with("www.")) {
                url.remove_prefix(4);
            }
            return std::string(url);
        }
    }
    return {};
}

struct Texts {
    std::string user; ///< Demandes de l'utilisateur, en minuscules.
    std::string data; ///< Résultats d'outils, en minuscules.
};

/// Faux si `value` (minuscules) n'est pas suspecte.
bool suspicious(const std::string& value, const Texts& texts) {
    if (!texts.data.contains(value) || texts.user.contains(value)) {
        return false;
    }
    const std::string host = host_of(value);
    return host.empty() || !texts.user.contains(host);
}

/// Première valeur texte suspecte de `root`, en parcourant aussi les listes
/// et objets imbriqués (sans récursion).
std::optional<std::string> find(const nlohmann::json& root, const Texts& texts) {
    std::vector<const nlohmann::json*> pending{&root};
    while (!pending.empty()) {
        const nlohmann::json* value = pending.back();
        pending.pop_back();
        if (value->is_string()) {
            const std::string_view text = trimmed(value->get_ref<const std::string&>());
            if (text.size() >= kMinChecked && text.size() <= kMaxChecked &&
                suspicious(lower(text), texts)) {
                return std::string(text);
            }
        } else if (value->is_array() || value->is_object()) {
            for (const auto& item : *value) {
                pending.push_back(&item);
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> untrusted_argument(const ToolSpec& spec, const nlohmann::json& arguments,
                                              std::span<const Message> history,
                                              const std::optional<std::string>& requester) {
    if (spec.risk == RiskLevel::ReadOnly || !arguments.is_object()) {
        return std::nullopt;
    }
    Texts texts;
    if (requester) {
        texts.user = lower(*requester);
    }
    for (const auto& message : history) {
        if (const auto* user = std::get_if<UserMessage>(&message)) {
            // Agent d'une équipe : sa demande vient du coordinateur (données).
            (requester ? texts.data : texts.user) += lower(user->content) + "\n";
        } else if (const auto* result = std::get_if<ToolResultMessage>(&message)) {
            texts.data += lower(result->content) + "\n";
        }
    }
    if (texts.data.empty()) {
        return std::nullopt;
    }
    const nlohmann::json* properties = nullptr;
    if (spec.input_schema.is_object() && spec.input_schema.contains("properties") &&
        spec.input_schema["properties"].is_object()) {
        properties = &spec.input_schema["properties"];
    }
    for (const auto& [key, value] : arguments.items()) {
        // Valeur d'une liste fermée : elle ne porte rien qu'une injection
        // pourrait choisir librement.
        if (properties != nullptr && properties->contains(key) && (*properties)[key].is_object() &&
            (*properties)[key].contains("enum")) {
            continue;
        }
        if (auto found = find(value, texts)) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace avenoir::core
