#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"

#include "settings_catalog.hpp"
#include "setup.hpp"

// Ce que fait Avenoir, une fois sa configuration lue : agent, outils, modules,
// modèles, recherche web, conversations, commandes (docs/FONCTIONNEMENT.md).
// Tout ce qui s'affiche passe par l'interface du client de la requête en
// cours (`display()`).
namespace avenoir::daemon {

/// Fichier de configuration utilisé, et son contenu (base de /configurer).
struct ConfigFile {
    std::filesystem::path path;
    core::AppConfig config;
    SaveSettings save; ///< Écriture scellée, ou simple en mode développement.
};

/// Suite à donner après une saisie.
enum class Outcome : unsigned char {
    Continue, ///< Rien de plus.
    Quit,     ///< Le client part (/quitter).
    Restart,  ///< Réglages changés : reconstruire Avenoir (/redemarrer).
    Shutdown, ///< Arrêter le démon (/arreter).
};

class Runtime {
  public:
    /// Construit Avenoir à partir de `file` (modules lancés, modèle de
    /// départ choisi). Les avertissements de démarrage sont gardés pour
    /// l'accueil du premier client.
    [[nodiscard]] static core::Result<std::unique_ptr<Runtime>> create(ConfigFile file);

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    ~Runtime();

    /// Accueil d'un client : cadre, avertissements (au premier seulement),
    /// derniers messages de la conversation.
    void welcome(bool first);

    /// Requête ou commande (« / ») d'un client, dans le fil de la requête.
    [[nodiscard]] Outcome input(std::string_view line, const std::stop_token& stop);

    /// Après chaque saisie : déchargement différé, état des modèles retenu.
    void finish_input();
    /// Modèles chargés ou déchargés hors d'Avenoir depuis la dernière fois.
    [[nodiscard]] std::vector<std::string> model_changes();

    /// Commandes proposées à la complétion (Tab).
    [[nodiscard]] std::vector<std::string> completions() const;
    /// « « titre » (n° 3) », pour l'au revoir du client.
    [[nodiscard]] std::string describe_conversation() const;
    /// Modèle actif, tel que l'accueil le montre (« gemma : gemma4…, local »).
    [[nodiscard]] std::string model_status() const;
    /// Réglages modifiables et leur valeur (menu /settings de la CLI).
    [[nodiscard]] std::vector<SettingLine> settings() const;
    [[nodiscard]] const ConfigFile& config() const;

  private:
    struct State;
    explicit Runtime(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace avenoir::daemon
