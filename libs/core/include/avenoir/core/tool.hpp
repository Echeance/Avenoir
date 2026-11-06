#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

namespace avenoir::core {

/// Niveau de risque d'un outil. Il détermine, via la `PermissionPolicy`, si
/// l'outil s'exécute librement, après confirmation, ou jamais.
enum class RiskLevel : std::uint8_t {
    ReadOnly,   ///< Aucun effet de bord (heure, infos système…).
    Reversible, ///< Effet de bord annulable (lancer une app, ouvrir une URL…).
    Sensitive,  ///< Destructeur, irréversible ou sensible (supprimer, envoyer, payer…).
};

[[nodiscard]] std::string_view to_string(RiskLevel risk) noexcept;
/// « read_only », « reversible » ou « sensitive ».
[[nodiscard]] std::optional<RiskLevel> risk_level_from_string(std::string_view text) noexcept;

/// Description d'un outil, transmise au LLM.
struct ToolSpec {
    std::string name;        ///< Unique, au format `[a-zA-Z0-9_-]{1,64}` (contrainte des API LLM).
    std::string description; ///< Pour le LLM : ce que fait l'outil, quand l'utiliser.
    nlohmann::json input_schema = nlohmann::json::object(); ///< JSON Schema des arguments.
    RiskLevel risk = RiskLevel::Sensitive;
};

enum class ToolErrorKind : std::uint8_t {
    InvalidArguments,
    Execution,
    Unsupported,
};

struct ToolError {
    ToolErrorKind kind = ToolErrorKind::Execution;
    std::string message;
};

struct ToolOutput;
using ToolResult = std::expected<ToolOutput, ToolError>;

/// Moyen de défaire une action (PROJECT.md § 2.4, « Annulation des actions »).
/// `run` doit **vérifier que l'état n'a pas changé** depuis l'action avant de
/// restaurer, et échouer sinon plutôt que d'écraser quelque chose.
struct UndoAction {
    std::string description;         ///< Lisible : « remettre le volume à 45 % ».
    std::function<ToolResult()> run; ///< Exécute l'annulation.
};

/// Résultat d'un outil : texte court, relu par le LLM et parfois affiché tel quel.
struct ToolOutput {
    std::string content;
    /// Présente seulement si l'outil sait défaire son action de façon sûre.
    std::optional<UndoAction> undo = std::nullopt;
};

/// Message lisible, renvoyé au LLM.
[[nodiscard]] std::string describe(const ToolError& error);

/// Tout ce que l'assistant peut *faire*. Les outils natifs et ceux des
/// modules externes implémentent la même interface.
class Tool {
  public:
    Tool() = default;
    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;
    Tool(Tool&&) = delete;
    Tool& operator=(Tool&&) = delete;
    virtual ~Tool() = default;

    [[nodiscard]] virtual ToolSpec spec() const = 0;
    /// Les arguments viennent du LLM : ils doivent être **validés** ici.
    [[nodiscard]] virtual ToolResult call(const nlohmann::json& arguments) const = 0;
};

/// Désérialise les arguments JSON d'un outil vers une structure typée
/// (qui définit `from_json`, par ex. via `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE`).
template <typename T>
[[nodiscard]] std::expected<T, ToolError> parse_args(const nlohmann::json& arguments) {
    try {
        return arguments.get<T>();
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(
            ToolError{.kind = ToolErrorKind::InvalidArguments, .message = e.what()});
    }
}

/// Registre des outils disponibles, indexés par nom.
class ToolRegistry {
  public:
    /// Ajoute un outil. Refuse un nom invalide ou déjà pris : un module ne
    /// peut pas remplacer silencieusement un outil existant.
    [[nodiscard]] Result<void> add(std::shared_ptr<const Tool> tool);

    [[nodiscard]] std::shared_ptr<const Tool> find(std::string_view name) const;
    [[nodiscard]] std::vector<ToolSpec> specs() const;
    [[nodiscard]] std::size_t size() const noexcept { return tools_.size(); }
    [[nodiscard]] bool empty() const noexcept { return tools_.empty(); }

  private:
    std::map<std::string, std::shared_ptr<const Tool>, std::less<>> tools_;
};

/// Vrai si `name` respecte `[a-zA-Z0-9_-]{1,64}`.
[[nodiscard]] bool is_valid_tool_name(std::string_view name) noexcept;

} // namespace avenoir::core
