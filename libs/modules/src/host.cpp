#include "avenoir/modules/host.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/env.hpp"
#include "avenoir/core/version.hpp"

#include "module.hpp"
#include "platform.hpp"
#include "rpc_client.hpp"

namespace avenoir::modules {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxTools = 128;        ///< Outils par module.
constexpr std::size_t kMaxToolPages = 16;     ///< Pages de `tools/list`.
constexpr std::size_t kMaxDescription = 1024; ///< Description d'un outil (octets).
constexpr std::size_t kMaxSchemaBytes = std::size_t{32} << 10U; ///< Schéma d'arguments sérialisé.
constexpr std::size_t kMaxLogBytes = std::size_t{1} << 20U;     ///< Taille d'un fichier de log.

std::unexpected<core::Error> module_error(std::string message) {
    return core::make_error(core::ErrorCode::Module, std::move(message));
}

/// Crée un dossier lisible par l'utilisateur seul.
bool create_private_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return false;
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    return true;
}

/// Résout le programme d'un module : fichier de son dossier (sans sortir de
/// ce dossier, liens symboliques compris), sinon nom nu cherché dans le PATH.
core::Result<std::string> resolve_program(const Manifest& manifest) {
    const std::filesystem::path program(manifest.run.front());
    std::error_code error;
    const auto root = std::filesystem::canonical(manifest.directory, error);
    if (error) {
        return module_error("dossier du module introuvable");
    }
    for (const auto& candidate : platform::program_candidates(root, program)) {
        if (!std::filesystem::is_regular_file(candidate, error)) {
            continue;
        }
        const auto real = std::filesystem::canonical(candidate, error);
        if (error) {
            continue;
        }
        const auto relative = real.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..") {
            return module_error("le programme « " + manifest.run.front() +
                                " » pointe hors du dossier du module : refusé");
        }
        return real.string();
    }
    // Nom nu (`npx`, `uvx`…) : cherché dans le PATH au lancement.
    if (std::distance(program.begin(), program.end()) == 1) {
        return manifest.run.front();
    }
    return module_error("programme introuvable : " + manifest.run.front());
}

/// Environnement minimal d'un module (directive de sécurité n° 15) : jamais
/// les secrets d'Avenoir ni ceux des autres modules.
std::map<std::string, std::string>
module_environment(const Manifest& manifest, const std::optional<std::filesystem::path>& data) {
    std::map<std::string, std::string> env;
    const auto copy = [&env](std::string_view name) {
        if (auto value = core::read_env(name)) {
            env.emplace(std::string(name), std::move(*value));
        }
    };
    std::ranges::for_each(platform::base_environment(), copy);
    if (std::ranges::contains(manifest.permissions, "network")) {
        std::ranges::for_each(platform::proxy_environment(), copy);
    }
    std::ranges::for_each(manifest.env, copy);
    env["AVENOIR_MODULE_ID"] = manifest.id;
    env["AVENOIR_VERSION"] = std::string(core::version());
    if (data) {
        env["AVENOIR_MODULE_DATA"] = data->string();
    }
    return env;
}

/// Log du stderr d'un module, borné en taille ; jeté si aucun dossier.
std::shared_ptr<spdlog::logger> module_logger(const std::string& id,
                                              const std::optional<std::filesystem::path>& root) {
    const std::string name = "module." + id;
    if (root && create_private_directory(*root)) {
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (*root / (id + ".log")).string(), kMaxLogBytes, 1);
            auto logger = std::make_shared<spdlog::logger>(name, std::move(sink));
            logger->set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");
            logger->set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::info);
            return logger;
        } catch (const std::exception& e) {
            spdlog::warn("log du module {} impossible : {}", id, e.what());
        }
    }
    return std::make_shared<spdlog::logger>(name, std::make_shared<spdlog::sinks::null_sink_mt>());
}

/// Outil annoncé par le module (`tools/list`), validé.
struct ListedTool {
    std::string name;
    std::string description;
    json schema;
};

