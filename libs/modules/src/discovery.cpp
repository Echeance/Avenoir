#include "avenoir/modules/discovery.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "avenoir/core/error.hpp"

#include "platform.hpp"

namespace avenoir::modules {

namespace {

/// Sous-dossiers d'un dossier de modules, triés pour un ordre stable. Les
/// dossiers cachés (« .git »…) sont ignorés.
std::vector<std::filesystem::path> module_directories(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    std::error_code error;
    const std::filesystem::directory_iterator it(root, error);
    if (error) {
        return out;
    }
    for (const auto& entry : it) {
        std::error_code type_error;
        if (!entry.is_directory(type_error) || type_error) {
            continue;
        }
        if (entry.path().filename().string().starts_with('.')) {
            continue;
        }
        out.push_back(entry.path());
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

Discovery discover(std::span<const SearchDirectory> directories) {
    Discovery discovery;
    for (const auto& root : directories) {
        for (const auto& directory : module_directories(root.path)) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(directory / "module.toml", error)) {
                discovery.problems.push_back(
                    {.directory = directory, .message = "pas de module.toml"});
                continue;
            }
            auto manifest = load_manifest(directory);
            if (!manifest) {
                discovery.problems.push_back(
                    {.directory = directory, .message = manifest.error().message});
                continue;
            }
            const bool duplicate =
                std::ranges::any_of(discovery.modules, [&](const DiscoveredModule& known) {
                    return known.manifest.id == manifest->id;
                });
            if (duplicate) {
                discovery.problems.push_back(
                    {.directory = directory,
                     .message = "identifiant « " + manifest->id + " » déjà utilisé : ignoré"});
                continue;
            }
            discovery.modules.push_back({.manifest = std::move(*manifest), .origin = root.origin});
        }
    }
    return discovery;
}

std::vector<SearchDirectory>
default_search_directories(const std::optional<std::filesystem::path>& user_data) {
    std::vector<SearchDirectory> out;
    if (const auto executable = executable_directory()) {
        out.push_back({.path = *executable / "modules", .origin = Origin::Bundled});
    }
    if (user_data) {
        out.push_back({.path = *user_data / "modules", .origin = Origin::User});
    }
    return out;
}

std::optional<std::filesystem::path> executable_directory() {
    return platform::executable_directory();
}

bool is_enabled(const DiscoveredModule& module, const core::ModuleSettings* settings) noexcept {
    if (settings != nullptr && settings->enabled.has_value()) {
        return *settings->enabled;
    }
    return module.origin == Origin::Bundled;
}

} // namespace avenoir::modules
