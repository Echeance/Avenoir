#pragma once

#include <memory>

#include "avenoir/storage/secrets.hpp"

// Code propre à chaque système (src/platform/<os>.cpp).
namespace avenoir::storage::platform {

/// Trousseau du système, ou rien s'il n'existe pas sur cette machine
/// (programme `secret-tool` absent, système non pris en charge). Un trousseau
/// présent peut encore refuser de répondre (session sans Secret Service) :
/// ses erreurs le disent.
[[nodiscard]] std::unique_ptr<SecretStore> system_keychain();

} // namespace avenoir::storage::platform
