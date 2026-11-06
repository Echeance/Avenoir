#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace avenoir::core {

/// Demande d'appel d'outil émise par le LLM.
struct ToolCall {
    std::string id; ///< Identifiant attribué par le fournisseur, renvoyé avec le résultat.
    std::string name;
    nlohmann::json arguments = nlohmann::json::object(); ///< Conforme au schéma de l'outil.

    bool operator==(const ToolCall&) const = default;
};

/// Consommation d'un appel au LLM, quand le fournisseur la communique.
struct Usage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::optional<double> cost_usd = std::nullopt; ///< Coût indicatif (prix publics).

    bool operator==(const Usage&) const = default;
};

struct UserMessage {
    std::string content;

    bool operator==(const UserMessage&) const = default;
};

struct AssistantMessage {
    std::string text;                 ///< Vide si le modèle n'a fait qu'appeler des outils.
    std::vector<ToolCall> tool_calls; ///< Vide si c'est la réponse finale.
    std::optional<Usage> usage = std::nullopt; ///< Consommation de cette réponse, si connue.

    bool operator==(const AssistantMessage&) const = default;
};

struct ToolResultMessage {
    std::string call_id;
    std::string content;
    bool is_error = false;

    bool operator==(const ToolResultMessage&) const = default;
};

/// Un message de l'historique, indépendant du fournisseur LLM. Chaque
/// fournisseur le convertit vers son format. Le prompt système est géré à part.
using Message = std::variant<UserMessage, AssistantMessage, ToolResultMessage>;

} // namespace avenoir::core
