#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/manifest.hpp"

namespace avenoir::modules {

struct HostOptions {
    /// Délai de la poignée de main (`initialize`, `tools/list`).
    std::chrono::milliseconds handshake_timeout{10'000};
    /// Délai maximal d'un appel d'outil ou d'une annulation.
    std::chrono::milliseconds call_timeout{30'000};
    /// Données des modules : chacun reçoit `<data_root>/<id>` (variable
    /// `AVENOIR_MODULE_DATA`). Absent : pas de dossier de données.
    std::optional<std::filesystem::path> data_root;
    /// Logs des modules (leur stderr) : `<log_root>/<id>.log`. Absent : jetés.
    std::optional<std::filesystem::path> log_root;
};

/// Risque d'un outil de module choisi par l'utilisateur depuis Avenoir
/// (commande /risque), enregistré hors de `config.toml`.
struct RiskOverride {
    std::string tool; ///< Nom complet (`id__outil`).
    core::RiskLevel risk = core::RiskLevel::Sensitive;
    /// Vrai si l'utilisateur a saisi « Je sais ce que je fais » : seul un
    /// accord permet de passer **sous** le risque du manifeste.
    bool agreed = false;
};

/// Risque effectif d'un outil de module, et avertissement éventuel.
struct RiskDecision {
    core::RiskLevel risk = core::RiskLevel::Sensitive;
    std::string warning; ///< Vide si rien à signaler.
};

/// Calcule le risque effectif (PROJECT.md § 2.6) :
/// 1. celui du manifeste, `sensitive` s'il est absent ;
/// 2. remplacé par le choix enregistré par /risque s'il est plus élevé, ou
///    plus bas **avec** accord (sans accord : ignoré, avec avertissement) ;
/// 3. relevé par `config.toml` (`[modules.<id>] risk`), qui ne peut
///    qu'augmenter (une baisse y est ignorée, avec avertissement).
[[nodiscard]] RiskDecision resolve_risk(std::string_view qualified_name,
                                        std::string_view short_name,
                                        std::optional<core::RiskLevel> declared,
                                        const core::ModuleSettings* settings,
                                        std::span<const RiskOverride> overrides);

enum class ModuleState : std::uint8_t {
    Disabled, ///< Désactivé par l'utilisateur (ou module utilisateur non activé).
    Running,
    Failed, ///< Lancement ou poignée de main échoué, ou arrêté depuis.
};

struct ModuleToolInfo {
    std::string name; ///< Nom complet.
    core::RiskLevel risk = core::RiskLevel::Sensitive;
    core::RiskLevel declared = core::RiskLevel::Sensitive; ///< Risque du manifeste.
};

/// État d'un module, pour /modules.
struct ModuleStatus {
    std::string id;
    std::string name;
    std::string version;
    Origin origin = Origin::User;
    Protocol protocol = Protocol::Avenoir;
    ModuleState state = ModuleState::Disabled;
    std::string message; ///< Raison d'un échec.
    std::vector<std::string> permissions;
    std::vector<std::string> env;
    std::vector<ModuleToolInfo> tools;
    std::filesystem::path directory;
};

/// Commande d'interface d'un module (`/meteo Lyon`).
struct ModuleCommand {
    std::string name;   ///< Sans « / ».
    std::string module; ///< Identifiant du module.
    std::string tool;   ///< Nom complet de l'outil.
    std::string arg;    ///< Argument rempli par le texte après la commande (peut être vide).
    std::string description;
};

class Module;

/// Hôte de modules : lance les modules activés, vérifie ce qu'ils exposent et
/// l'enregistre dans les registres du cœur. Un module est du code tiers : rien
/// de ce qu'il déclare n'est pris pour acquis (directive de sécurité n° 15).
///
/// Les processus des modules vivent tant que leurs outils sont référencés
/// (registre de l'agent) ou que l'hôte existe.
class ModuleHost {
  public:
    explicit ModuleHost(HostOptions options = {});
    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ModuleHost(ModuleHost&&) noexcept;
    ModuleHost& operator=(ModuleHost&&) noexcept;
    ~ModuleHost();

    /// Lance les modules activés (poignée de main, liste des outils). Un
    /// module défaillant est signalé dans `statuses()` sans bloquer les autres.
    void load(const Discovery& discovery,
              const std::map<std::string, core::ModuleSettings, std::less<>>& settings,
              std::span<const RiskOverride> overrides);

    /// Ajoute les outils des modules lancés au registre.
    void register_tools(core::ToolRegistry& registry);
    /// Ajoute les règles de voie rapide des modules (après celles d'Avenoir,
    /// qui restent prioritaires).
    void register_fast_path(core::FastRouter& router);
    /// Commandes des modules. Celles dont le nom est dans `reserved`
    /// (commandes d'Avenoir) ou déjà pris par un autre module sont refusées.
    [[nodiscard]] std::vector<ModuleCommand> commands(std::span<const std::string> reserved);

    /// Applique un choix de risque fait avec /risque (à enregistrer par
    /// l'appelant), immédiatement. Renvoie le risque effectif, qui tient
    /// compte du manifeste, de l'accord et de `config.toml` (`resolve_risk`).
    /// Erreur si l'outil n'est pas un outil de module chargé.
    [[nodiscard]] core::Result<RiskDecision> choose_risk(const RiskOverride& choice);
    /// Oublie le choix de risque d'un outil : retour au manifeste (et à
    /// `config.toml`).
    [[nodiscard]] core::Result<RiskDecision> clear_risk(std::string_view tool);
    /// Informations sur un outil de module chargé, ou rien.
    [[nodiscard]] std::optional<ModuleToolInfo> tool_info(std::string_view tool) const;

    /// État de chaque module découvert (y compris désactivés ou en échec).
    [[nodiscard]] std::vector<ModuleStatus> statuses() const;

    /// Avertissements accumulés (baisse de risque ignorée, outil refusé…),
    /// vidés à la lecture.
    [[nodiscard]] std::vector<std::string> take_warnings();

  private:
    struct Entry;

    void start(Entry& entry, const core::ModuleSettings* settings);
    void warn(std::string message);
    /// Recalcule et applique le risque de l'outil `tool` d'après les choix.
    [[nodiscard]] core::Result<RiskDecision> refresh_risk(std::string_view tool);

    HostOptions options_;
    std::map<std::string, core::ModuleSettings, std::less<>> settings_;
    std::vector<RiskOverride> overrides_;
    std::vector<Entry> entries_;
    std::vector<std::string> warnings_;
};

} // namespace avenoir::modules
