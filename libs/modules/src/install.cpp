#include "avenoir/modules/install.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace avenoir::modules {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kStagingSuffix = ".nouveau";
constexpr std::string_view kUninstallMark = ".desinstaller";
constexpr std::string_view kEraseData = "donnees";

core::Result<void> install_error(std::string message) {
    return core::make_error(core::ErrorCode::Module, std::move(message));
}

bool is_inside(const fs::path& path, const fs::path& root) {
    const auto [end, ignored] = std::ranges::mismatch(root, path);
    return end == root.end();
}

} // namespace

core::Result<InstallPlan> prepare_install(const fs::path& source, const fs::path& user_root,
                                          std::span<const std::string> reserved) {
    std::error_code error;
    const fs::path canonical = fs::canonical(source, error);
    if (error || !fs::is_directory(canonical, error)) {
        return core::make_error(core::ErrorCode::Module,
                                "dossier introuvable : « " + source.string() + " »");
    }
    const fs::path root = fs::weakly_canonical(user_root, error);
    if (!error && is_inside(canonical, root)) {
        return core::make_error(core::ErrorCode::Module,
                                "ce dossier est déjà dans les modules installés");
    }
    auto manifest = load_manifest(canonical);
    if (!manifest) {
        return std::unexpected(std::move(manifest.error()));
    }
    if (std::ranges::contains(reserved, manifest->id)) {
        return core::make_error(core::ErrorCode::Module,
                                "« " + manifest->id +
                                    " » est un module fourni avec Avenoir : il ne se remplace pas");
    }
    InstallPlan plan;
    plan.source = canonical;
    for (fs::recursive_directory_iterator it(canonical, fs::directory_options::none, error), end;
         !error && it != end; it.increment(error)) {
        const auto status = it->symlink_status(error);
        if (error) {
            break;
        }
        if (fs::is_symlink(status)) {
            return core::make_error(core::ErrorCode::Module,
                                    "lien symbolique refusé : « " + it->path().string() + " »");
        }
        if (fs::is_directory(status)) {
            continue;
        }
        if (!fs::is_regular_file(status)) {
            return core::make_error(core::ErrorCode::Module,
                                    "fichier spécial refusé : « " + it->path().string() + " »");
        }
        plan.bytes += it->file_size(error);
        if (++plan.files > kMaxModuleFiles || plan.bytes > kMaxModuleBytes) {
            return core::make_error(core::ErrorCode::Module,
                                    "module trop gros (2000 fichiers, 200 Mio au plus)");
        }
    }
    if (error) {
        return core::make_error(core::ErrorCode::Module, "dossier illisible : " + error.message());
    }
    if (auto installed = load_manifest(user_root / manifest->id)) {
        for (const auto& permission : manifest->permissions) {
            if (!std::ranges::contains(installed->permissions, permission)) {
                plan.added.push_back(permission);
            }
        }
        plan.installed = std::move(*installed);
    } else {
        plan.added = manifest->permissions;
    }
    plan.manifest = std::move(*manifest);
    return plan;
}

core::Result<void> stage_install(const InstallPlan& plan, const fs::path& user_root) {
    std::error_code error;
    const fs::path staging = user_root / ("." + plan.manifest.id + std::string(kStagingSuffix));
    fs::remove_all(staging, error);
    fs::create_directories(staging, error);
    if (error) {
        return install_error("copie impossible : " + error.message());
    }
    // Fichiers et dossiers ordinaires seulement (vérifiés par prepare_install).
    fs::copy(plan.source, staging, fs::copy_options::recursive, error);
    if (error) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return install_error("copie impossible : " + error.message());
    }
    return {};
}

core::Result<void> mark_uninstall(const fs::path& user_root, std::string_view id, bool erase_data) {
    if (!is_valid_module_id(id)) {
        return install_error("identifiant de module invalide");
    }
    const fs::path directory = user_root / std::string(id);
    std::error_code error;
    if (!fs::is_directory(directory, error)) {
        return install_error("module « " + std::string(id) + " » introuvable dans vos modules");
    }
    std::ofstream mark(directory / std::string(kUninstallMark), std::ios::trunc);
    mark << (erase_data ? kEraseData : std::string_view{});
    if (!mark) {
        return install_error("marque de désinstallation impossible à écrire");
    }
    return {};
}

std::vector<std::string> apply_pending(const fs::path& user_root,
                                       const std::optional<fs::path>& data_root) {
    std::vector<std::string> messages;
    std::error_code error;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(user_root, error), end; !error && it != end;
         it.increment(error)) {
        entries.push_back(it->path());
    }
    // Mises en place : « .<id>.nouveau » remplace « <id> ».
    for (const auto& entry : entries) {
        const std::string name = entry.filename().string();
        if (!name.starts_with('.') || !name.ends_with(kStagingSuffix)) {
            continue;
        }
        const std::string id = name.substr(1, name.size() - 1 - kStagingSuffix.size());
        if (!is_valid_module_id(id)) {
            continue;
        }
        const fs::path target = user_root / id;
        std::error_code failed;
        fs::remove_all(target, failed);
        if (!failed) {
            fs::rename(entry, target, failed);
        }
        messages.push_back(failed ? "module « " + id + " » : mise en place impossible (" +
                                        failed.message() + ")"
                                  : "module « " + id + " » installé");
    }
    // Désinstallations marquées (dossiers listés d'abord : on ne supprime
    // pas pendant le parcours).
    entries.clear();
    for (fs::directory_iterator it(user_root, error), end; !error && it != end;
         it.increment(error)) {
        entries.push_back(it->path());
    }
    for (const auto& entry : entries) {
        const fs::path mark = entry / std::string(kUninstallMark);
        std::error_code missing;
        if (!fs::is_directory(entry, missing) || !fs::exists(mark, missing)) {
            continue;
        }
        std::string what;
        {
            std::ifstream file(mark);
            what.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        const std::string id = entry.filename().string();
        std::error_code failed;
        fs::remove_all(entry, failed);
        std::string message =
            failed ? "module « " + id + " » : désinstallation impossible (" + failed.message() + ")"
                   : "module « " + id + " » désinstallé";
        if (!failed && what == kEraseData && data_root && is_valid_module_id(id)) {
            fs::remove_all(*data_root / id, failed);
            message += failed ? ", ses données restent" : ", ses données effacées";
        }
        messages.push_back(std::move(message));
    }
    return messages;
}

} // namespace avenoir::modules
