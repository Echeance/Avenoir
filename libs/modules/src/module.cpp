#include "module.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace avenoir::modules {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxUndoDescription = 200;
constexpr std::size_t kMaxUndoToken = 1024;
constexpr std::size_t kMaxResultBytes = std::size_t{1} << 20U;

/// Décode un caractère UTF-8 à partir de `i` ; renvoie rien si la séquence
/// est invalide (et avance alors d'un octet).
std::optional<char32_t> decode(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char lead = byte(i);
    std::size_t length = 0;
    char32_t code = 0;
    if (lead < 0x80U) {
        ++i;
        return lead;
    }
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
        ++i;
        return std::nullopt;
    }
    if (i + length > text.size()) {
        ++i;
        return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
        if ((byte(i + k) & 0xC0U) != 0x80U) {
            ++i;
            return std::nullopt;
        }
        code = (code << 6U) | (byte(i + k) & 0x3FU);
    }
    // Formes trop longues, surrogates et valeurs hors Unicode refusées.
    const bool overlong = (length == 2 && code < 0x80U) || (length == 3 && code < 0x800U) ||
                          (length == 4 && code < 0x10000U);
    if (overlong || (code >= 0xD800U && code <= 0xDFFFU) || code > 0x10FFFFU) {
        ++i;
        return std::nullopt;
    }
    i += length;
    return code;
}

void append_utf8(std::string& out, char32_t code) {
    if (code < 0x80U) {
        out += static_cast<char>(code);
    } else if (code < 0x800U) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000U) {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (code >> 18U));
        out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

/// Caractères qui pourraient piloter le terminal ou tromper la lecture :
/// contrôles C0 / C1, DEL, et marques de direction bidirectionnelles.
bool is_dangerous(char32_t code) {
    return code < 0x20U || code == 0x7FU || (code >= 0x80U && code <= 0x9FU) ||
           (code >= 0x202AU && code <= 0x202EU) || (code >= 0x2066U && code <= 0x2069U);
}

/// Texte d'un résultat MCP : contenus `text` concaténés.
std::string content_text(const json& result) {
    std::string text;
    const auto content = result.find("content");
    if (content != result.end() && content->is_array()) {
        for (const auto& item : *content) {
            if (!item.is_object()) {
                continue;
            }
            const auto type = item.find("type");
            const std::string kind =
                type != item.end() && type->is_string() ? type->get<std::string>() : "";
            if (!text.empty()) {
                text += '\n';
            }
            if (kind == "text") {
                const auto value = item.find("text");
                text += value != item.end() && value->is_string() ? value->get<std::string>() : "";
            } else if (kind == "image") {
                text += "[image]";
            } else if (kind == "audio") {
                text += "[audio]";
            } else {
                text += "[ressource]";
            }
        }
    }
    if (text.empty()) {
        if (const auto structured = result.find("structuredContent"); structured != result.end()) {
            text = structured->dump(-1, ' ', false, json::error_handler_t::replace);
        }
    }
    return text;
}

/// Annulation proposée par le module dans `_meta`, si elle est bien formée.
std::optional<core::UndoAction> undo_action(const json& result,
                                            const std::shared_ptr<const Module>& module) {
    const auto meta = result.find("_meta");
    if (meta == result.end() || !meta->is_object()) {
        return std::nullopt;
    }
    const auto undo = meta->find(kUndoMetaKey);
    if (undo == meta->end() || !undo->is_object()) {
        return std::nullopt;
    }
    const auto description = undo->find("description");
    const auto token = undo->find("token");
    if (description == undo->end() || !description->is_string() || token == undo->end() ||
        !token->is_string()) {
        return std::nullopt;
    }
    const auto token_text = token->get<std::string>();
    std::string label = clean_text(description->get<std::string>(), kMaxUndoDescription, false);
    if (token_text.empty() || token_text.size() > kMaxUndoToken || label.empty()) {
        return std::nullopt;
    }
    return core::UndoAction{
        .description = std::move(label),
        .run = [module, token_text] -> core::ToolResult {
            auto reply = module->undo(token_text);
            if (!reply) {
                return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                       .message = reply.error().message});
            }
            // Une annulation ne propose pas d'annulation à son tour.
            return to_output(*reply, module, /*allow_undo=*/false);
        },
    };
}

} // namespace

std::string clean_text(std::string_view text, std::size_t max_bytes, bool multiline) {
    std::string out;
    out.reserve(std::min(text.size(), max_bytes));
    std::size_t i = 0;
    while (i < text.size()) {
        const auto code = decode(text, i);
        std::string piece;
        if (!code) {
            append_utf8(piece, 0xFFFDU);
        } else if (*code == U'\n' || *code == U'\t') {
            piece = multiline ? std::string(1, static_cast<char>(*code)) : " ";
        } else if (is_dangerous(*code)) {
            continue;
        } else {
            append_utf8(piece, *code);
        }
        if (out.size() + piece.size() > max_bytes) {
            out += "…";
            break;
        }
        out += piece;
    }
    return out;
}

Module::Module(Manifest manifest, std::unique_ptr<RpcClient> client,
               std::chrono::milliseconds call_timeout)
    : manifest_(std::move(manifest)), client_(std::move(client)), call_timeout_(call_timeout) {}

core::Result<nlohmann::json> Module::call_tool(std::string_view name,
                                               const nlohmann::json& arguments,
                                               const std::stop_token& stop) const {
    return client_->request("tools/call", {{"name", name}, {"arguments", arguments}}, call_timeout_,
                            stop);
}

core::Result<nlohmann::json> Module::undo(std::string_view token) const {
    return client_->request("avenoir/undo", {{"token", token}}, call_timeout_);
}

ModuleTool::ModuleTool(std::shared_ptr<const Module> module, std::string short_name,
                       core::ToolSpec spec)
    : module_(std::move(module)), short_name_(std::move(short_name)), spec_(std::move(spec)),
      risk_(spec_.risk) {}

core::ToolSpec ModuleTool::spec() const {
    core::ToolSpec out = spec_;
    out.risk = risk_.load();
    return out;
}

core::ToolResult ModuleTool::call(const nlohmann::json& arguments,
                                  const std::stop_token& stop) const {
    if (!module_->alive()) {
        return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                               .message = "module « " + module_->manifest().id +
                                                          " » arrêté : " + module_->failure()});
    }
    auto result = module_->call_tool(short_name_, arguments, stop);
    if (!result) {
        return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                               .message = result.error().message});
    }
    return to_output(*result, module_, module_->manifest().protocol == Protocol::Avenoir);
}

core::ToolResult to_output(const nlohmann::json& result,
                           const std::shared_ptr<const Module>& module, bool allow_undo) {
    if (!result.is_object()) {
        return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                               .message = "réponse du module mal formée"});
    }
    std::string text = clean_text(content_text(result), kMaxResultBytes, true);
    const auto is_error = result.find("isError");
    if (is_error != result.end() && is_error->is_boolean() && is_error->get<bool>()) {
        return std::unexpected(core::ToolError{
            .kind = core::ToolErrorKind::Execution,
            .message = text.empty() ? "le module a signalé une erreur" : std::move(text)});
    }
    core::ToolOutput output{.content = text.empty() ? "(aucun résultat)" : std::move(text)};
    if (allow_undo) {
        output.undo = undo_action(result, module);
    }
    return output;
}

} // namespace avenoir::modules
