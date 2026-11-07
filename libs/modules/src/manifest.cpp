#include "avenoir/modules/manifest.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

namespace avenoir::modules {

namespace {

using core::Result;

constexpr std::size_t kMaxNameBytes = 64;
constexpr std::size_t kMaxDescriptionBytes = 500;
constexpr std::size_t kMaxPatternBytes = 500;
constexpr std::size_t kMaxCommandName = 32;

std::unexpected<core::Error> manifest_error(std::string_view where, std::string_view what) {
    return core::make_error(core::ErrorCode::Config,
                            "module.toml : " + std::string(where) + " : " + std::string(what));
}

std::string key_path(std::string_view section, std::string_view key) {
    return section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);
}

bool is_lower_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/// `[a-z0-9][a-z0-9-]{0,max-1}` : identifiants de module, de commande, d'onglet.
bool is_slug(std::string_view text, std::size_t max) {
    return !text.empty() && text.size() <= max && is_lower_alnum(text.front()) &&
           std::ranges::all_of(text, [](char c) { return is_lower_alnum(c) || c == '-'; });
}

/// Nom d'argument JSON : `[a-zA-Z_][a-zA-Z0-9_]{0,63}`.
bool is_argument_name(std::string_view text) {
    const auto word = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    return !text.empty() && text.size() <= kMaxNameBytes &&
           (text.front() < '0' || text.front() > '9') && std::ranges::all_of(text, word);
}

/// Nom de variable d'environnement : `[A-Z_][A-Z0-9_]{0,63}`.
bool is_env_name(std::string_view text) {
    const auto allowed = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    return !text.empty() && text.size() <= kMaxNameBytes &&
           (text.front() < '0' || text.front() > '9') && std::ranges::all_of(text, allowed);
}

/// Texte d'interface : non vide, sans caractère de contrôle (séquences
/// d'échappement du terminal), de longueur bornée.
bool is_display_text(std::string_view text, std::size_t max) {
    return !text.empty() && text.size() <= max && std::ranges::none_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7F;
    });
}

/// Première erreur parmi plusieurs résultats, ou rien si tous ont réussi.
template <typename... Results>
std::optional<core::Error> first_error(const Results&... results) {
    std::optional<core::Error> error;
    const auto keep_first = [&error](const auto& result) {
        if (!error && !result) {
            error = result.error();
        }
    };
    (keep_first(results), ...);
    return error;
}

/// Refuse toute clé non prévue : une faute de frappe ne doit pas passer
/// inaperçue dans un fichier qui décide de ce qui s'exécute.
Result<void> only_keys(const toml::table& table, std::string_view section,
                       std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, value] : table) {
        if (!std::ranges::contains(allowed, key.str())) {
            return manifest_error(key_path(section, key.str()), "clé inconnue");
        }
    }
    return {};
}

template <typename T>
Result<std::optional<T>> optional_value(const toml::table& table, std::string_view section,
                                        std::string_view key) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::optional<T>{};
    }
    if (std::optional<T> value = node->value<T>()) {
        return value;
    }
    return manifest_error(key_path(section, key), "type invalide");
}

template <typename T>
Result<T> required_value(const toml::table& table, std::string_view section, std::string_view key) {
    auto value = optional_value<T>(table, section, key);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    if (!value->has_value()) {
        return manifest_error(key_path(section, key), "clé obligatoire absente");
    }
    return std::move(**value);
}

Result<std::vector<std::string>> string_array(const toml::table& table, std::string_view key) {
    std::vector<std::string> out;
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return out;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        return manifest_error(key, "tableau de chaînes attendu");
    }
    for (const auto& item : *array) {
        auto text = item.value<std::string>();
        if (!text) {
            return manifest_error(key, "tableau de chaînes attendu");
        }
        out.push_back(std::move(*text));
    }
    return out;
}

