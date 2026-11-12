#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include "avenoir/core/error.hpp"

// Transport local entre le démon et ses interfaces (docs/PROJECT.md § 2.1) :
// socket Unix sous Linux, tube nommé sous Windows. Seul l'utilisateur qui a
// lancé le démon peut s'y connecter, et le client vérifie que le démon est
// bien à lui. Messages : une ligne de texte (JSON) par message, bornée.
namespace avenoir::ipc {

/// Taille maximale d'un message (une ligne).
inline constexpr std::size_t kMaxMessageBytes = std::size_t{8} * 1024 * 1024;

/// Adresse du démon : chemin d'un socket Unix, ou nom de tube nommé Windows.
struct Endpoint {
    std::string address;
};

/// Adresse par défaut, propre à l'utilisateur : `$XDG_RUNTIME_DIR/avenoir/
/// daemon.sock` (sinon `/tmp/avenoir-<uid>/`, créé `0700` et vérifié), ou
/// `\\.\pipe\avenoir-<SID>`. Rien si elle ne peut pas être établie sûrement.
[[nodiscard]] core::Result<Endpoint> default_endpoint();

/// Adresse voisine de `base`, propre à `tag` (minuscules, chiffres, « - ») :
/// un démon privé, par exemple pour une configuration de développement.
[[nodiscard]] Endpoint endpoint_variant(const Endpoint& base, std::string_view tag);

/// Connexion établie, dans les deux sens. Lecture et écriture peuvent se faire
/// depuis deux fils différents (un lecteur, des écrivains synchronisés).
class Connection {
  public:
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    virtual ~Connection() = default;

    /// Prochaine ligne (sans le saut de ligne) ; rien quand l'autre côté a
    /// fermé. Erreur `Cancelled` si `stop` est demandé ; erreur si une ligne
    /// dépasse `kMaxMessageBytes`.
    [[nodiscard]] virtual core::Result<std::optional<std::string>>
    read_line(const std::stop_token& stop) = 0;

    /// Écrit `line` suivie d'un saut de ligne (`line` n'en contient pas).
    [[nodiscard]] virtual core::Result<void> write_line(std::string_view line) = 0;

    /// Ferme la connexion : une lecture en cours se termine.
    virtual void close() = 0;
};

/// Point d'écoute du démon.
class Listener {
  public:
    Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;
    virtual ~Listener() = default;

    /// Attend un client de l'utilisateur (les autres sont refusés et notés).
    /// Erreur `Cancelled` si `stop` est demandé.
    [[nodiscard]] virtual core::Result<std::unique_ptr<Connection>>
    accept(const std::stop_token& stop) = 0;
};

/// Écoute à `endpoint`. Erreur si un démon y répond déjà (une seule instance) ;
/// une adresse abandonnée (démon arrêté brutalement) est reprise.
[[nodiscard]] core::Result<std::unique_ptr<Listener>> listen(const Endpoint& endpoint);

/// Se connecte au démon de `endpoint`, après avoir vérifié qu'il appartient
/// au même utilisateur. Erreur si personne n'écoute.
[[nodiscard]] core::Result<std::unique_ptr<Connection>> connect(const Endpoint& endpoint);

} // namespace avenoir::ipc
