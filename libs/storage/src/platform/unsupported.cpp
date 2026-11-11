#include <memory>

#include "../platform.hpp"

namespace avenoir::storage::platform {

std::unique_ptr<SecretStore> system_keychain() {
    return nullptr;
}

} // namespace avenoir::storage::platform
