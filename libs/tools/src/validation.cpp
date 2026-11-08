#include <algorithm>
#include <expected>
#include <string>
#include <string_view>

#include "avenoir/tools/tools.hpp"

namespace avenoir::tools::detail {

namespace {

constexpr std::size_t kMaxAppNameLength = 100;
constexpr std::size_t kMaxUrlLength = 2048;

std::unexpected<core::ToolError> invalid(std::string message) {
    return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::InvalidArguments,
                                           .message = std::move(message)});
}

bool is_ascii_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::expected<std::string, core::ToolError> validate_app_name(std::string_view name) {
    const std::string_view trimmed = trim(name);
    const auto allowed = [](char c) {
        return is_ascii_alnum(c) || c == '.' || c == '-' || c == '_' || c == '+' || c == ' ';
    };
    if (trimmed.empty() || trimmed.size() > kMaxAppNameLength || trimmed.front() == '-' ||
        trimmed.front() == '.' || !std::ranges::all_of(trimmed, allowed)) {
        return invalid("nom d'application refusé : « " + std::string(name) + " »");
    }
    return std::string(trimmed);
}

std::expected<std::string, core::ToolError> validate_app_query(std::string_view name) {
    const std::string_view trimmed = trim(name);
    const auto refused = [&name] {
        return invalid("nom d'application refusé : « " + std::string(name) + " »");
    };
    if (trimmed.empty() || trimmed.size() > kMaxAppNameLength || trimmed.front() == '-' ||
        trimmed.front() == '.') {
        return refused();
    }
    std::size_t i = 0;
    while (i < trimmed.size()) {
        const auto lead = static_cast<unsigned char>(trimmed[i]);
        if (lead < 0x80U) {
            const char c = trimmed[i++];
            if (!is_ascii_alnum(c) && c != '.' && c != '-' && c != '_' && c != '+' && c != ' ' &&
                c != '\'') {
                return refused(); // contrôle, séparateur de commande, chemin…
            }
            continue;
        }
        // UTF-8 : longueur d'après l'octet de tête, puis octets de continuation.
        std::size_t length = 0;
        char32_t code = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            code = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            code = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            code = lead & 0x07U;
        } else {
            return refused();
        }
        if (i + length > trimmed.size()) {
            return refused();
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto byte = static_cast<unsigned char>(trimmed[i + k]);
            if ((byte & 0xC0U) != 0x80U) {
                return refused();
            }
            code = (code << 6U) | (byte & 0x3FU);
        }
        // Contrôles C1, marques bidirectionnelles et caractères invisibles.
        const bool dangerous = code < 0xA0 || (code >= 0x200B && code <= 0x200F) ||
                               (code >= 0x202A && code <= 0x202E) ||
                               (code >= 0x2066 && code <= 0x2069) || code == 0xFEFF;
        if (dangerous) {
            return refused();
        }
        i += length;
    }
    return std::string(trimmed);
}

std::expected<std::string, core::ToolError> validate_url(std::string_view url) {
    const std::string_view trimmed = trim(url);
    std::string_view rest;
    if (trimmed.starts_with("https://")) {
        rest = trimmed.substr(8);
    } else if (trimmed.starts_with("http://")) {
        rest = trimmed.substr(7);
    } else {
        return invalid("seules les adresses http:// et https:// sont acceptées");
    }
    const auto forbidden = [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte <= 0x20U || byte == 0x7FU;
    };
    if (rest.empty() || rest.front() == '/' || trimmed.size() > kMaxUrlLength ||
        std::ranges::any_of(trimmed, forbidden)) {
        return invalid("URL refusée : « " + std::string(url) + " »");
    }
    return std::string(trimmed);
}

} // namespace avenoir::tools::detail
