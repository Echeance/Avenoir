#include "cases.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <utility>

#include <toml++/toml.hpp>

namespace avenoir::evals {

namespace {

using nlohmann::json;

std::unexpected<core::Error> suite_error(std::string_view where, std::string_view what) {
    return core::make_error(core::ErrorCode::Config,
                            std::string(where) + " : " + std::string(what));
}

/// Valeur TOML simple → JSON. Les arguments attendus sont des textes,
/// nombres ou booléens ; une table ou une liste n'y a pas sa place.
std::optional<json> scalar_to_json(const toml::node& node) {
    if (node.is_table() || node.is_array()) {
        return std::nullopt;
    }
    if (const auto value = node.value<std::int64_t>(); value && node.is_integer()) {
        return *value;
    }
    if (const auto value = node.value<double>(); value && node.is_floating_point()) {
        return *value;
    }
    if (const auto value = node.value<bool>()) {
        return *value;
    }
    return node.value_or(std::string{});
}

/// Refuse les clés inconnues d'une table.
core::Result<void> only_keys(const toml::table& table, std::string_view where,
                             std::initializer_list<std::string_view> known) {
    for (const auto& [key, value] : table) {
        if (std::ranges::find(known, key.str()) == known.end()) {
            return suite_error(std::string(where) + "." + std::string(key.str()), "clé inconnue");
        }
    }
    return {};
}

core::Result<std::vector<std::string>> strings(const toml::table& table, std::string_view where,
                                               std::string_view key) {
    std::vector<std::string> out;
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return out;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        return suite_error(std::string(where) + "." + std::string(key), "liste de textes attendue");
    }
    for (const auto& item : *array) {
        const auto text = item.value<std::string>();
        if (!text || !item.is_string()) {
            return suite_error(std::string(where) + "." + std::string(key),
                               "liste de textes attendue");
        }
        out.push_back(*text);
    }
    return out;
}

core::Result<std::map<std::string, std::string, std::less<>>>
string_map(const toml::table& table, std::string_view where, std::string_view key) {
    std::map<std::string, std::string, std::less<>> out;
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return out;
    }
    const auto* inner = node->as_table();
    if (inner == nullptr) {
        return suite_error(std::string(where) + "." + std::string(key), "table attendue");
    }
    for (const auto& [name, value] : *inner) {
        const auto text = value.value<std::string>();
        if (!text || !value.is_string()) {
            return suite_error(std::string(where) + "." + std::string(key) + "." +
                                   std::string(name.str()),
                               "texte attendu");
        }
        out.emplace(std::string(name.str()), *text);
    }
    return out;
}

core::Result<core::PermissionPolicy> parse_policy(const toml::table& table,
                                                  std::string_view where) {
    if (auto known = only_keys(table, where, {"read_only", "reversible", "sensitive"}); !known) {
        return std::unexpected(std::move(known.error()));
    }
    core::PermissionPolicy policy;
    for (auto [key, target] :
         {std::pair{"read_only", &policy.read_only}, std::pair{"reversible", &policy.reversible},
          std::pair{"sensitive", &policy.sensitive}}) {
        const toml::node* node = table.get(key);
        if (node == nullptr) {
            continue;
        }
        const auto value = core::policy_from_string(node->value_or(std::string{}));
        if (!value) {
            return suite_error(std::string(where) + "." + key, "allow, confirm ou deny attendu");
        }
        *target = *value;
    }
    return policy;
}

core::Result<ExpectedCall> parse_expected(const toml::node& node, std::string_view where) {
    const auto* table = node.as_table();
    if (table == nullptr) {
        return suite_error(where, "table attendue");
    }
    if (auto known = only_keys(*table, where, {"tool", "arguments"}); !known) {
        return std::unexpected(std::move(known.error()));
    }
    ExpectedCall call;
    const auto tool = (*table)["tool"].value<std::string>();
    if (!tool || tool->empty()) {
        return suite_error(std::string(where) + ".tool", "nom d'outil attendu");
    }
    call.tool = *tool;
    if (const toml::node* arguments = table->get("arguments")) {
        const auto* inner = arguments->as_table();
        if (inner == nullptr) {
            return suite_error(std::string(where) + ".arguments", "table attendue");
        }
        for (const auto& [key, value] : *inner) {
            auto converted = scalar_to_json(value);
            if (!converted) {
                return suite_error(std::string(where) + ".arguments." + std::string(key.str()),
                                   "texte, nombre ou booléen attendu");
            }
            call.arguments[std::string(key.str())] = std::move(*converted);
        }
    }
    return call;
}

