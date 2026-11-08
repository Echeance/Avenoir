#include "modules.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "avenoir/core/tool.hpp"
#include "avenoir/terminal/style.hpp"

#include "display.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::paint;

std::string gray(std::string_view text) {
    return paint(text, {.color = Color::Gray});
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string permission_label(std::string_view permission) {
    if (permission == "network") {
        return "réseau";
    }
    if (permission == "files.read") {
        return "lecture de fichiers";
    }
    if (permission == "files.write") {
        return "écriture de fichiers";
    }
    if (permission == "ui.web") {
        return "onglet web";
    }
    return std::string(permission);
}

std::string join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += separator;
        }
        out += item;
    }
    return out;
}

std::string_view risk_label(core::RiskLevel risk) {
    switch (risk) {
    case core::RiskLevel::ReadOnly:
        return "lecture seule";
    case core::RiskLevel::Reversible:
        return "réversible";
    case core::RiskLevel::Sensitive:
        return "sensible";
    }
    return "sensible";
}

/// Niveau demandé à /risque : rien = « défaut » (retour au manifeste).
struct RiskRequest {
    std::optional<core::RiskLevel> risk;
};

std::optional<RiskRequest> parse_level(std::string_view text) {
    if (text == "lecture" || text == "lecture_seule" || text == "lecture-seule" ||
        text == "read_only") {
        return RiskRequest{.risk = core::RiskLevel::ReadOnly};
    }
    if (text == "réversible" || text == "reversible") {
        return RiskRequest{.risk = core::RiskLevel::Reversible};
    }
    if (text == "sensible" || text == "sensitive") {
        return RiskRequest{.risk = core::RiskLevel::Sensitive};
    }
    if (text == "défaut" || text == "defaut" || text == "default") {
        return RiskRequest{.risk = std::nullopt};
    }
    return std::nullopt;
}

void audit(const std::string& message) {
    const auto logger = spdlog::get("audit");
    (logger != nullptr ? logger : spdlog::default_logger())->info("{}", message);
}

} // namespace

ModulesUi::ModulesUi(modules::ModuleHost& host, std::vector<modules::ModuleCommand> commands)
    : host_(&host), commands_(std::move(commands)) {}

void ModulesUi::print_status() const {
    const auto statuses = host_->statuses();
    if (statuses.empty()) {
        display().info("Aucun module installé.");
        return;
    }
    for (const auto& status : statuses) {
        std::string icon;
        switch (status.state) {
        case modules::ModuleState::Running:
            icon = paint("●", {.color = Color::Green});
            break;
        case modules::ModuleState::Disabled:
            icon = gray("○");
            break;
        case modules::ModuleState::Failed:
            icon = paint("✗", {.color = Color::Red});
            break;
        }
        const std::string origin =
            status.origin == modules::Origin::Bundled ? "fourni avec Avenoir" : "installé par vous";
        const std::string protocol = status.protocol == modules::Protocol::Mcp ? " · MCP" : "";
        display().plain(std::format(
            "  {} {}  {}", icon, paint(status.name, {.bold = true}),
            gray(std::format("{} · v{} · {}{}", status.id, status.version, origin, protocol))));
        if (status.state == modules::ModuleState::Disabled) {
            display().plain("    " +
                            gray(std::format("désactivé — pour l'activer : "
                                             "[modules.{}] enabled = true dans config.toml",
                                             status.id)));
            continue;
        }
        if (status.state == modules::ModuleState::Failed) {
            display().plain("    " + paint("échec : " + status.message, {.color = Color::Red}));
        }
        std::vector<std::string> permissions;
        std::ranges::transform(status.permissions, std::back_inserter(permissions),
                               permission_label);
        display().plain("    " + gray("permissions : " +
                                      (permissions.empty() ? "aucune" : join(permissions, ", "))));
        if (!status.env.empty()) {
            display().plain("    " + gray("variables transmises : " + join(status.env, ", ")));
        }
        for (const auto& tool : status.tools) {
            display().plain(std::format("    {} {}  {}",
                                        paint(kToolGlyph, {.color = Color::Yellow}),
                                        terminal::pad(paint(tool.name, {.bold = true}), 26),
                                        terminal::risk_badge(tool.risk)));
        }
    }
}

void ModulesUi::print_risks() const {
    bool any = false;
    for (const auto& status : host_->statuses()) {
        for (const auto& tool : status.tools) {
            any = true;
            std::string line =
                std::format("  {}  {}", terminal::pad(paint(tool.name, {.bold = true}), 26),
                            terminal::risk_badge(tool.risk));
            if (tool.risk != tool.declared) {
                line += gray(std::format("  (manifeste : {})", risk_label(tool.declared)));
            }
            display().plain(line);
        }
    }
    if (!any) {
        display().info("Aucun outil de module chargé.");
        return;
    }
    display().plain(gray("Changer : /risque <outil> <lecture | réversible | sensible | défaut>"));
}

