// Transport sous Windows : tube nommé `\\.\pipe\avenoir-<SID>`, accessible à
// l'utilisateur seul (DACL), clients distants refusés ; l'identité du
// processus à l'autre bout est vérifiée des deux côtés. E/S asynchrones
// (OVERLAPPED) pour pouvoir les interrompre.

#ifndef NOMINMAX
#define NOMINMAX
#endif
// windows.h d'abord : sddl.h en dépend.
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "avenoir/core/error.hpp"
#include "avenoir/ipc/transport.hpp"

namespace avenoir::ipc {

namespace {

constexpr DWORD kPollMilliseconds = 100;
constexpr DWORD kBufferBytes = 64 * 1024;

std::unexpected<core::Error> ipc_error(std::string message) {
    return core::make_error(core::ErrorCode::Ipc, std::move(message));
}

std::string last_error_text() {
    return std::format("code {}", GetLastError());
}

/// Poignée fermée à la destruction.
class Handle {
  public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE));
        }
        return *this;
    }
    ~Handle() { reset(); }
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

  private:
    HANDLE handle_;
};

/// SID (binaire) de l'utilisateur d'un jeton.
std::optional<std::vector<BYTE>> token_user_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size == 0 || GetTokenInformation(token, TokenUser, buffer.data(), size, &size) == FALSE) {
        return std::nullopt;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const DWORD length = GetLengthSid(user->User.Sid);
    const auto* bytes = static_cast<const BYTE*>(user->User.Sid);
    return std::vector<BYTE>(bytes, bytes + length);
}

std::optional<std::vector<BYTE>> current_user_sid() {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return std::nullopt;
    }
    const Handle owned(token);
    return token_user_sid(token);
}

/// Le processus `pid` appartient-il à l'utilisateur courant ?
bool same_user_process(ULONG pid) {
    const Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.valid()) {
        return false;
    }
    HANDLE token = nullptr;
    if (OpenProcessToken(process.get(), TOKEN_QUERY, &token) == FALSE) {
        return false;
    }
    const Handle owned(token);
    const auto theirs = token_user_sid(token);
    const auto ours = current_user_sid();
    return theirs && ours && *theirs == *ours;
}

std::optional<std::wstring> current_user_sid_string() {
    auto sid = current_user_sid();
    if (!sid) {
        return std::nullopt;
    }
    LPWSTR text = nullptr;
    if (ConvertSidToStringSidW(sid->data(), &text) == FALSE) {
        return std::nullopt;
    }
    std::wstring result(text);
    LocalFree(text);
    return result;
}

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

/// Attend la fin d'une opération asynchrone ; l'annule si `stop` est
/// demandé. Faux si annulée ou échouée (`error` dit pourquoi).
bool wait_overlapped(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred,
                     const std::stop_token& stop, DWORD& error,
                     const std::atomic<bool>* closing = nullptr) {
    while (true) {
        const DWORD waited = WaitForSingleObject(overlapped.hEvent, kPollMilliseconds);
        if (waited == WAIT_OBJECT_0) {
            break;
        }
        if (stop.stop_requested() || (closing != nullptr && closing->load())) {
            CancelIoEx(pipe, &overlapped);
            GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
            error = ERROR_OPERATION_ABORTED;
            return false;
        }
    }
    if (GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) == FALSE) {
        error = GetLastError();
        return false;
    }
    return true;
}

class PipeConnection final : public Connection {
  public:
    explicit PipeConnection(Handle pipe) : pipe_(std::move(pipe)) {}

    core::Result<std::optional<std::string>> read_line(const std::stop_token& stop) override {
        {
            const std::scoped_lock lock(state_mutex_);
            if (closed_) {
                return std::optional<std::string>{};
            }
            reading_ = true;
        }
        auto result = read_until_line(stop);
        // Fermée pendant la lecture : c'est au lecteur de libérer le tube.
        const std::scoped_lock lock(state_mutex_);
        reading_ = false;
        if (closed_) {
            release();
        }
        return result;
    }

