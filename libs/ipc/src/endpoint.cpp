#include <string>
#include <string_view>

#include "avenoir/ipc/transport.hpp"

namespace avenoir::ipc {

Endpoint endpoint_variant(const Endpoint& base, std::string_view tag) {
    // Socket Unix : « …/daemon.sock » → « …/daemon-<tag>.sock » (même dossier
    // privé) ; tube nommé : « …avenoir-<SID> » → « …avenoir-<SID>-<tag> ».
    constexpr std::string_view kSocketSuffix = ".sock";
    std::string address = base.address;
    if (address.ends_with(kSocketSuffix)) {
        address.insert(address.size() - kSocketSuffix.size(), "-" + std::string(tag));
    } else {
        address += "-" + std::string(tag);
    }
    return Endpoint{.address = std::move(address)};
}

} // namespace avenoir::ipc