void ModulesUi::risk(std::string_view argument, Prompt& prompt, storage::Store* store) {
    argument = trim(argument);
    if (argument.empty()) {
        print_risks();
        return;
    }
    const auto space = argument.find(' ');
    const std::string tool(argument.substr(0, space));
    const auto level = space == std::string_view::npos
                           ? std::nullopt
                           : parse_level(trim(argument.substr(space + 1)));
    if (!level) {
        display().warning("Usage : /risque <outil> <lecture | réversible | sensible | défaut>");
        return;
    }
    const auto info = host_->tool_info(tool);
    if (!info) {
        display().warning(std::format("« {} » n'est pas un outil de module (voir /risque).", tool));
        return;
    }
    if (store == nullptr) {
        display().error("base indisponible : le choix ne peut pas être enregistré.");
        return;
    }

    if (!level->risk) {
        if (auto removed = store->remove_risk_choice(tool); !removed) {
            display().error(core::describe(removed.error()));
            return;
        }
        const auto decision = host_->clear_risk(tool);
        if (decision) {
            audit(std::format("risque de {} : retour au manifeste ({})", tool,
                              core::to_string(decision->risk)));
            display().success(std::format("{} : {} (risque du manifeste)", tool,
                                          terminal::risk_badge(decision->risk)));
        }
        return;
    }

    // Abaisser sous le risque prévu par l'auteur : choix explicite et tracé.
    const bool lowering = *level->risk < info->declared;
    if (lowering) {
        display().warning(
            "Cet outil sera exécuté avec moins de contrôles que ne le prévoit son auteur.");
        display().plain("  " + paint("Tout comportement ayant un impact négatif causé par ce "
                                     "changement relève de votre seule responsabilité.",
                                     {.color = Color::Yellow}));
        if (*level->risk == core::RiskLevel::ReadOnly ||
            info->declared == core::RiskLevel::Sensitive) {
            display().plain("  " + gray("L'outil pourra s'exécuter sans vous demander de "
                                        "confirmation."));
        }
        const auto answer =
            prompt.read(std::format("  Pour confirmer, tapez « {} » : ", kAgreementPhrase), false);
        if (answer.status != Prompt::Status::Line || trim(answer.text) != kAgreementPhrase) {
            display().info("Rien n'a été changé.");
            return;
        }
    }
    if (auto saved = store->set_risk_choice(tool, *level->risk, lowering); !saved) {
        display().error(core::describe(saved.error()));
        return;
    }
    const auto decision = host_->choose_risk(
        modules::RiskOverride{.tool = tool, .risk = *level->risk, .agreed = lowering});
    if (!decision) {
        display().error(core::describe(decision.error()));
        return;
    }
    audit(std::format("risque de {} : {} demandé, {} appliqué{}", tool,
                      core::to_string(*level->risk), core::to_string(decision->risk),
                      lowering ? std::format(" (accord « {} »)", kAgreementPhrase) : ""));
    display().success(std::format("{} : {}", tool, terminal::risk_badge(decision->risk)));
    if (!decision->warning.empty()) {
        display().warning(decision->warning);
    }
}

const modules::ModuleCommand* ModulesUi::find_command(std::string_view name) const {
    const auto it = std::ranges::find(commands_, name, &modules::ModuleCommand::name);
    return it == commands_.end() ? nullptr : &*it;
}

core::ToolCall ModulesUi::make_call(const modules::ModuleCommand& command,
                                    std::string_view argument) {
    nlohmann::json arguments = nlohmann::json::object();
    argument = trim(argument);
    if (!command.arg.empty() && !argument.empty()) {
        arguments[command.arg] = std::string(argument);
    }
    return core::ToolCall{.id = "command", .name = command.tool, .arguments = std::move(arguments)};
}

std::vector<std::string> ModulesUi::completion_names() const {
    std::vector<std::string> out;
    out.reserve(commands_.size());
    for (const auto& command : commands_) {
        out.push_back("/" + command.name);
    }
    return out;
}

std::string ModulesUi::help() const {
    std::string out;
    for (const auto& command : commands_) {
        const std::string usage = "/" + command.name + (command.arg.empty() ? "" : " [texte]");
        const std::string description =
            command.description.empty() ? "module " + command.module : command.description;
        out += "  " + terminal::pad(paint(usage, {.color = Color::Cyan}), 26) + gray(description) +
               "\n";
    }
    return out;
}

} // namespace avenoir::cli