/// Tables d'un tableau de tables (`[[tools]]`…).
Result<std::vector<const toml::table*>> table_array(const toml::table& root, std::string_view key) {
    std::vector<const toml::table*> out;
    const toml::node* node = root.get(key);
    if (node == nullptr) {
        return out;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        return manifest_error(key, "tableau de tables attendu ([[" + std::string(key) + "]])");
    }
    for (const auto& item : *array) {
        const auto* table = item.as_table();
        if (table == nullptr) {
            return manifest_error(key, "tableau de tables attendu ([[" + std::string(key) + "]])");
        }
        out.push_back(table);
    }
    return out;
}

/// Le programme est un nom nu (cherché dans le dossier du module, puis dans
/// le PATH) ou un chemin relatif qui reste dans le dossier du module.
Result<void> validate_program(std::string_view program) {
    if (program.empty() || program.contains('\0')) {
        return manifest_error("run", "programme vide ou invalide");
    }
    const std::filesystem::path path(program);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return manifest_error("run", "chemin absolu interdit : le programme doit être dans le "
                                     "dossier du module ou dans le PATH");
    }
    for (const auto& part : path) {
        if (part == "..") {
            return manifest_error("run", "le programme ne peut pas sortir du dossier du module");
        }
    }
    return {};
}

Result<void> parse_run(const toml::table& root, Manifest& manifest) {
    auto run = string_array(root, "run");
    if (!run) {
        return std::unexpected(std::move(run.error()));
    }
    if (run->empty()) {
        return manifest_error("run", "clé obligatoire absente ou vide (run = [\"programme\", …])");
    }
    if (auto valid = validate_program(run->front()); !valid) {
        return valid;
    }
    for (const auto& arg : *run) {
        if (arg.contains('\0')) {
            return manifest_error("run", "argument invalide");
        }
    }
    manifest.run = std::move(*run);
    return {};
}

Result<void> parse_tools(const toml::table& root, Manifest& manifest) {
    auto tables = table_array(root, "tools");
    if (!tables) {
        return std::unexpected(std::move(tables.error()));
    }
    for (std::size_t i = 0; i < tables->size(); ++i) {
        const toml::table& table = *(*tables)[i];
        const std::string section = "tools[" + std::to_string(i) + "]";
        if (auto keys = only_keys(table, section, {"name", "risk", "public"}); !keys) {
            return keys;
        }
        auto name = required_value<std::string>(table, section, "name");
        auto risk = optional_value<std::string>(table, section, "risk");
        auto is_public = optional_value<bool>(table, section, "public");
        if (auto error = first_error(name, risk, is_public)) {
            return std::unexpected(std::move(*error));
        }
        if (!core::is_valid_tool_name(qualified_tool_name(manifest.id, *name)) ||
            name->contains(kToolSeparator)) {
            return manifest_error(key_path(section, "name"),
                                  "nom invalide « " + *name + " » ([a-zA-Z0-9_-], sans « __ », " +
                                      std::to_string(kMaxNameBytes) +
                                      " caractères au plus avec le préfixe du module)");
        }
        if (manifest.find_tool(*name) != nullptr) {
            return manifest_error(key_path(section, "name"), "outil déclaré deux fois : " + *name);
        }
        ToolDeclaration tool{.name = std::move(*name),
                             .risk = std::nullopt,
                             .is_public = is_public->value_or(false)};
        if (risk->has_value()) {
            tool.risk = core::risk_level_from_string(**risk);
            if (!tool.risk) {
                return manifest_error(key_path(section, "risk"),
                                      "read_only, reversible ou sensitive attendu");
            }
        }
        manifest.tools.push_back(std::move(tool));
    }
    return {};
}

/// Vérifie qu'une commande ou une règle vise un outil **du module**.
Result<void> check_own_tool(const Manifest& manifest, std::string_view section,
                            const std::string& tool) {
    if (manifest.find_tool(tool) == nullptr) {
        return manifest_error(key_path(section, "tool"),
                              "« " + tool + " » n'est pas un outil déclaré dans [[tools]]");
    }
    return {};
}

