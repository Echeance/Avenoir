#include <array>
#include <optional>
#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../platform.hpp"

namespace avenoir::llm::platform {

std::optional<std::string> default_route_ipv4() {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return std::nullopt;
    }
    std::optional<std::string> address;
    // Un socket UDP « connecté » choisit son adresse source d'après la table
    // de routage ; aucun paquet n'est envoyé.
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket != INVALID_SOCKET) {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = ::htons(9);
        ::inet_pton(AF_INET, "192.0.2.1", &target.sin_addr); // TEST-NET-1, jamais joint
        sockaddr_in local{};
        int size = sizeof(local);
        if (::connect(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0 &&
            ::getsockname(socket, reinterpret_cast<sockaddr*>(&local), &size) == 0) {
            std::array<char, INET_ADDRSTRLEN> text{};
            if (::inet_ntop(AF_INET, &local.sin_addr, text.data(), text.size()) != nullptr) {
                address = std::string(text.data());
            }
        }
        ::closesocket(socket);
    }
    ::WSACleanup();
    return address;
}

} // namespace avenoir::llm::platform
