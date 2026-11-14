// Lancement du démon sous Linux : posix_spawn dans une nouvelle session
// (setsid), entrées et sorties sur /dev/null ; commande dans le terminal de
// la CLI (run_in_terminal) ; arguments en tableau, jamais de shell.

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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

std::optional<std::string> read_hidden_line() {
    termios saved{};
    if (isatty(STDIN_FILENO) != 1 || tcgetattr(STDIN_FILENO, &saved) != 0) {
        return std::nullopt;
    }
    // Mode ligne sans écho ; Ctrl+C reste un signal (lecture interrompue).
    termios hidden = saved;
    hidden.c_lflag |= static_cast<tcflag_t>(ICANON | ISIG);
    hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
    std::string line;
    bool complete = false;
    char c = 0;
    while (true) {
        const ssize_t got = read(STDIN_FILENO, &c, 1);
        if (got < 0 && errno == EINTR) {
            break; // Ctrl+C : abandon
        }
        if (got <= 0) {
            break;
        }
        if (c == '\n') {
            complete = true;
            break;
        }
        if (line.size() < 4096) {
            line += c;
        }
    }
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    if (!complete) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

namespace {

/// Lance `arguments` sans shell, `text` sur son entrée ; aucun autre
/// descripteur ne lui est laissé (wl-copy et xclip restent en arrière-plan
/// pour servir le presse-papiers). Vrai si le programme a bien démarré.
bool feed_program(const std::vector<std::string>& arguments, std::string_view text) {
    std::array<int, 2> pipe_ends{};
    if (pipe(pipe_ends.data()) != 0) {
        return false;
    }
    std::vector<std::string> owned = arguments;
    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (auto& argument : owned) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[0], STDIN_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    pid_t pid = 0;
    const int failed = posix_spawnp(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_ends[0]);
    if (failed != 0) {
        close(pipe_ends[1]);
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const std::string_view rest = text.substr(written);
        const ssize_t sent = write(pipe_ends[1], rest.data(), rest.size());
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            break;
        }
        written += static_cast<std::size_t>(sent);
    }
    close(pipe_ends[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return written == text.size() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool in_path(const std::string& name) {
    const char* path = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe) : lecture seule
    if (path == nullptr) {
        return false;
    }
    std::string_view rest(path);
    while (!rest.empty()) {
        const auto colon = rest.find(':');
        const std::filesystem::path candidate = std::filesystem::path(rest.substr(0, colon)) / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return true;
        }
        rest.remove_prefix(colon == std::string_view::npos ? rest.size() : colon + 1);
    }
    return false;
}

} // namespace

bool copy_to_clipboard(std::string_view text) {
    const char* wayland = std::getenv("WAYLAND_DISPLAY"); // NOLINT(concurrency-mt-unsafe)
    if (wayland != nullptr && *wayland != '\0' && in_path("wl-copy")) {
        return feed_program({"wl-copy"}, text);
    }
    if (in_path("xclip")) {
        return feed_program({"xclip", "-selection", "clipboard"}, text);
    }
    if (in_path("xsel")) {
        return feed_program({"xsel", "--clipboard", "--input"}, text);
    }
    return false;
}

} // namespace avenoir::cli