Result<void> parse_commands(const toml::table& root, Manifest& manifest) {
    auto tables = table_array(root, "commands");
    if (!tables) {
        return std::unexpected(std::move(tables.error()));
    }
    for (std::size_t i = 0; i < tables->size(); ++i) {
        const toml::table& table = *(*tables)[i];
        const std::string section = "commands[" + std::to_string(i) + "]";
        if (auto keys = only_keys(table, section, {"name", "tool", "arg", "description"}); !keys) {
            return keys;
        }
        auto name = required_value<std::string>(table, section, "name");
        auto tool = required_value<std::string>(table, section, "tool");
        auto arg = optional_value<std::string>(table, section, "arg");
        auto description = optional_value<std::string>(table, section, "description");
        if (auto error = first_error(name, tool, arg, description)) {
            return std::unexpected(std::move(*error));
        }
        if (!is_slug(*name, kMaxCommandName)) {
            return manifest_error(key_path(section, "name"),
                                  "nom invalide ([a-z0-9-], sans « / »)");
        }
        if (std::ranges::any_of(manifest.commands,
                                [&](const CommandDeclaration& c) { return c.name == *name; })) {
            return manifest_error(key_path(section, "name"),
                                  "commande déclarée deux fois : " + *name);
        }
        if (auto own = check_own_tool(manifest, section, *tool); !own) {
            return own;
        }
        if (arg->has_value() && !is_argument_name(**arg)) {
            return manifest_error(key_path(section, "arg"), "nom d'argument invalide");
        }
        if (description->has_value() && !is_display_text(**description, kMaxDescriptionBytes)) {
            return manifest_error(key_path(section, "description"), "texte invalide");
        }
        manifest.commands.push_back(CommandDeclaration{
            .name = std::move(*name),
            .tool = std::move(*tool),
            .arg = arg->value_or(""),
            .description = description->value_or(""),
        });
    }
    return {};
}

/// Arguments fixes d'une règle : valeurs simples seulement.
Result<nlohmann::json> fixed_arguments(const toml::table& table, std::string_view section) {
    nlohmann::json out = nlohmann::json::object();
    const toml::node* node = table.get("arguments");
    if (node == nullptr) {
        return out;
    }
    const auto* arguments = node->as_table();
    if (arguments == nullptr) {
        return manifest_error(key_path(section, "arguments"), "table attendue");
    }
    for (const auto& [key, value] : *arguments) {
        const std::string where = key_path(key_path(section, "arguments"), key.str());
        if (!is_argument_name(key.str())) {
            return manifest_error(where, "nom d'argument invalide");
        }
        const std::string name(key.str());
        if (const auto text = value.value_exact<std::string>()) {
            out[name] = *text;
        } else if (const auto integer = value.value_exact<std::int64_t>()) {
            out[name] = *integer;
        } else if (const auto real = value.value_exact<double>()) {
            out[name] = *real;
        } else if (const auto boolean = value.value_exact<bool>()) {
            out[name] = *boolean;
        } else {
            return manifest_error(where, "valeur simple attendue (texte, nombre ou booléen)");
        }
    }
    return out;
}

