#pragma once

// Exécution d'un cas : le vrai agent et le vrai fournisseur LLM, mais des
// doublures à la place des outils (rien n'est lancé ni modifié).

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "avenoir/core/llm.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/tool.hpp"

#include "cases.hpp"

namespace avenoir::evals {

/// Doublure d'un outil : même nom, description, schéma et risque que l'outil
/// réel ; renvoie un résultat préparé, ou une erreur préparée.
class DoubleTool final : public core::Tool {
  public:
    /// `result` peut contenir des `{arg}` (voir `fill_template`) ; si
    /// `failure` est présent, l'appel échoue avec ce message.
    DoubleTool(core::ToolSpec spec, std::string result, std::optional<std::string> failure);

    [[nodiscard]] core::ToolSpec spec() const override { return spec_; }
    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& stop) const override;

  private:
    core::ToolSpec spec_;
    std::string result_;
    std::optional<std::string> failure_;
};

struct RunOptions {
    std::string system_prompt;
    std::size_t max_steps = 10;
    core::PermissionPolicy policy; ///< Remplacée par celle du cas s'il en a une.
};

/// Passe un cas avec ce fournisseur (neuf, sans historique) et les outils
/// décrits par `specs`. Pas de voie rapide : tout passe par le LLM.
[[nodiscard]] Transcript run_case(const Case& test, const Suite& suite,
                                  std::span<const core::ToolSpec> specs,
                                  std::unique_ptr<core::LlmProvider> llm,
                                  const RunOptions& options);

} // namespace avenoir::evals
