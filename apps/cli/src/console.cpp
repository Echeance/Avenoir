// Code propre à chaque OS pour la console : gestion de Ctrl+C et encodage.
// Le gestionnaire de signal ne fait que lever un indicateur (seule opération
// sûre dans un gestionnaire) ; le thread principal le lit et demande l'arrêt
// de la requête en cours via std::stop_source.

#include "console.hpp"

#include <csignal>
#include <filesystem>
#include <optional>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

#include "avenoir/core/env.hpp"

namespace {

// Indicateur partagé avec le gestionnaire de signal : sig_atomic_t volatile
// est le seul type que la norme garantit dans ce contexte, et il doit être
// global pour être visible du gestionnaire.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_interrupt = 0;

} // namespace

#ifdef _WIN32

namespace {

BOOL WINAPI on_console_event(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        g_interrupt = 1;
        return TRUE; // Événement traité : ne pas fermer le programme.
    }
    return FALSE;
}

} // namespace

#else

extern "C" void avenoir_on_sigint(int /*signal*/) {
    g_interrupt = 1;
}

#endif

namespace avenoir::cli {

void install_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(on_console_event, TRUE);
#else
    struct sigaction action{};
    action.sa_handler = avenoir_on_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, nullptr);
#endif
}

bool take_interrupt() noexcept {
    if (g_interrupt != 0) {
        g_interrupt = 0;
        return true;
    }
    return false;
}

void raise_interrupt() noexcept {
    g_interrupt = 1;
}

std::optional<std::filesystem::path> data_directory() {
    std::filesystem::path base;
#ifdef _WIN32
    const auto local = core::read_env("LOCALAPPDATA");
    if (!local) {
        return std::nullopt;
    }
    base = *local;
#else
    if (const auto xdg = core::read_env("XDG_DATA_HOME")) {
        base = *xdg;
    } else if (const auto home = core::read_env("HOME")) {
        base = std::filesystem::path(*home) / ".local" / "share";
    } else {
        return std::nullopt;
    }
#endif
    std::filesystem::path directory = base / "avenoir";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::nullopt;
    }
    return directory;
}

} // namespace avenoir::cli
