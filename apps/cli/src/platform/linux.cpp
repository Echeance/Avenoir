// Lancement du démon sous Linux : posix_spawn dans une nouvelle session
// (setsid), entrées et sorties sur /dev/null ; arguments en tableau, jamais
// de shell.

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <unistd.h>

#include "avenoir/core/error.hpp"

#include "../launcher.hpp"

// Environnement transmis au démon tel quel (variable de la bibliothèque C).
extern char**
    environ; // NOLINT(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables)

namespace avenoir::cli {

core::Result<void> start_daemon(const std::vector<std::string>& arguments) {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return core::make_error(core::ErrorCode::Ipc,
                                "exécutable introuvable : " + error.message());
    }
    const std::string program = (self.parent_path() / "avenoird").string();
    if (!std::filesystem::is_regular_file(program, error)) {
        return core::make_error(core::ErrorCode::Ipc, "démon introuvable : " + program);
    }
    std::vector<std::string> owned{program};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (auto& argument : owned) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    for (const int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        posix_spawn_file_actions_addopen(&actions, fd, "/dev/null",
                                         fd == STDIN_FILENO ? O_RDONLY : O_WRONLY, 0);
    }
    // Nouvelle session : le démon survit à la fermeture du terminal et ne
    // reçoit pas ses Ctrl+C.
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
    pid_t pid = 0;
    const int failed =
        posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (failed != 0) {
        return core::make_error(core::ErrorCode::Ipc, "démon impossible à lancer : " +
                                                          std::system_category().message(failed));
    }
    return {};
}

} // namespace avenoir::cli
