#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "platform.hpp"

namespace avenoir::modules::platform {

void ignore_broken_pipe() noexcept {
    // Pas de SIGPIPE ici : une écriture vers un tube fermé renvoie une erreur.
}

std::optional<std::filesystem::path> executable_directory() {
    return std::nullopt;
}

std::span<const std::string_view> base_environment() {
    static constexpr std::array<std::string_view, 4> kNames{"PATH", "HOME", "LANG", "TMPDIR"};
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