core::Result<std::vector<ListedTool>> list_tools(RpcClient& client,
                                                 std::chrono::milliseconds timeout,
                                                 std::vector<std::string>& warnings,
                                                 const std::string& id) {
    std::vector<ListedTool> tools;
    std::optional<std::string> cursor;
    for (std::size_t page = 0; page < kMaxToolPages; ++page) {
        json params = json::object();
        if (cursor) {
            params["cursor"] = *cursor;
        }
        auto result = client.request("tools/list", std::move(params), timeout);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        const auto listed = result->find("tools");
        if (!result->is_object() || listed == result->end() || !listed->is_array()) {
            return module_error("réponse tools/list mal formée");
        }
        for (const auto& item : *listed) {
            if (tools.size() >= kMaxTools) {
                warnings.push_back(id + " : plus de " + std::to_string(kMaxTools) +
                                   " outils, les suivants sont ignorés");
                return tools;
            }
            const auto name = item.is_object() ? item.find("name") : item.end();
            if (!item.is_object() || name == item.end() || !name->is_string()) {
                warnings.push_back(id + " : outil sans nom ignoré");
                continue;
            }
            ListedTool tool{.name = name->get<std::string>(), .description = {}, .schema = {}};
            if (const auto text = item.find("description");
                text != item.end() && text->is_string()) {
                tool.description = clean_text(text->get<std::string>(), kMaxDescription, true);
            }
            const auto schema = item.find("inputSchema");
            if (schema == item.end() || !schema->is_object() ||
                schema->value("type", json()) != "object" ||
                schema->dump(-1, ' ', false, json::error_handler_t::replace).size() >
                    kMaxSchemaBytes) {
                warnings.push_back(id + " : outil « " + clean_text(tool.name, 64, false) +
                                   " » ignoré (schéma d'arguments invalide ou trop gros)");
                continue;
            }
            tool.schema = *schema;
            tools.push_back(std::move(tool));
        }
        const auto next = result->find("nextCursor");
        if (next == result->end() || !next->is_string()) {
            return tools;
        }
        cursor = next->get<std::string>();
    }
    return tools;
}

} // namespace

// ─── Risque effectif ────────────────────────────────────────────────────────

RiskDecision resolve_risk(std::string_view qualified_name, std::string_view short_name,
                          std::optional<core::RiskLevel> declared,
                          const core::ModuleSettings* settings,
                          std::span<const RiskOverride> overrides) {
    const core::RiskLevel base = declared.value_or(core::RiskLevel::Sensitive);
    RiskDecision decision{.risk = base, .warning = {}};

    const auto chosen = std::ranges::find(overrides, qualified_name, &RiskOverride::tool);
    if (chosen != overrides.end()) {
        if (chosen->risk >= base || chosen->agreed) {
            decision.risk = chosen->risk;
        } else {
            decision.warning = std::string(qualified_name) + " : baisse du risque sans accord " +
                               "enregistré, ignorée (utiliser /risque)";
        }
    }

    if (settings != nullptr) {
        if (const auto it = settings->risk.find(short_name); it != settings->risk.end()) {
            if (it->second > decision.risk) {
                decision.risk = it->second;
            } else if (it->second < decision.risk) {
                decision.warning = std::string(qualified_name) + " : config.toml ne peut que " +
                                   "relever le risque, baisse ignorée (utiliser /risque)";
            }
        }
    }
    return decision;
}

// ─── Hôte ───────────────────────────────────────────────────────────────────

struct ModuleHost::Entry {
    DiscoveredModule discovered;
    ModuleState state = ModuleState::Disabled;
    std::string message;
    std::shared_ptr<Module> module;
    std::vector<std::shared_ptr<ModuleTool>> tools;
    std::vector<ModuleToolInfo> tool_infos;
};

ModuleHost::ModuleHost(HostOptions options) : options_(std::move(options)) {}
ModuleHost::ModuleHost(ModuleHost&&) noexcept = default;
ModuleHost& ModuleHost::operator=(ModuleHost&&) noexcept = default;
ModuleHost::~ModuleHost() = default;

void ModuleHost::warn(std::string message) {
    spdlog::warn("{}", message);
    warnings_.push_back(std::move(message));
}

std::vector<std::string> ModuleHost::take_warnings() {
    return std::exchange(warnings_, {});
}

void ModuleHost::load(const Discovery& discovery,
                      const std::map<std::string, core::ModuleSettings, std::less<>>& settings,
                      std::span<const RiskOverride> overrides) {
    settings_ = settings;
    overrides_.assign(overrides.begin(), overrides.end());
    for (const auto& [id, value] : settings) {
        const bool known = std::ranges::any_of(
            discovery.modules, [&](const DiscoveredModule& m) { return m.manifest.id == id; });
        if (!known) {
            warn(std::format("[modules.{}] : aucun module « {} » installé", id, id));
        }
    }
    for (const auto& module : discovery.modules) {
        Entry& entry = entries_.emplace_back();
        entry.discovered = module;
        const auto found = settings.find(module.manifest.id);
        const core::ModuleSettings* own = found == settings.end() ? nullptr : &found->second;
        if (!is_enabled(module, own)) {
            entry.state = ModuleState::Disabled;
            continue;
        }
        start(entry, own);
    }
}

