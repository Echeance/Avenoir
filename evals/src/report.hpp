#pragma once

#include <string>
#include <vector>

#include "cases.hpp"

namespace avenoir::evals {

/// Un passage d'un cas, et ses manquements (vides : réussi).
struct Attempt {
    Transcript transcript;
    std::vector<std::string> failures;
};

struct CaseResult {
    const Case* test = nullptr;
    std::vector<Attempt> attempts; ///< Un par répétition.

    [[nodiscard]] std::size_t passed() const;
};

struct ReportHeader {
    std::string date;     ///< « 2026-11-07 15:30 »
    std::string model;    ///< « claude_code / sonnet »
    std::string prompt;   ///< « par défaut » ou chemin du fichier
    std::string platform; ///< « Linux », « Windows »
};

/// Rapport Markdown : synthèse, tableau des cas, détail des échecs.
[[nodiscard]] std::string make_report(const ReportHeader& header,
                                      const std::vector<CaseResult>& results);

} // namespace avenoir::evals
