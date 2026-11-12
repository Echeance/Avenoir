// Transport sous Linux : socket Unix dans un dossier propre à l'utilisateur
// (`0700`), identité du pair vérifiée par SO_PEERCRED des deux côtés.

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "avenoir/core/env.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/ipc/transport.hpp"

namespace avenoir::ipc {

namespace {

constexpr int kPollMilliseconds = 100;
constexpr std::size_t kChunkBytes = std::size_t{64} * 1024;

std::unexpected<core::Error> ipc_error(std::string message) {
    return core::make_error(core::ErrorCode::Ipc, std::move(message));
}

std::string errno_text() {
    return std::system_category().message(errno);
}

/// Descripteur fermé à la destruction.
class FileDescriptor {
  public:
    explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }
    ~FileDescriptor() { reset(); }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }
    [[nodiscard]] int get() const noexcept { return fd_; }

  private:
    int fd_;
};

/// Adresse `sockaddr_un` de `path` ; rien si le chemin est trop long.
std::optional<sockaddr_un> unix_address(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return std::nullopt;
    }
    std::memcpy(&address.sun_path[0], path.data(), path.size());
    return address;
}

/// Identifiant de l'utilisateur à l'autre bout de `fd`.
std::optional<uid_t> peer_uid(int fd) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0) {
        return std::nullopt;
    }
    return credentials.uid;
}

/// Dossier privé `path` : créé `0700` s'il manque ; refusé s'il n'est pas
/// un vrai dossier à l'utilisateur, accessible à lui seul.
core::Result<void> private_directory(const std::filesystem::path& path) {
    if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
        return ipc_error(
            std::format("dossier {} impossible à créer : {}", path.string(), errno_text()));
    }
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() ||
        (info.st_mode & 0077U) != 0) {
        return ipc_error(std::format("dossier {} peu sûr (lien, autre propriétaire ou droits "
                                     "trop larges) : refusé",
                                     path.string()));
    }
    return {};
}

int connect_socket(const sockaddr_un& address) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) : API des sockets
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const int saved = errno;
        ::close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

class SocketConnection final : public Connection {
  public:
    explicit SocketConnection(FileDescriptor fd) : fd_(std::move(fd)) {}

    core::Result<std::optional<std::string>> read_line(const std::stop_token& stop) override {
        while (true) {
            if (const auto end = buffer_.find('\n'); end != std::string::npos) {
                std::string line = buffer_.substr(0, end);
                buffer_.erase(0, end + 1);
                return std::optional<std::string>{std::move(line)};
            }
            if (buffer_.size() > kMaxMessageBytes) {
                return ipc_error("message trop long");
            }
            if (stop.stop_requested()) {
                return core::make_error(core::ErrorCode::Cancelled, {});
            }
            pollfd watched{.fd = fd_.get(), .events = POLLIN, .revents = 0};
            const int ready = ::poll(&watched, 1, kPollMilliseconds);
            if (ready < 0 && errno != EINTR) {
                return ipc_error("lecture impossible : " + errno_text());
            }
            if (ready <= 0) {
                continue;
            }
            std::array<char, kChunkBytes> chunk{};
            const ssize_t count = ::recv(fd_.get(), chunk.data(), chunk.size(), 0);
            if (count < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                if (errno == ECONNRESET || errno == EBADF) {
                    return std::optional<std::string>{};
                }
                return ipc_error("lecture impossible : " + errno_text());
            }
            if (count == 0) {
                return std::optional<std::string>{}; // fermé par l'autre côté
            }
            buffer_.append(chunk.data(), static_cast<std::size_t>(count));
        }
    }

    core::Result<void> write_line(std::string_view line) override {
        if (line.contains('\n') || line.size() > kMaxMessageBytes) {
            return ipc_error("message invalide (saut de ligne ou trop long)");
        }
        std::string data(line);
        data += '\n';
        const std::scoped_lock lock(write_mutex_);
        std::size_t sent = 0;
        while (sent < data.size()) {
            const std::string_view rest = std::string_view(data).substr(sent);
            const ssize_t count = ::send(fd_.get(), rest.data(), rest.size(), MSG_NOSIGNAL);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return ipc_error("écriture impossible : " + errno_text());
            }
            sent += static_cast<std::size_t>(count);
        }
        return {};
    }

    void close() override { ::shutdown(fd_.get(), SHUT_RDWR); }

  private:
    FileDescriptor fd_;
    std::string buffer_;
    std::mutex write_mutex_;
};

