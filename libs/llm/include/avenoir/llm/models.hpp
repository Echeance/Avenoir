#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/agent.hpp"
#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/llm.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::llm {

/// Un modèle entre lesquels l'utilisateur peut basculer (docs/PROJECT.md § 2.5).
struct ModelChoice {
    std::string name;
    core::LlmConfig config;
    /// Tourne sur la machine : la conversation n'en sort pas.
    bool local = false;
};

/// Vrai si le modèle est local : `openai_compatible` vers l'adresse de la
/// machine (`localhost`, `127.x.x.x`, `[::1]`), ou déclaré `local = true`
/// (serveur du réseau local). Les autres fournisseurs sont distants.
[[nodiscard]] bool is_local(const core::LlmConfig& config, std::optional<bool> declared);

/// Identifiant du modèle (« ministral-3:8b », « haiku ») ou du fournisseur.
[[nodiscard]] std::string model_id(const core::LlmConfig& config);

/// « leger : ministral-3:8b, local » ; « claude : haiku (Claude Code), distant ».
[[nodiscard]] std::string describe(const ModelChoice& choice);

/// Modèles de la configuration (`[models.<nom>]`), dans l'ordre du fichier.
[[nodiscard]] std::vector<ModelChoice> models_from(const core::AppConfig& config);

using ProviderFactory =
    std::function<core::Result<std::unique_ptr<core::LlmProvider>>(const core::LlmConfig&)>;

/// Change le modèle de l'agent à la demande (commande `/modele`, outil
/// `switch_llm`). Passer à un modèle distant demande toujours confirmation
/// (directive de sécurité n° 17) : la conversation sortirait de la machine.
class ModelSwitcher {
  public:
    /// `models` peut être vide (pas de LLM : seule la voie rapide répond).
    /// `confirmer` : nul = tout passage à un modèle distant est refusé.
    ModelSwitcher(std::vector<ModelChoice> models, core::ConfirmationHandler* confirmer,
                  ProviderFactory factory = {});

    /// Modèle de départ : `preferred` (le dernier utilisé) s'il existe encore,
    /// sinon le premier. Pas de confirmation : c'est un modèle de la
    /// configuration, déjà choisi par l'utilisateur. Renvoie son fournisseur,
    /// nul s'il n'y a aucun modèle.
    [[nodiscard]] core::Result<std::unique_ptr<core::LlmProvider>>
    start(std::optional<std::string_view> preferred);

    /// L'agent dont changer le modèle (à appeler une fois l'agent créé).
    void attach(core::Agent& agent) noexcept { agent_ = &agent; }

    /// Appelée après chaque changement réussi (pour retenir le modèle).
    void on_switch(std::function<void(const ModelChoice&)> callback) {
        on_switch_ = std::move(callback);
    }

    [[nodiscard]] const std::vector<ModelChoice>& models() const noexcept { return models_; }
    /// Modèle actif, nul s'il n'y en a aucun.
    [[nodiscard]] const ModelChoice* active() const noexcept {
        return active_ < models_.size() ? &models_[active_] : nullptr;
    }

    /// Par nom ou par identifiant du modèle, sans casse ni accents
    /// (« Léger » trouve `leger`, « ministral-3:8b » aussi).
    [[nodiscard]] const ModelChoice* find(std::string_view name) const;

    /// Bascule sur `name` : confirmation si le modèle est distant, création du
    /// fournisseur, puis l'agent l'utilise. Renvoie la phrase à afficher.
    [[nodiscard]] core::Result<std::string> switch_to(std::string_view name);

    /// « gemma, leger, claude » : pour les messages d'erreur.
    [[nodiscard]] std::string names() const;

  private:
    std::vector<ModelChoice> models_;
    std::size_t active_ = 0;
    core::ConfirmationHandler* confirmer_;
    ProviderFactory factory_;
    core::Agent* agent_ = nullptr;
    std::function<void(const ModelChoice&)> on_switch_;
};

/// Outil `switch_llm` (réversible, annulable : retour au modèle précédent).
/// Sa description liste les modèles de `switcher`, qui doit lui survivre.
[[nodiscard]] std::shared_ptr<core::Tool> make_switch_llm_tool(ModelSwitcher& switcher);

/// Règles de voie rapide : « passe sur le modèle léger », « utilise le modèle
/// claude »… (le mot « modèle » ou « llm » est exigé : règles strictes).
[[nodiscard]] core::Result<void> register_switch_rules(core::FastRouter& router);

} // namespace avenoir::llm
