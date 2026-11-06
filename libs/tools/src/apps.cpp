#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "avenoir/tools/tools.hpp"

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

/// Lance une application. Non annulable de façon sûre : la fermer pourrait
/// faire perdre ce que l'utilisateur y a fait entre-temps.
class LaunchAppTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "launch_app",
            .description = "Lance une application installée à partir de son nom (ex. « firefox », "
                           "« code », « org.gnome.Nautilus », « notepad »). Le nom est celui de "
                           "l'exécutable ou de l'application installée, sans chemin ni option.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"name",
                                {{"type", "string"},
                                 {"description", "Nom de l'application ou de l'exécutable"}}}}},
                             {"required", nlohmann::json::array({"name"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments) const override {
        auto args = core::parse_args<LaunchArgs>(arguments);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        auto name = detail::validate_app_name(args->name);
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        if (auto launched = platform::launch_app(*name); !launched) {
            return std::unexpected(execution_error(std::move(launched.error())));
        }
        return core::ToolOutput{.content = "Application « " + *name + " » lancée."};
    }
};

/// Ouvre une URL. Non annulable : on ne sait pas fermer un onglet précis.
class OpenUrlTool final : public core::Tool {
  public:
    [[nodiscard]] core::ToolSpec spec() const override {
        return core::ToolSpec{
            .name = "open_url",
            .description = "Ouvre une adresse web (http ou https) dans le navigateur par défaut.",
            .input_schema = {{"type", "object"},
                             {"properties",
                              {{"url",
                                {{"type", "string"},
                                 {"description", "URL complète, commençant par https://"}}}}},
                             {"required", nlohmann::json::array({"url"})}},
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments) const override {
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

std::shared_ptr<const core::Tool> make_launch_app_tool() {
    return std::make_shared<LaunchAppTool>();
}

std::shared_ptr<const core::Tool> make_open_url_tool() {
    return std::make_shared<OpenUrlTool>();
}

} // namespace avenoir::tools
