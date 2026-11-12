#include "avenoir/core/error.hpp"

#include <string>
#include <string_view>

namespace avenoir::core {

std::string describe(const Error& error) {
    std::string_view prefix;
    switch (error.code) {
    case ErrorCode::Config:
        prefix = "configuration invalide";
        break;
    case ErrorCode::Llm:
        prefix = "erreur du fournisseur LLM";
        break;
    case ErrorCode::MaxStepsReached:
        prefix = "nombre maximal d'étapes atteint sans réponse finale";
        break;
    case ErrorCode::Cancelled:
        prefix = "requête annulée";
        break;
    case ErrorCode::Module:
        prefix = "erreur de module";
        break;
    case ErrorCode::Ipc:
        prefix = "erreur de communication avec le démon";
        break;
    }
    if (error.message.empty()) {
        return std::string(prefix);
    }
    return std::string(prefix) + " : " + error.message;
}

} // namespace avenoir::core
