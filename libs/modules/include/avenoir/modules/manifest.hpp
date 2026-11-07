#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::modules {

/// Protocole parlé par le module sur stdio (JSON-RPC 2.0, une ligne par message).
enum class Protocol : std::uint8_t {
    Avenoir, ///< MCP + extensions `avenoir/*` (annulation…).
    Mcp,     ///< Serveur MCP existant : outils seulement.
};

[[nodiscard]] std::string_view to_string(Protocol protocol) noexcept;

/// Outil déclaré par le manifeste. Sa description et son schéma viennent du
/// module (`tools/list`) ; son nom et son risque, du manifeste, qui fait foi.
struct ToolDeclaration {
    std::string name;                    ///< Nom court ; exposé en `id__nom`.
    std::optional<core::RiskLevel> risk; ///< Absent : `sensitive`.
    bool is_public = false;              ///< Appelable par les autres modules (plus tard).
};

/// Commande d'interface (`/nom [texte]`) qui appelle un outil du module.
struct CommandDeclaration {
    std::string name;        ///< Sans « / ».
    std::string tool;        ///< Nom court d'un outil du module.
    std::string arg;         ///< Argument rempli par le texte après la commande (peut être vide).
    std::string description; ///< Pour l'aide.
};

/// Règle de voie rapide, limitée aux outils du module.
struct FastPathDeclaration {
    std::string pattern; ///< Expression régulière ECMAScript sur la phrase entière.
    std::string tool;    ///< Nom court d'un outil du module.
    std::string arg;     ///< Argument rempli par le premier groupe capturant (peut être vide).
    nlohmann::json arguments = nlohmann::json::object(); ///< Arguments fixes.
};

enum class TabKind : std::uint8_t {
    Declarative,
    Web, ///< Exige la permission `ui.web`.
};

/// Onglet dans le widget (pris en charge avec l'interface graphique).
struct TabDeclaration {
    std::string id;
    std::string title;
    TabKind kind = TabKind::Declarative;
};

/// Contenu validé d'un `module.toml` (PROJECT.md § 2.6). Il est lu **avant**
/// de lancer le module : rien de ce qu'il déclare n'est exécuté à ce stade.
struct Manifest {
    std::string id; ///< `[a-z0-9-]`, égal au nom du dossier.
    std::string name;
    std::string version;
    std::string description;
    /// Programme et arguments. Le programme est un fichier du dossier du
    /// module, ou un nom nu cherché dans le PATH (serveurs MCP : `npx`…).
    std::vector<std::string> run;
    Protocol protocol = Protocol::Avenoir;
    std::vector<std::string> permissions; ///< Montrées à l'utilisateur.
    std::vector<std::string> env;         ///< Variables d'environnement transmises au module.
    std::vector<ToolDeclaration> tools;
    std::vector<CommandDeclaration> commands;
    std::vector<FastPathDeclaration> fast_path;
    std::vector<TabDeclaration> tabs;
    std::filesystem::path directory; ///< Dossier du module.

    /// Déclaration de l'outil `tool_name` (nom court), ou `nullptr`.
    [[nodiscard]] const ToolDeclaration* find_tool(std::string_view tool_name) const;
};

/// Permissions connues : une permission inconnue rend le manifeste invalide.
/// `network` (réseau), `files.read` / `files.write` (fichiers de
/// l'utilisateur), `ui.web` (onglet web en iframe isolée).
inline constexpr std::array<std::string_view, 4> kKnownPermissions{"network", "files.read",
                                                                   "files.write", "ui.web"};

/// Séparateur entre l'identifiant du module et le nom de l'outil.
inline constexpr std::string_view kToolSeparator = "__";

/// Nom complet d'un outil de module : `id__nom`.
[[nodiscard]] std::string qualified_tool_name(std::string_view module_id, std::string_view tool);

/// Vrai si `id` est un identifiant de module valide : `[a-z0-9][a-z0-9-]{0,31}`.
[[nodiscard]] bool is_valid_module_id(std::string_view id) noexcept;

/// Lit et valide un manifeste. `directory` est le dossier du module : son
/// nom doit être l'identifiant déclaré.
[[nodiscard]] core::Result<Manifest> parse_manifest(std::string_view toml_text,
                                                    const std::filesystem::path& directory);

/// Lit `<directory>/module.toml` puis appelle `parse_manifest`.
[[nodiscard]] core::Result<Manifest> load_manifest(const std::filesystem::path& directory);

} // namespace avenoir::modules