    core::Result<void> write_line(std::string_view line) override {
        if (line.contains('\n') || line.size() > kMaxMessageBytes) {
            return ipc_error("message invalide (saut de ligne ou trop long)");
        }
        std::string data(line);
        data += '\n';
        const std::scoped_lock lock(write_mutex_);
        if (!pipe_.valid()) {
            return ipc_error("connexion fermée");
        }
        return write_all(data);
    }

    /// Ferme vraiment le tube (l'autre côté lit la fin) ; une lecture en cours
    /// est interrompue et c'est elle qui le libère.
    void close() override {
        const std::scoped_lock lock(state_mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
        closing_ = true;
        if (reading_) {
            CancelIoEx(pipe_.get(), nullptr);
        } else {
            release();
        }
    }

  private:
    /// Appelée verrou d'état pris.
    void release() {
        const std::scoped_lock lock(write_mutex_);
        if (pipe_.valid()) {
            CancelIoEx(pipe_.get(), nullptr);
            pipe_.reset();
        }
    }

    core::Result<std::optional<std::string>> read_until_line(const std::stop_token& stop) {
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
            if (closing_) {
                return std::optional<std::string>{};
            }
            const Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED overlapped{};
            overlapped.hEvent = event.get();
            std::array<char, kBufferBytes> chunk{};
            DWORD count = 0;
            if (ReadFile(pipe_.get(), chunk.data(), kBufferBytes, nullptr, &overlapped) == FALSE &&
                GetLastError() != ERROR_IO_PENDING) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
                    error == ERROR_INVALID_HANDLE) {
                    return std::optional<std::string>{};
                }
                return ipc_error("lecture impossible : " + std::format("code {}", error));
            }
            DWORD error = 0;
            if (!wait_overlapped(pipe_.get(), overlapped, count, stop, error, &closing_)) {
                if (error == ERROR_OPERATION_ABORTED) {
                    if (stop.stop_requested()) {
                        return core::make_error(core::ErrorCode::Cancelled, {});
                    }
                    return std::optional<std::string>{}; // fermé (close)
                }
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                    return std::optional<std::string>{};
                }
                if (error != ERROR_MORE_DATA) {
                    return ipc_error("lecture impossible : " + std::format("code {}", error));
                }
            }
            if (count == 0 && error == 0) {
                continue;
            }
            buffer_.append(chunk.data(), count);
        }
    }

    /// Appelée verrou d'écriture pris.
    core::Result<void> write_all(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED overlapped{};
            overlapped.hEvent = event.get();
            const auto size =
                static_cast<DWORD>(std::min<std::size_t>(data.size() - sent, kBufferBytes));
            if (WriteFile(pipe_.get(), data.data() + sent, size, nullptr, &overlapped) == FALSE &&
                GetLastError() != ERROR_IO_PENDING) {
                return ipc_error("écriture impossible : " + last_error_text());
            }
            DWORD written = 0;
            if (GetOverlappedResult(pipe_.get(), &overlapped, &written, TRUE) == FALSE) {
                return ipc_error("écriture impossible : " + last_error_text());
            }
            sent += written;
        }
        return {};
    }

    Handle pipe_;
    std::mutex state_mutex_; ///< Protège closed_ et reading_.
    bool closed_ = false;
    bool reading_ = false;
    std::atomic<bool> closing_{false}; ///< Lu sans verrou par la boucle de lecture.
    std::string buffer_;
    std::mutex write_mutex_;
};

class PipeListener final : public Listener {
  public:
    PipeListener(std::wstring name, std::wstring sid)
        : name_(std::move(name)), sid_(std::move(sid)) {}