Result<void> parse_fast_path(const toml::table& root, Manifest& manifest) {
    auto tables = table_array(root, "fast_path");
    if (!tables) {
        return std::unexpected(std::move(tables.error()));
    }
    for (std::size_t i = 0; i < tables->size(); ++i) {
        const toml::table& table = *(*tables)[i];
        const std::string section = "fast_path[" + std::to_string(i) + "]";
        if (auto keys = only_keys(table, section, {"pattern", "tool", "arg", "arguments"}); !keys) {
            return keys;
        }
        auto pattern = required_value<std::string>(table, section, "pattern");
        auto tool = required_value<std::string>(table, section, "tool");
        auto arg = optional_value<std::string>(table, section, "arg");
        auto arguments = fixed_arguments(table, section);
        if (auto error = first_error(pattern, tool, arg, arguments)) {
            return std::unexpected(std::move(*error));
        }
        if (pattern->empty() || pattern->size() > kMaxPatternBytes) {
            return manifest_error(key_path(section, "pattern"), "expression vide ou trop longue");
        }
        try {
            const std::regex check(*pattern, std::regex::ECMAScript | std::regex::icase);
            if (arg->has_value() && check.mark_count() < 1) {
                return manifest_error(key_path(section, "arg"),
                                      "l'expression n'a pas de groupe capturant pour l'argument");
            }
        } catch (const std::regex_error& e) {
            return manifest_error(key_path(section, "pattern"), e.what());
        }
        if (auto own = check_own_tool(manifest, section, *tool); !own) {
            return own;
        }
        if (arg->has_value() && !is_argument_name(**arg)) {
            return manifest_error(key_path(section, "arg"), "nom d'argument invalide");
        }
        manifest.fast_path.push_back(FastPathDeclaration{
            .pattern = std::move(*pattern),
            .tool = std::move(*tool),
            .arg = arg->value_or(""),
            .arguments = std::move(*arguments),
        });
    }
    return {};
}

Result<void> parse_tabs(const toml::table& root, Manifest& manifest) {
    auto tables = table_array(root, "tabs");
    if (!tables) {
        return std::unexpected(std::move(tables.error()));
    }
    for (std::size_t i = 0; i < tables->size(); ++i) {
        const toml::table& table = *(*tables)[i];
        const std::string section = "tabs[" + std::to_string(i) + "]";
        if (auto keys = only_keys(table, section, {"id", "title", "kind"}); !keys) {
            return keys;
        }
        auto id = required_value<std::string>(table, section, "id");
        auto title = required_value<std::string>(table, section, "title");
        auto kind = optional_value<std::string>(table, section, "kind");
        if (auto error = first_error(id, title, kind)) {
            return std::unexpected(std::move(*error));
        }
        if (!is_slug(*id, kMaxCommandName)) {
            return manifest_error(key_path(section, "id"), "identifiant invalide ([a-z0-9-])");
        }
        if (!is_display_text(*title, kMaxNameBytes)) {
            return manifest_error(key_path(section, "title"), "titre invalide");
        }
        TabDeclaration tab{
            .id = std::move(*id), .title = std::move(*title), .kind = TabKind::Declarative};
        const std::string kind_text = kind->value_or("declarative");
        if (kind_text == "web") {
            if (!std::ranges::contains(manifest.permissions, "ui.web")) {
                return manifest_error(key_path(section, "kind"),
                                      "un onglet web exige la permission « ui.web »");
            }
            tab.kind = TabKind::Web;
        } else if (kind_text != "declarative") {
            return manifest_error(key_path(section, "kind"), "declarative ou web attendu");
        }
        manifest.tabs.push_back(std::move(tab));
    }
    return {};
}

Result<void> parse_header(const toml::table& root, const std::filesystem::path& directory,
                          Manifest& manifest) {
    auto id = required_value<std::string>(root, "", "id");
    auto name = required_value<std::string>(root, "", "name");
    auto version = required_value<std::string>(root, "", "version");
    auto description = optional_value<std::string>(root, "", "description");
    auto protocol = optional_value<std::string>(root, "", "protocol");
    if (auto error = first_error(id, name, version, description, protocol)) {
        return std::unexpected(std::move(*error));
    }
    if (!is_valid_module_id(*id)) {
        return manifest_error("id", "identifiant invalide « " + *id + " » ([a-z0-9-], 32 au plus)");
    }
    if (directory.filename().string() != *id) {
        return manifest_error("id", "« " + *id + " » doit être le nom du dossier du module (« " +
                                        directory.filename().string() + " »)");
    }
    if (!is_display_text(*name, kMaxNameBytes)) {
        return manifest_error("name", "nom invalide");
    }
    if (!is_display_text(*version, kMaxCommandName)) {
        return manifest_error("version", "version invalide");
    }
    if (description->has_value() && !is_display_text(**description, kMaxDescriptionBytes)) {
        return manifest_error("description", "texte invalide");
    }
    const std::string protocol_text = protocol->value_or("avenoir");
    if (protocol_text == "avenoir") {
        manifest.protocol = Protocol::Avenoir;
    } else if (protocol_text == "mcp") {
        manifest.protocol = Protocol::Mcp;
    } else {
        return manifest_error("protocol", "avenoir ou mcp attendu");
    }
    manifest.id = std::move(*id);
    manifest.name = std::move(*name);
    manifest.version = std::move(*version);
    manifest.description = description->value_or("");
    return {};
}

