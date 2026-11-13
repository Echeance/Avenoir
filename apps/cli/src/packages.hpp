#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Paquets du système que la CLI accepte d'installer à la demande du démon
// (création guidée, /verrouillage). La liste est fermée et la commande
// construite ici : le démon ne donne qu'un nom, jamais une commande.
namespace avenoir::cli {

/// Vrai si `package` fait partie des paquets installables (« swaylock »).
[[nodiscard]] bool installable_package(std::string_view package);

/// Commande d'installation de `package` par le gestionnaire de paquets
/// trouvé (`available` : programme présent dans le PATH), avec sudo :
/// pacman, apt-get, dnf ou zypper. Rien si le paquet n'est pas installable,
/// ou sans sudo ni gestionnaire connu.
[[nodiscard]] std::optional<std::vector<std::string>>
install_command(std::string_view package, const std::function<bool(std::string_view)>& available);

/// Programme `name` présent dans un dossier du PATH (jamais le dossier courant).
[[nodiscard]] bool executable_in_path(std::string_view name);

} // namespace avenoir::cli
