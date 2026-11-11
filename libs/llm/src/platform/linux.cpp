#include <array>
#include <optional>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../platform.hpp"

namespace avenoir::llm::platform {

std::optional<std::string> default_route_ipv4() {
    // Un socket UDP « connecté » choisit son adresse source d'après la table
    // de routage ; aucun paquet n'est envoyé.
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return std::nullopt;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    ::inet_pton(AF_INET, "192.0.2.1", &target.sin_addr); // TEST-NET-1, jamais joint
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    std::optional<std::string> address;
    // API des sockets : sockaddr_in passé comme sockaddr.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    const bool routed =
        ::connect(fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &size) == 0;
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    if (routed) {
        std::array<char, INET_ADDRSTRLEN> text{};
        if (::inet_ntop(AF_INET, &local.sin_addr, text.data(), text.size()) != nullptr) {
            address = std::string(text.data());
        }
    }
    ::close(fd);
    return address;
}

} // namespace avenoir::llm::platform