Result<void> parse_permissions_and_env(const toml::table& root, Manifest& manifest) {
    auto permissions = string_array(root, "permissions");
    auto env = string_array(root, "env");
    if (auto error = first_error(permissions, env)) {
        return std::unexpected(std::move(*error));
    }
    std::set<std::string, std::less<>> seen;
    for (const auto& permission : *permissions) {
        if (!std::ranges::contains(kKnownPermissions, permission)) {
            return manifest_error("permissions", "permission inconnue « " + permission + " »");
        }
        if (!seen.insert(permission).second) {
            return manifest_error("permissions", "permission répétée « " + permission + " »");
        }
    }
    for (const auto& name : *env) {
        if (!is_env_name(name)) {
            return manifest_error("env", "nom de variable invalide « " + name + " » ([A-Z0-9_])");
        }
        // Réservées à l'hôte : un module ne peut pas les demander.
        if (name.starts_with("AVENOIR_")) {
            return manifest_error("env", "« " + name + " » est réservée à Avenoir");
        }
    }
    manifest.permissions = std::move(*permissions);
    manifest.env = std::move(*env);
    return {};
}

} // namespace

std::string_view to_string(Protocol protocol) noexcept {
    return protocol == Protocol::Mcp ? "mcp" : "avenoir";
}

const ToolDeclaration* Manifest::find_tool(std::string_view tool_name) const {
    const auto it = std::ranges::find(tools, tool_name, &ToolDeclaration::name);
    return it == tools.end() ? nullptr : &*it;
}

std::string qualified_tool_name(std::string_view module_id, std::string_view tool) {
    return std::string(module_id) + std::string(kToolSeparator) + std::string(tool);
}

bool is_valid_module_id(std::string_view id) noexcept {
    // Pas de « -- » ni de tiret final : identifiants lisibles et préfixes
    // d'outils sans ambiguïté.
    return is_slug(id, kMaxCommandName) && id.back() != '-' && !id.contains("--");
}

core::Result<Manifest> parse_manifest(std::string_view toml_text,
                                      const std::filesystem::path& directory) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& e) {
        const auto& begin = e.source().begin;
        return core::make_error(core::ErrorCode::Config,
                                "module.toml illisible (ligne " + std::to_string(begin.line) +
                                    ", colonne " + std::to_string(begin.column) +
                                    ") : " + std::string(e.description()));
    }
    if (auto keys = only_keys(root, "",
                              {"id", "name", "version", "description", "run", "protocol",
                               "permissions", "env", "tools", "commands", "fast_path", "tabs"});
        !keys) {
        return std::unexpected(std::move(keys.error()));
    }

    Manifest manifest;
    manifest.directory = directory;
    if (auto header = parse_header(root, directory, manifest); !header) {
        return std::unexpected(std::move(header.error()));
    }
    // L'ordre compte : les commandes et règles vérifient les outils déclarés,
    // les onglets web la permission « ui.web ».
    for (const auto& step : {parse_run, parse_permissions_and_env, parse_tools, parse_commands,
                             parse_fast_path, parse_tabs}) {
        if (auto parsed = step(root, manifest); !parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
    }
    return manifest;
}

core::Result<Manifest> load_manifest(const std::filesystem::path& directory) {
    const std::filesystem::path path = directory / "module.toml";
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::make_error(core::ErrorCode::Config, "lecture impossible : " + path.string());
    }
    std::ostringstream content;
    content << file.rdbuf();
    return parse_manifest(content.str(), directory);
}

} // namespace avenoir::modules
