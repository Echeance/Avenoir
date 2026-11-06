#include "avenoir/core/router.hpp"

#include <array>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace avenoir::core {

namespace {

/// Retire les espaces autour et la ponctuation finale, y compris séparée par
/// une espace (« Lance firefox. », « Drapeau ! »).
std::string_view normalize(std::string_view input) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!input.empty() && is_space(input.front())) {
        input.remove_prefix(1);
    }
    while (!input.empty() &&
           (is_space(input.back()) || input.back() == '.' || input.back() == '!')) {
        input.remove_suffix(1);
    }
    return input;
}

} // namespace

Result<FastRouter> FastRouter::with_defaults() {
    FastRouter router;
    // L'ordre compte : les URL avant les applications.
    // Un seul « mot » pour l'application : « ouvre le fichier X et… » part au LLM.
    struct Default {
        std::string_view pattern;
        std::string_view tool;
        std::string_view arg;
    };
    const std::array<Default, 3> defaults{{
        {.pattern = R"(^(?:ouvre|va sur)\s+(https?://\S+)$)", .tool = "open_url", .arg = "url"},
        {.pattern = R"(^(?:ouvre|lance|démarre)\s+([\w.+-]+)$)",
         .tool = "launch_app",
         .arg = "name"},
        {.pattern = R"(^(?:quelle heure est-il|quel jour sommes-nous)\s*\??$)",
         .tool = "current_time",
         .arg = ""},
    }};
    for (const auto& rule : defaults) {
        if (auto added =
                router.add_rule(rule.pattern, std::string(rule.tool), std::string(rule.arg));
            !added) {
            return std::unexpected(std::move(added.error()));
        }
    }
    return router;
}

Result<void> FastRouter::add_rule(std::string_view pattern, std::string tool,
                                  std::string arg_name) {
    try {
        rules_.push_back(Rule{
            .regex = std::regex(pattern.begin(), pattern.end(),
                                std::regex::ECMAScript | std::regex::icase),
            .tool = std::move(tool),
            .arg_name = std::move(arg_name),
        });
    } catch (const std::regex_error& e) {
        return make_error(ErrorCode::Config, "règle de voie rapide invalide « " +
                                                 std::string(pattern) + " » : " + e.what());
    }
    return {};
}

std::optional<FastRoute> FastRouter::route(std::string_view input) const {
    const std::string_view text = normalize(input);
    if (text.empty()) {
        return std::nullopt;
    }
    for (const auto& rule : rules_) {
        std::match_results<std::string_view::const_iterator> match;
        if (!std::regex_match(text.begin(), text.end(), match, rule.regex)) {
            continue;
        }
        FastRoute route{.tool = rule.tool};
        if (!rule.arg_name.empty()) {
            if (match.size() < 2 || !match[1].matched) {
                continue;
            }
            route.arguments[rule.arg_name] = match[1].str();
        }
        return route;
    }
    return std::nullopt;
}

} // namespace avenoir::core
