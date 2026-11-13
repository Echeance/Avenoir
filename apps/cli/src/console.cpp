// Code propre à chaque OS pour la console : gestion de Ctrl+C et encodage.
// Le gestionnaire de signal ne fait que lever un indicateur (seule opération
// sûre dans un gestionnaire) ; le thread principal le lit et demande l'arrêt
// de la requête en cours via std::stop_source.

#include "console.hpp"

#include <array>
#include <csignal>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

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

bool stdin_is_terminal() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
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

namespace {

/// Retire le dernier caractère UTF-8 de `text` (Retour arrière).
void pop_utf8(std::string& text) {
    while (!text.empty()) {
        const auto byte = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((byte & 0xC0U) != 0x80U) {
            break;
        }
    }
}

} // namespace

#ifdef _WIN32

namespace {

/// Ajoute à `out` le caractère `code` en UTF-8.
void append_utf8(std::string& out, char32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (code >> 18U));
        out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

} // namespace

void set_mouse_reporting(bool on) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input == INVALID_HANDLE_VALUE || GetConsoleMode(input, &mode) == 0) {
        return;
    }
    // La sélection rapide capterait la souris : coupée pendant le plein écran.
    if (on) {
        mode = (mode | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) & ~DWORD{ENABLE_QUICK_EDIT_MODE};
    } else {
        mode = (mode & ~DWORD{ENABLE_MOUSE_INPUT}) | ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE;
    }
    SetConsoleMode(input, mode);
}

KeyReader::KeyReader() {
    active_ = stdin_is_terminal();
}

KeyReader::~KeyReader() = default;

std::optional<ScrollStep> KeyReader::poll() {
    if (!active_) {
        return std::nullopt;
    }
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD count = 0;
    while (GetNumberOfConsoleInputEvents(input, &count) != 0 && count > 0) {
        INPUT_RECORD record{};
        DWORD read = 0;
        if (ReadConsoleInputW(input, &record, 1, &read) == 0 || read == 0) {
            break;
        }
        if (record.EventType == MOUSE_EVENT &&
            record.Event.MouseEvent.dwEventFlags == MOUSE_WHEELED) {
            const auto delta = static_cast<short>(HIWORD(record.Event.MouseEvent.dwButtonState));
            return delta > 0 ? ScrollStep::WheelUp : ScrollStep::WheelDown;
        }
        if (record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == 0) {
            continue;
        }
        const auto& key = record.Event.KeyEvent;
        const bool ctrl = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        switch (key.wVirtualKeyCode) {
        case VK_PRIOR:
            return ScrollStep::PageUp;
        case VK_NEXT:
            return ScrollStep::PageDown;
        case VK_UP:
            if (ctrl) {
                return ScrollStep::LineUp;
            }
            continue;
        case VK_DOWN:
            if (ctrl) {
                return ScrollStep::LineDown;
            }
            continue;
        case VK_BACK:
            pop_utf8(typed_);
            continue;
        default:
            break;
        }
        const wchar_t unit = key.uChar.UnicodeChar;
        // Caractères imprimables du plan de base (hors moitiés de paires).
        if (unit >= 0x20 && unit != 0x7F && (unit < 0xD800 || unit > 0xDFFF)) {
            append_utf8(typed_, static_cast<char32_t>(unit));
        }
    }
    return std::nullopt;
}

void KeyReader::parse() {}

#else

void set_mouse_reporting(bool on) {
    // Boutons et molette (1000), coordonnées en mode SGR (1006).
    const std::string_view sequence = on ? "\x1b[?1000h\x1b[?1006h" : "\x1b[?1006l\x1b[?1000l";
    (void)std::fwrite(sequence.data(), 1, sequence.size(), stdout);
    (void)std::fflush(stdout);
}

namespace {

/// Réglages du terminal avant le lecteur, rétablis à sa destruction.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) : un seul lecteur à la fois
termios g_saved_termios{};

} // namespace

KeyReader::KeyReader() {
    if (!stdin_is_terminal() || tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
        return;
    }
    termios raw = g_saved_termios;
    // Ni écho ni lecture par ligne ; Ctrl+C reste un signal (ISIG gardé).
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 0; // lecture sans attente
    raw.c_cc[VTIME] = 0;
    active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
}

KeyReader::~KeyReader() {
    if (active_) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
}

std::optional<ScrollStep> KeyReader::poll() {
    if (!active_) {
        return std::nullopt;
    }
    std::array<char, 256> buffer{};
    const auto count = read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count > 0) {
        pending_.append(buffer.data(), static_cast<std::size_t>(count));
        idle_polls_ = 0;
    } else if (!pending_.empty() && ++idle_polls_ > 3) {
        pending_.clear(); // Échap seul, ou séquence tronquée
    }
    step_.reset();
    parse();
    return step_;
}

void KeyReader::parse() {
    // Interprète `pending_` jusqu'au premier défilement ; garde une séquence
    // incomplète pour la lecture suivante.
    while (!pending_.empty() && !step_) {
        const auto byte = static_cast<unsigned char>(pending_.front());
        if (byte != 0x1BU) {
            if (byte == 0x7FU || byte == 0x08U) {
                pop_utf8(typed_);
            } else if (byte >= 0x20U || (byte & 0x80U) != 0) {
                typed_ += pending_.front();
            }
            pending_.erase(0, 1);
            continue;
        }
        if (pending_.size() < 3) {
            return; // séquence pas encore complète
        }
        if (pending_[1] != '[') {
            pending_.erase(0, 2); // ESC O x, Alt+touche : ignorés
            if (!pending_.empty() && pending_.front() != '\x1b') {
                pending_.erase(0, 1);
            }
            continue;
        }
        // CSI : paramètres jusqu'à la lettre ou au « ~ » final.
        std::size_t end = 2;
        while (end < pending_.size() && (pending_[end] < '@' || pending_[end] > '~')) {
            ++end;
        }
        if (end >= pending_.size()) {
            return;
        }
        const std::string sequence = pending_.substr(0, end + 1);
        pending_.erase(0, end + 1);
        if (sequence == "\x1b[5~") {
            step_ = ScrollStep::PageUp;
        } else if (sequence == "\x1b[6~") {
            step_ = ScrollStep::PageDown;
        } else if (sequence == "\x1b[1;5A") {
            step_ = ScrollStep::LineUp;
        } else if (sequence == "\x1b[1;5B") {
            step_ = ScrollStep::LineDown;
        } else if (sequence.starts_with("\x1b[<") && sequence.ends_with('M')) {
            // Souris SGR : « ESC [ < bouton ; x ; y M » ; 64/65 : molette.
            unsigned int button = 0;
            for (const char digit : std::string_view(sequence).substr(3)) {
                if (digit < '0' || digit > '9' || button > 1000) {
                    break;
                }
                button = (button * 10) + static_cast<unsigned int>(digit - '0');
            }
            if ((button & ~0x1CU) == 64) {
                step_ = ScrollStep::WheelUp;
            } else if ((button & ~0x1CU) == 65) {
                step_ = ScrollStep::WheelDown;
            }
        }
    }
}

#endif

std::string KeyReader::take_typed() {
    std::string typed;
    typed.swap(typed_);
    return typed;
}

} // namespace avenoir::cli
