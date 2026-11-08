#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/tools/tools.hpp"

#include "app_index.hpp"
#include "platform.hpp"
#include "tools_impl.hpp"

namespace avenoir::tools {

namespace {

struct LaunchArgs {
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LaunchArgs, name)

struct OpenUrlArgs {
    std::string url;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OpenUrlArgs, url)

core::ToolError execution_error(std::string message) {
    return core::ToolError{.kind = core::ToolErrorKind::Execution, .message = std::move(message)};
}

/// « navigateur » et ses variantes : le navigateur par défaut.
bool is_browser_request(std::string_view name) {
    const auto wanted = detail::normalize_app_name(name);
    return wanted == "navigateur" || wanted == "navigateurweb" || wanted == "navigateurinternet" ||
           wanted == "browser" || wanted == "webbrowser" || wanted == "internet";
}

std::string names_of(const std::vector<AppEntry>& apps) {
    std::string out;
    for (const auto& app : apps) {
        out += (out.empty() ? "" : ", ") + app.name;
    }
    return out;
}

/// Lance une application. Non annulable de façon sûre : la fermer pourrait
/// faire perdre ce que l'utilisateur y a fait entre-temps.
class LaunchAppTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "launch_app",
            .description =
                "Lance une application installée, à partir du nom que l'utilisateur emploie (« "
                "bloc-notes », « calculatrice », « firefox », « Visual Studio Code », « code »…) : "
                "nom affiché, nom générique, mot-clé ou nom de l'exécutable conviennent, sans "
                "chemin ni option. « navigateur » lance le navigateur par défaut. Si plusieurs "
                "applications correspondent, l'outil les liste : demande laquelle.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"name",
                                {{"type", "string"}, {"description", "Nom de l'application"}}}}},
                             {"required", nlohmann::json::array({"name"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<LaunchArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        auto query = detail::validate_app_query(args->name);
        if (!query) {
            return std::unexpected(std::move(query.error()));
        }
        if (is_browser_request(*query)) {
            if (const auto browser = platform::default_browser()) {
                if (auto launched = platform::launch_entry(*browser); !launched) {
                    return std::unexpected(execution_error(std::move(launched.error())));
                }
                return core::ToolOutput{.content = "Navigateur « " + browser->name + " » lancé."};
            }
        }
        // 1. Les applications installées, par leur nom (relues une fois si
        //    rien ne correspond : elle vient peut-être d'être installée).
        auto match = detail::pick_app(AppIndex::apps(), *query);
        if (!match.best && match.candidates.empty()) {
            match = detail::pick_app(AppIndex::apps(/*refresh=*/true), *query);
        }
        if (match.best) {
            if (auto launched = platform::launch_entry(*match.best); !launched) {
                return std::unexpected(execution_error(std::move(launched.error())));
            }
            return core::ToolOutput{.content = "Application « " + match.best->name + " » lancée."};
        }
        // 2. Un exécutable, par son nom exact (validation stricte).
        if (auto exact = detail::validate_app_name(*query)) {
            if (platform::launch_app(*exact)) {
                return core::ToolOutput{.content = "Application « " + *exact + " » lancée."};
            }
        }
        if (!match.candidates.empty()) {
            return std::unexpected(execution_error(
                "plusieurs applications pourraient correspondre à « " + *query +
                " » : " + names_of(match.candidates) + ". Demande laquelle lancer."));
        }
        return std::unexpected(execution_error("application « " + *query +
                                               " » introuvable parmi les applications "
                                               "installées (list_apps permet de chercher)"));
    }
};

struct ListAppsArgs {
    std::string query;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ListAppsArgs, query)

/// Liste ou cherche les applications installées.
class ListAppsTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "list_apps",
            .description = "Cherche parmi les applications installées (nom, nom générique, "
                           "mots-clés). Inutile avant launch_app, qui trouve déjà l'application "
                           "par le nom que donne l'utilisateur : ne sert que si l'utilisateur "
                           "demande quelles applications sont installées, ou si launch_app n'a "
                           "rien trouvé. Sans recherche, en donne le nombre et une partie de la "
                           "liste.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"query",
                                {{"type", "string"},
                                 {"description", "Mots à chercher (« éditeur de texte »…)"}}}}}},
            .risk = core::RiskLevel::ReadOnly,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<ListAppsArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        auto apps = AppIndex::apps();
        if (args->query.empty()) {
            std::ranges::sort(apps, {}, &AppEntry::name);
            const std::size_t shown = std::min<std::size_t>(apps.size(), 40);
            const std::vector<AppEntry> first(apps.begin(),
                                              apps.begin() + static_cast<long>(shown));
            return core::ToolOutput{
                .content =
                    std::format("{} application(s) installée(s) ; {} : {}.", apps.size(),
                                shown < apps.size() ? "les premières" : "toutes", names_of(first))};
        }
        auto query = detail::validate_app_query(args->query);
        if (!query) {
            return std::unexpected(std::move(query.error()));
        }
        const auto found = detail::search_apps(apps, *query, 15);
        if (found.empty()) {
            return core::ToolOutput{.content = "Aucune application installée ne correspond à « " +
                                               *query + " »."};
        }
        return core::ToolOutput{.content = "Applications correspondant à « " + *query +
                                           " » : " + names_of(found) + "."};
    }
};

/// Ouvre une URL. Non annulable : on ne sait pas fermer un onglet précis.
class OpenUrlTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "open_url",
            .description = "Ouvre une adresse web dans le navigateur par défaut. Seules les "
                           "adresses http:// et https:// sont acceptées : jamais file://, un "
                           "chemin local ou un autre protocole.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"url",
                                {{"type", "string"},
                                 {"description", "URL complète, commençant par https://"}}}}},
                             {"required", nlohmann::json::array({"url"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& /*stop*/) const override {
        auto args = core::parse_args<OpenUrlArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        auto url = detail::validate_url(args->url);
        if (!url) {
            return std::unexpected(std::move(url.error()));
        }
        if (auto opened = platform::open_url(*url); !opened) {
            return std::unexpected(execution_error(std::move(opened.error())));
        }
        return core::ToolOutput{.content = *url + " ouvert dans le navigateur."};
    }
};

} // namespace

std::shared_ptr<const core::Tool> make_list_apps_tool() {
    return std::make_shared<ListAppsTool>();
}

std::shared_ptr<const core::Tool> make_launch_app_tool() {
    return std::make_shared<LaunchAppTool>();
}

std::shared_ptr<const core::Tool> make_open_url_tool() {
    return std::make_shared<OpenUrlTool>();
}

} // namespace avenoir::tools