core::Result<Case> parse_case(const toml::table& table, std::size_t index) {
    std::string where = std::format("cases[{}]", index);
    Case test;
    const auto id = table["id"].value<std::string>();
    if (!id || id->empty()) {
        return suite_error(where + ".id", "identifiant obligatoire");
    }
    test.id = *id;
    where += " (" + test.id + ")";
    if (auto known = only_keys(table, where,
                               {"id", "category", "request", "requests", "confirm", "policy",
                                "expect", "forbid", "forbid_in_arguments", "max_calls",
                                "reply_contains", "reply_contains_any", "reply_excludes",
                                "preamble_excludes", "results", "failures"});
        !known) {
        return std::unexpected(std::move(known.error()));
    }
    test.category = table["category"].value_or(std::string{"divers"});

    if (const auto request = table["request"].value<std::string>()) {
        test.requests.push_back(*request);
    }
    auto requests = strings(table, where, "requests");
    if (!requests) {
        return std::unexpected(std::move(requests.error()));
    }
    std::ranges::move(*requests, std::back_inserter(test.requests));
    if (test.requests.empty()) {
        return suite_error(where, "request ou requests obligatoire");
    }

    const std::string confirm = table["confirm"].value_or(std::string{"reject"});
    if (confirm != "accept" && confirm != "reject") {
        return suite_error(where + ".confirm", "accept ou reject attendu");
    }
    test.confirmation = confirm == "accept" ? core::Decision::Approved : core::Decision::Rejected;

    if (const auto* policy = table["policy"].as_table()) {
        auto parsed = parse_policy(*policy, where + ".policy");
        if (!parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
        test.policy = *parsed;
    }

    if (const toml::node* expect = table.get("expect")) {
        const auto* array = expect->as_array();
        if (array == nullptr) {
            return suite_error(where + ".expect", "liste de tables attendue ([[cases.expect]])");
        }
        for (std::size_t i = 0; i < array->size(); ++i) {
            auto call = parse_expected(*array->get(i), std::format("{}.expect[{}]", where, i));
            if (!call) {
                return std::unexpected(std::move(call.error()));
            }
            test.expect.push_back(std::move(*call));
        }
    }

    if (const toml::node* max = table.get("max_calls")) {
        const auto value = max->value<std::int64_t>();
        if (!value || *value < 0) {
            return suite_error(where + ".max_calls", "entier positif attendu");
        }
        test.max_calls = static_cast<std::size_t>(*value);
    }

    auto forbid = strings(table, where, "forbid");
    auto forbid_text = strings(table, where, "forbid_in_arguments");
    auto contains = strings(table, where, "reply_contains");
    auto contains_any = strings(table, where, "reply_contains_any");
    auto excludes = strings(table, where, "reply_excludes");
    auto preamble = strings(table, where, "preamble_excludes");
    auto results = string_map(table, where, "results");
    auto failures = string_map(table, where, "failures");
    for (const auto* error :
         {forbid ? nullptr : &forbid.error(), forbid_text ? nullptr : &forbid_text.error(),
          contains ? nullptr : &contains.error(), contains_any ? nullptr : &contains_any.error(),
          excludes ? nullptr : &excludes.error(), preamble ? nullptr : &preamble.error(),
          results ? nullptr : &results.error(), failures ? nullptr : &failures.error()}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    test.forbid = std::move(*forbid);
    test.forbid_in_arguments = std::move(*forbid_text);
    test.reply_contains = std::move(*contains);
    test.reply_contains_any = std::move(*contains_any);
    test.reply_excludes = std::move(*excludes);
    test.preamble_excludes = std::move(*preamble);
    test.results = std::move(*results);
    test.failures = std::move(*failures);
    return test;
}

/// Minuscules pour comparer sans casse : ASCII et lettres accentuées
/// latines (À à Þ, sauf ×), ce qui suffit au français.
std::string fold(std::string_view text) {
    std::string out(text);
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto& byte = out[i];
        if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<char>(byte - 'A' + 'a');
        } else if (static_cast<unsigned char>(byte) == 0xC3 && i + 1 < out.size()) {
            auto& next = out[i + 1];
            const auto value = static_cast<unsigned char>(next);
            if (value >= 0x80 && value <= 0x9E && value != 0x97) {
                next = static_cast<char>(value + 0x20);
            }
            ++i;
        }
    }
    return out;
}

bool contains_folded(std::string_view text, std::string_view part) {
    return fold(text).contains(fold(part));
}

bool matches_value(const json& expected, const json& actual) {
    if (expected.is_string()) {
        return actual.is_string() &&
               contains_folded(actual.get<std::string>(), expected.get<std::string>());
    }
    if (expected.is_number()) {
        return actual.is_number() && std::abs(actual.get<double>() - expected.get<double>()) < 1e-9;
    }
    return expected == actual;
}

bool matches(const ExpectedCall& expected, const core::ToolCall& call) {
    if (call.name != expected.tool || !call.arguments.is_object()) {
        return false;
    }
    return std::ranges::all_of(expected.arguments.items(), [&call](const auto& item) {
        const auto found = call.arguments.find(item.key());
        return found != call.arguments.end() && matches_value(item.value(), *found);
    });
}

std::string list(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        out += (out.empty() ? "« " : ", « ") + item + " »";
    }
    return out;
}

} // namespace

