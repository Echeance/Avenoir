#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

#include "platform.hpp"

namespace avenoir::modules::platform {

void ignore_broken_pipe() noexcept {
    // Pas de SIGPIPE ici : une écriture vers un tube fermé renvoie une erreur.
}

std::optional<std::filesystem::path> executable_directory() {
    // Chemins longs : on agrandit le tampon tant qu'il est trop petit.
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        if (buffer.size() >= 32'768) {
            return std::nullopt;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::span<const std::string_view> base_environment() {
    static constexpr std::array<std::string_view, 17> kNames{"PATH",
                                                             "PATHEXT",
                                                             "SystemRoot",
                                                             "SystemDrive",
                                                             "windir",
                                                             "TEMP",
                                                             "TMP",
                                                             "USERPROFILE",
                                                             "USERNAME",
                                                             "HOMEDRIVE",
                                                             "HOMEPATH",
                                                             "APPDATA",
                                                             "LOCALAPPDATA",
                                                             "ProgramData",
                                                             "ProgramFiles",
                                                             "NUMBER_OF_PROCESSORS",
                                                             "PROCESSOR_ARCHITECTURE"};
    return kNames;
}

std::span<const std::string_view> proxy_environment() {
    static constexpr std::array<std::string_view, 6> kNames{
        "http_proxy", "https_proxy", "no_proxy", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY"};
    return kNames;
}

std::vector<std::filesystem::path> program_candidates(const std::filesystem::path& directory,
                                                      const std::filesystem::path& program) {
    std::vector<std::filesystem::path> out{directory / program};
    if (!program.has_extension()) {
        std::filesystem::path with_extension = directory / program;
        with_extension += ".exe";
        out.push_back(std::move(with_extension));
    }
    return out;
}

} // namespace avenoir::modules::platform