class SocketListener final : public Listener {
  public:
    SocketListener(FileDescriptor fd, std::string path)
        : fd_(std::move(fd)), path_(std::move(path)) {}
    SocketListener(const SocketListener&) = delete;
    SocketListener& operator=(const SocketListener&) = delete;
    SocketListener(SocketListener&&) = delete;
    SocketListener& operator=(SocketListener&&) = delete;
    ~SocketListener() override { ::unlink(path_.c_str()); }

    core::Result<std::unique_ptr<Connection>> accept(const std::stop_token& stop) override {
        while (!stop.stop_requested()) {
            pollfd watched{.fd = fd_.get(), .events = POLLIN, .revents = 0};
            const int ready = ::poll(&watched, 1, kPollMilliseconds);
            if (ready < 0 && errno != EINTR) {
                return ipc_error("attente des clients impossible : " + errno_text());
            }
            if (ready <= 0) {
                continue;
            }
            FileDescriptor client(::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
            if (client.get() < 0) {
                continue;
            }
            const auto uid = peer_uid(client.get());
            if (!uid || *uid != ::geteuid()) {
                spdlog::warn("connexion au démon refusée : autre utilisateur (uid {})",
                             uid ? std::to_string(*uid) : std::string("inconnu"));
                continue;
            }
            return std::make_unique<SocketConnection>(std::move(client));
        }
        return core::make_error(core::ErrorCode::Cancelled, {});
    }

  private:
    FileDescriptor fd_;
    std::string path_;
};

} // namespace

core::Result<Endpoint> default_endpoint() {
    std::filesystem::path directory;
    if (const auto runtime = core::read_env("XDG_RUNTIME_DIR");
        runtime && runtime->starts_with('/')) {
        directory = std::filesystem::path(*runtime) / "avenoir";
    } else {
        directory = std::filesystem::path("/tmp") / std::format("avenoir-{}", ::geteuid());
    }
    if (auto made = private_directory(directory); !made) {
        return std::unexpected(std::move(made.error()));
    }
    return Endpoint{.address = (directory / "daemon.sock").string()};
}

core::Result<std::unique_ptr<Listener>> listen(const Endpoint& endpoint) {
    const auto address = unix_address(endpoint.address);
    if (!address) {
        return ipc_error("adresse du démon trop longue : " + endpoint.address);
    }
    if (auto made = private_directory(std::filesystem::path(endpoint.address).parent_path());
        !made) {
        return std::unexpected(std::move(made.error()));
    }
    // Un démon répond déjà : une seule instance. Sinon, adresse abandonnée.
    if (const int existing = connect_socket(*address); existing >= 0) {
        ::close(existing);
        return ipc_error("un démon Avenoir tourne déjà (" + endpoint.address + ")");
    }
    ::unlink(endpoint.address.c_str());

    FileDescriptor fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) {
        return ipc_error("socket impossible : " + errno_text());
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) : API des sockets
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
        return ipc_error(
            std::format("écoute impossible à {} : {}", endpoint.address, errno_text()));
    }
    ::chmod(endpoint.address.c_str(), 0600);
    if (::listen(fd.get(), 16) != 0) {
        ::unlink(endpoint.address.c_str());
        return ipc_error("écoute impossible : " + errno_text());
    }
    return std::make_unique<SocketListener>(std::move(fd), endpoint.address);
}

core::Result<std::unique_ptr<Connection>> connect(const Endpoint& endpoint) {
    const auto address = unix_address(endpoint.address);
    if (!address) {
        return ipc_error("adresse du démon trop longue : " + endpoint.address);
    }
    FileDescriptor fd(connect_socket(*address));
    if (fd.get() < 0) {
        return ipc_error(
            std::format("démon injoignable ({}) : {}", endpoint.address, errno_text()));
    }
    // Le démon doit être à l'utilisateur : un autre aurait pu prendre l'adresse.
    const auto uid = peer_uid(fd.get());
    if (!uid || *uid != ::geteuid()) {
        return ipc_error("le démon à " + endpoint.address +
                         " n'appartient pas à cet utilisateur : connexion refusée");
    }
    return std::make_unique<SocketConnection>(std::move(fd));
}

} // namespace avenoir::ipc
