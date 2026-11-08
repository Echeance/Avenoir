#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace avenoir::core {

/// Catégorie d'erreur du cœur de l'assistant.
enum class ErrorCode : std::uint8_t {
    Config,          ///< Configuration invalide (fichier, règle, outil mal formé…).
    Llm,             ///< Échec du fournisseur LLM (réseau, réponse illisible…).
    MaxStepsReached, ///< Trop d'allers-retours LLM ↔ outils sans réponse finale.
    Cancelled,       ///< Requête annulée par l'utilisateur.
    Module,          ///< Échec d'un module externe (lancement, délai, réponse invalide…).
};

/// Erreur renvoyée par les bibliothèques (jamais d'exception, voir docs/PROJECT.md § 4.2).
struct Error {
    ErrorCode code = ErrorCode::Config;
    std::string message;
};

/// Résultat d'une opération qui peut échouer.
template <typename T>
using Result = std::expected<T, Error>;

/// Raccourci : `return make_error(ErrorCode::Config, "…");`
[[nodiscard]] inline std::unexpected<Error> make_error(ErrorCode code, std::string message) {
    return std::unexpected(Error{.code = code, .message = std::move(message)});
}

/// Message lisible par l'utilisateur, préfixé par la catégorie.
[[nodiscard]] std::string describe(const Error& error);

} // namespace avenoir::core
