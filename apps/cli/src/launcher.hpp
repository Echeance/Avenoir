#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/ipc/transport.hpp"

// Lancement du démon par la CLI quand il ne répond pas (décision 53), et
// d'une commande dans le terminal de la CLI (installation de swaylock).
namespace avenoir::cli {

/// Lance `avenoird` (à côté de cet exécutable) en arrière-plan, détaché de
/// ce terminal (nouvelle session, entrées et sorties fermées), avec
/// `arguments`. Ne l'attend pas.
[[nodiscard]] core::Result<void> start_daemon(const std::vector<std::string>& arguments);

/// Adresse du démon : celle de l'utilisateur, ou un démon privé pour une
/// configuration donnée en argument (adresse propre à son chemin).
[[nodiscard]] core::Result<ipc::Endpoint>
daemon_endpoint(const std::optional<std::filesystem::path>& config);

/// Se connecte au démon ; le lance s'il ne répond pas, puis attend qu'il
/// soit prêt. Sert à toutes les interfaces (CLI, hôte des fenêtres).
[[nodiscard]] core::Result<std::unique_ptr<ipc::Connection>>
connect_daemon(const ipc::Endpoint& endpoint, const std::optional<std::filesystem::path>& config);

/// Lance `arguments` (tableau, jamais de shell) dans ce terminal : entrées et
/// sorties héritées, terminal remis en mode ligne avec écho le temps de la
/// commande (sudo y demande le mot de passe), puis rendu tel quel. Attend la
/// fin et rend le code de sortie ; rien si la commande ne peut pas être
/// lancée ou si ce système ne le permet pas.
[[nodiscard]] std::optional<int> run_in_terminal(const std::vector<std::string>& arguments);

/// Lit une ligne dans ce terminal sans l'afficher (clé d'API) ; rien si
/// l'entrée n'est pas un terminal, si elle se ferme ou sur ce système.
[[nodiscard]] std::optional<std::string> read_hidden_line();

/// Copie `text` dans le presse-papiers du système : `wl-copy` (Wayland),
/// sinon `xclip` ou `xsel` (X11) sous Linux, l'API du presse-papiers sous
/// Windows. Faux si aucun moyen n'a marché (la CLI envoie aussi la séquence
/// OSC 52, que beaucoup de terminaux comprennent).
bool copy_to_clipboard(std::string_view text);

} // namespace avenoir::cli
