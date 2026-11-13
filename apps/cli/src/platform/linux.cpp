// Lancement du démon sous Linux : posix_spawn dans une nouvelle session
// (setsid), entrées et sorties sur /dev/null ; commande dans le terminal de
// la CLI (run_in_terminal) ; arguments en tableau, jamais de shell.

#include <cerrno>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
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

std::optional<int> run_in_terminal(const std::vector<std::string>& arguments) {
    if (arguments.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> owned = arguments;
    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (auto& argument : owned) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    // Le terminal peut être en mode caractère sans écho (lecture des touches
    // pendant une requête) : mode ligne avec écho pour la commande.
    termios saved{};
    const bool terminal = isatty(STDIN_FILENO) == 1 && tcgetattr(STDIN_FILENO, &saved) == 0;
    if (terminal) {
        termios line = saved;
        line.c_lflag |= static_cast<tcflag_t>(ICANON | ECHO | ISIG);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &line);
    }
    pid_t pid = 0;
    const int failed = posix_spawnp(&pid, argv.front(), nullptr, nullptr, argv.data(), environ);
    std::optional<int> status;
    if (failed == 0) {
        int raw = 0;
        while (waitpid(pid, &raw, 0) < 0 && errno == EINTR) {
        }
        status = WIFEXITED(raw) ? WEXITSTATUS(raw) : 128;
    }
    if (terminal) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return status;
}

} // namespace avenoir::cli