    /// Crée une instance du tube ; la première avec FILE_FLAG_FIRST_PIPE_INSTANCE
    /// (échoue si un autre processus détient déjà ce nom).
    core::Result<Handle> create_instance(bool first) {
        // Accès réservé à l'utilisateur (DACL protégée : rien d'hérité).
        const std::wstring descriptor = L"D:P(A;;GA;;;" + sid_ + L")";
        PSECURITY_DESCRIPTOR security = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                descriptor.c_str(), SDDL_REVISION_1, &security, nullptr) == FALSE) {
            return ipc_error("droits du tube impossibles à établir : " + last_error_text());
        }
        SECURITY_ATTRIBUTES attributes{.nLength = sizeof(SECURITY_ATTRIBUTES),
                                       .lpSecurityDescriptor = security,
                                       .bInheritHandle = FALSE};
        DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (first) {
            open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        Handle pipe(CreateNamedPipeW(
            name_.c_str(), open_mode,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, kBufferBytes, kBufferBytes, 0, &attributes));
        const DWORD error = GetLastError();
        LocalFree(security);
        if (!pipe.valid()) {
            if (first && (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY)) {
                return ipc_error("un démon Avenoir tourne déjà");
            }
            return ipc_error(std::format("tube impossible à créer : code {}", error));
        }
        return pipe;
    }

    void set_next(Handle pipe) { next_ = std::move(pipe); }

    core::Result<std::unique_ptr<Connection>> accept(const std::stop_token& stop) override {
        while (!stop.stop_requested()) {
            if (!next_.valid()) {
                auto created = create_instance(false);
                if (!created) {
                    return std::unexpected(std::move(created.error()));
                }
                next_ = std::move(*created);
            }
            const Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED overlapped{};
            overlapped.hEvent = event.get();
            bool connected = ConnectNamedPipe(next_.get(), &overlapped) != FALSE;
            if (!connected) {
                const DWORD error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED) {
                    connected = true;
                } else if (error == ERROR_IO_PENDING) {
                    DWORD ignored = 0;
                    DWORD wait_error = 0;
                    connected = wait_overlapped(next_.get(), overlapped, ignored, stop, wait_error);
                    if (!connected && stop.stop_requested()) {
                        break;
                    }
                }
            }
            Handle pipe = std::move(next_);
            if (!connected) {
                continue;
            }
            ULONG pid = 0;
            if (GetNamedPipeClientProcessId(pipe.get(), &pid) == FALSE || !same_user_process(pid)) {
                spdlog::warn("connexion au démon refusée : processus {} d'un autre utilisateur",
                             pid);
                DisconnectNamedPipe(pipe.get());
                continue;
            }
            return std::make_unique<PipeConnection>(std::move(pipe));
        }
        return core::make_error(core::ErrorCode::Cancelled, {});
    }

  private:
    std::wstring name_;
    std::wstring sid_;
    Handle next_;
};

} // namespace

core::Result<Endpoint> default_endpoint() {
    const auto sid = current_user_sid_string();
    if (!sid) {
        return ipc_error("utilisateur courant introuvable : " + last_error_text());
    }
    std::string name = "\\\\.\\pipe\\avenoir-";
    for (const wchar_t c : *sid) {
        name += static_cast<char>(c); // SID : ASCII (S-1-5-…)
    }
    return Endpoint{.address = name};
}

core::Result<std::unique_ptr<Listener>> listen(const Endpoint& endpoint) {
    const auto sid = current_user_sid_string();
    if (!sid) {
        return ipc_error("utilisateur courant introuvable : " + last_error_text());
    }
    auto listener = std::make_unique<PipeListener>(widen(endpoint.address), *sid);
    auto first = listener->create_instance(true);
    if (!first) {
        return std::unexpected(std::move(first.error()));
    }
    listener->set_next(std::move(*first));
    return listener;
}

core::Result<std::unique_ptr<Connection>> connect(const Endpoint& endpoint) {
    const std::wstring name = widen(endpoint.address);
    for (int attempt = 0; attempt < 5; ++attempt) {
        // SECURITY_IDENTIFICATION : le démon ne peut pas agir en notre nom.
        Handle pipe(CreateFileW(
            name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
        if (!pipe.valid()) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipeW(name.c_str(), 1000);
                continue;
            }
            return ipc_error(
                std::format("démon injoignable ({}) : {}", endpoint.address, last_error_text()));
        }
        ULONG pid = 0;
        if (GetNamedPipeServerProcessId(pipe.get(), &pid) == FALSE || !same_user_process(pid)) {
            return ipc_error("le démon n'appartient pas à cet utilisateur : connexion refusée");
        }
        return std::make_unique<PipeConnection>(std::move(pipe));
    }
    return ipc_error("démon occupé : " + endpoint.address);
}

} // namespace avenoir::ipc
