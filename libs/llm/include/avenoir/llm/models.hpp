#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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

/// Charger un modèle local en mémoire (vidéo), l'en décharger, ou les deux.
enum class Residency : std::uint8_t { Load, Unload, Reload };

/// Requête POST JSON vers le serveur d'un modèle (remplaçable dans les tests).
using JsonPoster = std::function<core::Result<nlohmann::json>(
    const std::string& url, const nlohmann::json& body, const std::stop_token& stop)>;

/// Présence d'un modèle dans la mémoire de son serveur.
struct LoadState {
    bool loaded = false;
    std::optional<std::uint64_t> vram_bytes; ///< Mémoire vidéo occupée, si connue.
};

/// Requête GET JSON vers le serveur d'un modèle (remplaçable dans les tests).
using JsonGetter = std::function<core::Result<nlohmann::json>(const std::string& url)>;

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
                  ProviderFactory factory = {}, JsonPoster poster = {}, JsonGetter getter = {});

    /// Modèle de départ : `preferred` (le dernier utilisé) s'il existe encore,
    /// sinon le premier. Pas de confirmation : c'est un modèle de la
    /// configuration, déjà choisi par l'utilisateur. Renvoie son fournisseur,
    /// nul s'il n'y a aucun modèle.
    [[nodiscard]] core::Result<std::unique_ptr<core::LlmProvider>>
    start(std::optional<std::string_view> preferred);

    /// L'agent dont changer le modèle (à appeler une fois l'agent créé).
    void attach(core::Agent& agent) noexcept { agent_ = &agent; }

    /// En changeant de modèle, décharger l'ancien s'il est local, servi par
    /// le **même serveur** que le nouveau et différent de lui (vrai par
    /// défaut) : déchargé à la fin de la requête, comme `/decharger`. Sur un
    /// autre serveur, le décharger ne libérerait rien pour le nouveau.
    void set_unload_on_switch(bool enabled) noexcept { unload_on_switch_ = enabled; }

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

    /// Charge, décharge ou recharge le modèle `name` (vide : le modèle actif)
    /// dans la mémoire de son serveur. Modèles locaux servis par Ollama
    /// seulement (`keep_alive`). Décharger le modèle actif pendant une requête
    /// est **différé** à la fin de celle-ci (`finish_request`) : sa réponse le
    /// rechargerait aussitôt. Renvoie la phrase à afficher.
    [[nodiscard]] core::Result<std::string>
    set_residency(std::string_view name, Residency residency, const std::stop_token& stop);

    /// Change l'effort de raisonnement du modèle `name` (vide : l'actif) :
    /// « aucun », « faible », « moyen », « élevé », ou « défaut » (choix du
    /// serveur) ; les valeurs de l'API (`none`, `low`…) sont acceptées. Le
    /// modèle actif l'applique dès sa prochaine réponse, sans être rechargé
    /// (`reasoning_effort` est un paramètre de chaque requête).
    /// `openai_compatible` seulement. Renvoie la phrase à afficher.
    [[nodiscard]] core::Result<std::string> set_reasoning(std::string_view name,
                                                          std::string_view level);

    /// Effort de raisonnement réglé pour un modèle (vide : choix du serveur).
    [[nodiscard]] static std::optional<std::string> reasoning(const ModelChoice& model);

    /// Rétablit un effort retenu au lancement précédent, sans prévenir ni
    /// créer de fournisseur (à appeler avant `start`). Ignoré s'il est invalide.
    void restore_reasoning(std::string_view name, std::string_view level);

    /// Appelée après chaque changement d'effort réussi (pour le retenir).
    void on_reasoning(std::function<void(const ModelChoice&)> callback) {
        on_reasoning_ = std::move(callback);
    }

    /// Le modèle est-il chargé en mémoire par son serveur (Ollama, `/api/ps`) ?
    /// Vide si on ne peut pas le savoir (modèle distant, autre serveur, serveur
    /// injoignable) : dans le doute, rien n'est annoncé.
    [[nodiscard]] std::optional<bool> is_loaded(const ModelChoice& model) const;
    /// Comme `is_loaded`, avec la mémoire vidéo occupée.
    [[nodiscard]] std::optional<LoadState> load_state(const ModelChoice& model) const;

    /// État de chargement lisible du modèle `name`, ou de tous (vide) :
    /// « gemma : chargé (19,7 Go de mémoire vidéo) », « qwen : pas chargé »…
    [[nodiscard]] core::Result<std::string> status(std::string_view name) const;

    /// À appeler après chaque requête ou commande : exécute les déchargements
    /// différés (sauf celui du modèle redevenu actif). Renvoie la phrase à
    /// afficher, ou rien.
    [[nodiscard]] std::optional<std::string> finish_request();

  private:
    std::vector<ModelChoice> models_;
    std::size_t active_ = 0;
    core::ConfirmationHandler* confirmer_;
    ProviderFactory factory_;
    core::Agent* agent_ = nullptr;
    std::function<void(const ModelChoice&)> on_switch_;
    JsonPoster poster_;
    JsonGetter getter_;
    std::vector<std::size_t> pending_unload_; ///< Modèles à décharger après la requête.
    bool unload_on_switch_ = true;

    /// Met un modèle en attente de déchargement (une seule fois).
    void defer_unload(std::size_t index);
    void cancel_unload(std::size_t index);
    std::function<void(const ModelChoice&)> on_reasoning_;

    /// Requête `keep_alive` au serveur Ollama du modèle.
    [[nodiscard]] core::Result<void> send_residency(const ModelChoice& model, bool load,
                                                    const std::stop_token& stop) const;
};

/// Outil `switch_llm` (réversible, annulable : retour au modèle précédent).
/// Sa description liste les modèles de `switcher`, qui doit lui survivre.
[[nodiscard]] std::shared_ptr<core::Tool> make_switch_llm_tool(ModelSwitcher& switcher);

/// Outils `unload_llm` (décharger, « stopper ») et `load_llm` (charger,
/// « allumer », ou recharger) : réversibles, sans annulation promise.
[[nodiscard]] std::vector<std::shared_ptr<core::Tool>>
make_residency_tools(ModelSwitcher& switcher);

/// Règles de voie rapide : « décharge le modèle », « libère la carte
/// graphique », « charge le modèle gemma », « recharge le modèle »…
[[nodiscard]] core::Result<void> register_residency_rules(core::FastRouter& router);

/// Outil `llm_status` (lecture seule) : quels modèles sont chargés en mémoire.
[[nodiscard]] std::shared_ptr<core::Tool> make_status_tool(const ModelSwitcher& switcher);

/// Règles de voie rapide : « quels modèles sont chargés », « le modèle gemma
/// est-il chargé »…
[[nodiscard]] core::Result<void> register_status_rules(core::FastRouter& router);

/// Outil `set_reasoning` (réversible, annulable : retour à l'effort précédent).
[[nodiscard]] std::shared_ptr<core::Tool> make_reasoning_tool(ModelSwitcher& switcher);

/// Règles de voie rapide : « mets le raisonnement à élevé », « désactive le
/// raisonnement », « active le raisonnement »…
[[nodiscard]] core::Result<void> register_reasoning_rules(core::FastRouter& router);

/// « aucun » pour `none`, « faible », « moyen », « élevé », « par défaut ».
[[nodiscard]] std::string reasoning_label(const std::optional<std::string>& effort);

/// Règles de voie rapide : « passe sur le modèle léger », « utilise le modèle
/// claude »… (le mot « modèle » ou « llm » est exigé : règles strictes).
[[nodiscard]] core::Result<void> register_switch_rules(core::FastRouter& router);

} // namespace avenoir::llm