void ModuleHost::start(Entry& entry, const core::ModuleSettings* settings) {
    const Manifest& manifest = entry.discovered.manifest;
    const auto fail = [&entry, this](const std::string& message) {
        entry.state = ModuleState::Failed;
        entry.message = message;
        warn("module « " + entry.discovered.manifest.id + " » non chargé : " + message);
    };

    auto program = resolve_program(manifest);
    if (!program) {
        fail(program.error().message);
        return;
    }
    std::optional<std::filesystem::path> data;
    if (options_.data_root) {
        data = *options_.data_root / manifest.id;
        if (!create_private_directory(*data)) {
            fail("dossier de données impossible à créer : " + data->string());
            return;
        }
    }
    std::vector<std::string> arguments = manifest.run;
    arguments.front() = std::move(*program);

    auto client = RpcClient::start(ProcessSpec{
        .arguments = std::move(arguments),
        .working_directory = manifest.directory,
        .environment = module_environment(manifest, data),
        .log = module_logger(manifest.id, options_.log_root),
    });
    if (!client) {
        fail(client.error().message);
        return;
    }

    // Poignée de main MCP.
    auto initialized = (*client)->request(
        "initialize",
        {{"protocolVersion", kMcpProtocolVersion},
         {"capabilities", json::object()},
         {"clientInfo", {{"name", "avenoir"}, {"version", std::string(core::version())}}}},
        options_.handshake_timeout);
    if (!initialized) {
        fail(initialized.error().message);
        return;
    }
    if (!initialized->is_object()) {
        fail("réponse initialize mal formée");
        return;
    }
    if (auto ready = (*client)->notify("notifications/initialized", json::object()); !ready) {
        fail(ready.error().message);
        return;
    }
    auto listed = list_tools(**client, options_.handshake_timeout, warnings_, manifest.id);
    if (!listed) {
        fail(listed.error().message);
        return;
    }

    entry.module = std::make_shared<Module>(manifest, std::move(*client), options_.call_timeout);
    for (auto& tool : *listed) {
        const ToolDeclaration* declared = manifest.find_tool(tool.name);
        // Le manifeste fait foi : un module `avenoir` n'expose que les outils
        // déclarés ; un serveur MCP sans liste expose tout, en `sensitive`.
        const bool accepted =
            declared != nullptr || (manifest.protocol == Protocol::Mcp && manifest.tools.empty());
        if (!accepted) {
            warn(manifest.id + " : outil « " + clean_text(tool.name, 64, false) +
                 " » absent du manifeste, ignoré");
            continue;
        }
        const std::string qualified = qualified_tool_name(manifest.id, tool.name);
        if (!core::is_valid_tool_name(qualified) || tool.name.contains(kToolSeparator)) {
            warn(manifest.id + " : nom d'outil invalide « " + clean_text(tool.name, 64, false) +
                 " », ignoré");
            continue;
        }
        const auto declared_risk = declared != nullptr ? declared->risk : std::nullopt;
        RiskDecision risk = resolve_risk(qualified, tool.name, declared_risk, settings, overrides_);
        if (!risk.warning.empty()) {
            warn(std::move(risk.warning));
        }
        // La description vient du module : elle est signalée comme telle.
        std::string description = "[Module « " + manifest.name + " »] " + tool.description;
        entry.tool_infos.push_back(
            {.name = qualified,
             .risk = risk.risk,
             .declared = declared_risk.value_or(core::RiskLevel::Sensitive)});
        entry.tools.push_back(
            std::make_shared<ModuleTool>(entry.module, tool.name,
                                         core::ToolSpec{.name = qualified,
                                                        .description = std::move(description),
                                                        .input_schema = std::move(tool.schema),
                                                        .risk = risk.risk}));
    }
    for (const auto& declared : manifest.tools) {
        if (std::ranges::none_of(*listed,
                                 [&](const ListedTool& t) { return t.name == declared.name; })) {
            warn(manifest.id + " : l'outil déclaré « " + declared.name +
                 " » n'est pas fourni par le module");
        }
    }
    entry.state = ModuleState::Running;
    spdlog::info("module « {} » chargé : {} outil(s)", manifest.id, entry.tools.size());
}

void ModuleHost::register_tools(core::ToolRegistry& registry) {
    for (const auto& entry : entries_) {
        for (const auto& tool : entry.tools) {
            if (auto added = registry.add(tool); !added) {
                warn(added.error().message);
            }
        }
    }
}

