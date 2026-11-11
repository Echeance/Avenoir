#include "avenoir/core/claims.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avenoir::core {

std::string normalize_reply(std::string_view text) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 9> kReplace{{
        {"’", "'"},
        {" ", " "},
        {" ", " "},
        {"É", "é"},
        {"È", "è"},
        {"Ê", "ê"},
        {"À", "à"},
        {"Ç", "ç"},
        {"Ô", "ô"},
    }};
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        bool replaced = false;
        for (const auto& [from, to] : kReplace) {
            if (text.substr(i).starts_with(from)) {
                out += to;
                i += from.size();
                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }
        const char c = text[i++];
        if (c == '*' || c == '`') {
            continue; // gras et code Markdown : « je passe à **qwen** »
        }
        out += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

ReplyCheck make_claim_check(std::vector<ClaimRule> rules) {
    return [rules =
                std::move(rules)](std::string_view reply,
                                  std::span<const ToolCall> calls) -> std::optional<ReplyIssue> {
        const std::string text = normalize_reply(reply);
        for (const auto& rule : rules) {
            const bool done = std::ranges::any_of(calls, [&rule](const ToolCall& call) {
                return std::ranges::contains(rule.tools, call.name);
            });
            const bool claimed = std::ranges::any_of(
                rule.phrases, [&text](const std::string& phrase) { return text.contains(phrase); });
            if (done || !claimed) {
                continue;
            }
            std::string tools;
            for (const auto& tool : rule.tools) {
                tools += (tools.empty() ? "" : " ou ") + tool;
            }
            return ReplyIssue{
                .reminder = "Attention : ta réponse précédente annonçait une action (" +
                            rule.action + "), mais aucun outil ne l'a faite : rien n'a été fait. " +
                            "Si l'utilisateur la demande, appelle maintenant l'outil (" + tools +
                            ") ; Avenoir demande lui-même confirmation si besoin. Sinon, réponds "
                            "sans annoncer d'action.",
                .warning = "(Avenoir : rien n'a été fait — " + rule.action + ".)"};
        }
        return std::nullopt;
    };
}

} // namespace avenoir::core
