#include <array>
#include <csignal>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "platform.hpp"

namespace avenoir::modules::platform {

void ignore_broken_pipe() noexcept {
    static std::once_flag once;
    std::call_once(once, [] {
        struct sigaction current{};
        if (sigaction(SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL) {
            struct sigaction ignore{};
            ignore.sa_handler = SIG_IGN; // NOLINT(cppcoreguidelines-pro-type-union-access)
            sigemptyset(&ignore.sa_mask);
            sigaction(SIGPIPE, &ignore, nullptr);
        }
    });
}

std::optional<std::filesystem::path> executable_directory() {
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return std::nullopt;
    }
    return path.parent_path();
}

std::span<const std::string_view> base_environment() {
    static constexpr std::array<std::string_view, 17> kNames{"PATH",          "HOME",
                                                             "USER",          "LOGNAME",
                                                             "LANG",          "LANGUAGE",
                                                             "LC_ALL",        "LC_CTYPE",
                                                             "LC_MESSAGES",   "LC_NUMERIC",
                                                             "LC_TIME",       "TZ",
                                                             "TMPDIR",        "XDG_RUNTIME_DIR",
                                                             "SSL_CERT_FILE", "SSL_CERT_DIR",
                                                             "XDG_CACHE_HOME"};
    return kNames;
}

std::span<const std::string_view> proxy_environment() {
    static constexpr std::array<std::string_view, 6> kNames{
        "http_proxy", "https_proxy", "no_proxy", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY"};
    return kNames;
}

std::vector<std::filesystem::path> program_candidates(const std::filesystem::path& directory,
                                                      const std::filesystem::path& program) {
    return {directory / program};
}

} // namespace avenoir::modules::platform
