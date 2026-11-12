#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/message.hpp"
#include "avenoir/modules/host.hpp"
#include "avenoir/storage/store.hpp"

#include "prompt.hpp"
#include "settings_catalog.hpp"

namespace avenoir::cli {

/// Phrase exacte exigée pour abaisser le risque d'un outil de module
/// (docs/PROJECT.md § 2.6, décision 21).
inline constexpr std::string_view kAgreementPhrase = "Je sais ce que je fais";

/// Modules dans la CLI : /modules, /risque et commandes des modules.
class ModulesUi {
  public:
    ModulesUi(modules::ModuleHost& host, std::vector<modules::ModuleCommand> commands);

    /// /modules : état de chaque module, ses permissions et ses outils.
    void print_status() const;

    /// /risque [outil niveau] : sans argument, liste les risques ; sinon
    /// relève (librement) ou abaisse (avertissement, disclaimer et phrase
    /// exacte) le risque d'un outil de module, et l'enregistre dans `store`.
    void risk(std::string_view argument, Prompt& prompt, storage::Store* store);

    /// Commande d'un module (`meteo` pour « /meteo »), ou rien.
    [[nodiscard]] const modules::ModuleCommand* find_command(std::string_view name) const;
    /// Appel d'outil d'une commande, avec le texte saisi après elle.
    [[nodiscard]] static core::ToolCall make_call(const modules::ModuleCommand& command,
                                                  std::string_view argument);

    /// « /meteo »… pour la complétion.
    [[nodiscard]] std::vector<std::string> completion_names() const;
    /// Modules découverts et leur origine (pour /reglage modules.<id>.enabled).
    [[nodiscard]] std::vector<KnownModule> known_modules() const;
    /// Lignes d'aide des commandes des modules (vide s'il n'y en a pas).
    [[nodiscard]] std::string help() const;

  private:
    void print_risks() const;

    modules::ModuleHost* host_;
    std::vector<modules::ModuleCommand> commands_;
};

} // namespace avenoir::cli