void ModuleHost::register_fast_path(core::FastRouter& router) {
    for (const auto& entry : entries_) {
        if (entry.state != ModuleState::Running) {
            continue;
        }
        const Manifest& manifest = entry.discovered.manifest;
        for (const auto& rule : manifest.fast_path) {
            const std::string tool = qualified_tool_name(manifest.id, rule.tool);
            if (std::ranges::none_of(entry.tool_infos,
                                     [&](const ModuleToolInfo& t) { return t.name == tool; })) {
                continue; // Outil non fourni : règle sans objet.
            }
            if (auto added = router.add_rule(rule.pattern, tool, rule.arg, rule.arguments);
                !added) {
                warn(manifest.id + " : " + added.error().message);
            }
        }
    }
}

std::vector<ModuleCommand> ModuleHost::commands(std::span<const std::string> reserved) {
    std::vector<ModuleCommand> out;
    for (const auto& entry : entries_) {
        if (entry.state != ModuleState::Running) {
            continue;
        }
        const Manifest& manifest = entry.discovered.manifest;
        for (const auto& command : manifest.commands) {
            const std::string tool = qualified_tool_name(manifest.id, command.tool);
            if (std::ranges::none_of(entry.tool_infos,
                                     [&](const ModuleToolInfo& t) { return t.name == tool; })) {
                continue;
            }
            // Un module ne peut ni masquer une commande d'Avenoir ni celle
            // d'un autre module.
            if (std::ranges::contains(reserved, command.name) ||
                std::ranges::contains(out, command.name, &ModuleCommand::name)) {
                warn(manifest.id + " : commande /" + command.name + " déjà prise, ignorée");
                continue;
            }
            out.push_back({.name = command.name,
                           .module = manifest.id,
                           .tool = tool,
                           .arg = command.arg,
                           .description = command.description});
        }
    }
    return out;
}

core::Result<RiskDecision> ModuleHost::refresh_risk(std::string_view tool) {
    for (auto& entry : entries_) {
        for (std::size_t i = 0; i < entry.tools.size(); ++i) {
            ModuleToolInfo& info = entry.tool_infos[i];
            if (info.name != tool) {
                continue;
            }
            const Manifest& manifest = entry.discovered.manifest;
            const std::string short_name =
                info.name.substr(manifest.id.size() + kToolSeparator.size());
            const ToolDeclaration* declared = manifest.find_tool(short_name);
            const auto found = settings_.find(manifest.id);
            RiskDecision decision = resolve_risk(
                info.name, short_name, declared != nullptr ? declared->risk : std::nullopt,
                found == settings_.end() ? nullptr : &found->second, overrides_);
            info.risk = decision.risk;
            entry.tools[i]->set_risk(decision.risk);
            return decision;
        }
    }
    return core::make_error(core::ErrorCode::Config,
                            "« " + std::string(tool) + " » n'est pas un outil de module chargé");
}

core::Result<RiskDecision> ModuleHost::choose_risk(const RiskOverride& choice) {
    if (!tool_info(choice.tool)) {
        return core::make_error(core::ErrorCode::Config,
                                "« " + choice.tool + " » n'est pas un outil de module chargé");
    }
    std::erase_if(overrides_, [&](const RiskOverride& o) { return o.tool == choice.tool; });
    overrides_.push_back(choice);
    return refresh_risk(choice.tool);
}

core::Result<RiskDecision> ModuleHost::clear_risk(std::string_view tool) {
    std::erase_if(overrides_, [&](const RiskOverride& o) { return o.tool == tool; });
    return refresh_risk(tool);
}

std::optional<ModuleToolInfo> ModuleHost::tool_info(std::string_view tool) const {
    for (const auto& entry : entries_) {
        for (const auto& info : entry.tool_infos) {
            if (info.name == tool) {
                return info;
            }
        }
    }
    return std::nullopt;
}

std::vector<ModuleStatus> ModuleHost::statuses() const {
    std::vector<ModuleStatus> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        const Manifest& manifest = entry.discovered.manifest;
        ModuleStatus status{
            .id = manifest.id,
            .name = manifest.name,
            .version = manifest.version,
            .origin = entry.discovered.origin,
            .protocol = manifest.protocol,
            .state = entry.state,
            .message = entry.message,
            .permissions = manifest.permissions,
            .env = manifest.env,
            .tools = entry.tool_infos,
            .directory = manifest.directory,
        };
        if (status.state == ModuleState::Running && entry.module && !entry.module->alive()) {
            status.state = ModuleState::Failed;
            status.message = entry.module->failure();
        }
        out.push_back(std::move(status));
    }
    return out;
}

} // namespace avenoir::modules