core::Result<Suite> parse_suite(std::string_view toml_text) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& e) {
        return suite_error(std::format("ligne {}", e.source().begin.line), e.description());
    }
    if (auto known = only_keys(root, "racine", {"results", "cases"}); !known) {
        return std::unexpected(std::move(known.error()));
    }
    Suite suite;
    auto results = string_map(root, "racine", "results");
    if (!results) {
        return std::unexpected(std::move(results.error()));
    }
    suite.results = std::move(*results);

    const auto* cases = root["cases"].as_array();
    if (cases == nullptr || cases->empty()) {
        return suite_error("cases", "aucun cas ([[cases]])");
    }
    for (std::size_t i = 0; i < cases->size(); ++i) {
        const auto* table = cases->get(i)->as_table();
        if (table == nullptr) {
            return suite_error(std::format("cases[{}]", i), "table attendue");
        }
        auto test = parse_case(*table, i);
        if (!test) {
            return std::unexpected(std::move(test.error()));
        }
        const bool duplicate = std::ranges::any_of(
            suite.cases, [&test](const Case& other) { return other.id == test->id; });
        if (duplicate) {
            return suite_error(std::format("cases[{}]", i),
                               "identifiant en double « " + test->id + " »");
        }
        suite.cases.push_back(std::move(*test));
    }
    return suite;
}

core::Result<Suite> load_suite(const std::filesystem::path& path) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::make_error(core::ErrorCode::Config,
                                "fichier de cas illisible : " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return parse_suite(text.str());
}

std::string fill_template(std::string_view text, const json& arguments) {
    std::string out;
    while (!text.empty()) {
        const auto open = text.find('{');
        const auto close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) {
            out += text;
            break;
        }
        out += text.substr(0, open);
        const std::string_view inside = text.substr(open + 1, close - open - 1);
        const auto bar = inside.find('|');
        const std::string key(inside.substr(0, bar));
        const auto found = arguments.is_object() ? arguments.find(key) : arguments.end();
        if (found != arguments.end() && found->is_string()) {
            out += found->get<std::string>();
        } else if (found != arguments.end() && found->is_number()) {
            out += found->dump();
        } else if (bar != std::string_view::npos) {
            out += inside.substr(bar + 1);
        } else {
            out += text.substr(open, close - open + 1); // Laissé tel quel.
        }
        text.remove_prefix(close + 1);
    }
    return out;
}

std::vector<std::string> check(const Case& test, const Transcript& transcript) {
    std::vector<std::string> failures;
    if (transcript.error) {
        failures.push_back("erreur : " + *transcript.error);
    }

    // Chaque appel attendu doit correspondre à un appel réel distinct.
    std::vector<bool> used(transcript.calls.size(), false);
    for (const auto& expected : test.expect) {
        bool found = false;
        for (std::size_t i = 0; i < transcript.calls.size() && !found; ++i) {
            if (!used[i] && matches(expected, transcript.calls[i])) {
                used[i] = true;
                found = true;
            }
        }
        if (!found) {
            failures.push_back(std::format("appel attendu absent : {} {}", expected.tool,
                                           expected.arguments.dump()));
        }
    }

    for (const auto& call : transcript.calls) {
        if (std::ranges::contains(test.forbid, call.name)) {
            failures.push_back("appel interdit : " + describe_call(call));
        }
        const std::string arguments = call.arguments.dump(-1, ' ', /*ensure_ascii=*/false);
        for (const auto& text : test.forbid_in_arguments) {
            if (contains_folded(arguments, text)) {
                failures.push_back(
                    std::format("argument interdit « {} » : {}", text, describe_call(call)));
            }
        }
    }
    if (test.max_calls && transcript.calls.size() > *test.max_calls) {
        failures.push_back(std::format("{} appel(s) d'outil, {} au plus", transcript.calls.size(),
                                       *test.max_calls));
    }

    std::vector<std::string> missing;
    std::ranges::copy_if(test.reply_contains, std::back_inserter(missing),
                         [&transcript](const std::string& part) {
                             return !contains_folded(transcript.reply, part);
                         });
    if (!missing.empty()) {
        failures.push_back("réponse sans " + list(missing));
    }
    if (!test.reply_contains_any.empty() &&
        std::ranges::none_of(test.reply_contains_any, [&transcript](const std::string& part) {
            return contains_folded(transcript.reply, part);
        })) {
        failures.push_back("réponse sans aucun de " + list(test.reply_contains_any));
    }
    std::vector<std::string> present;
    std::ranges::copy_if(
        test.reply_excludes, std::back_inserter(present),
        [&transcript](const std::string& part) { return contains_folded(transcript.reply, part); });
    if (!present.empty()) {
        failures.push_back("réponse avec " + list(present));
    }
    for (const auto& text : transcript.preambles) {
        std::vector<std::string> announced;
        std::ranges::copy_if(
            test.preamble_excludes, std::back_inserter(announced),
            [&text](const std::string& part) { return contains_folded(text, part); });
        if (!announced.empty()) {
            failures.push_back(std::format(
                "données annoncées avant le résultat de l'outil ({}) : {}", list(announced), text));
        }
    }
    return failures;
}

std::string describe_call(const core::ToolCall& call) {
    return call.name + " " + call.arguments.dump(-1, ' ', /*ensure_ascii=*/false);
}

} // namespace avenoir::evals
