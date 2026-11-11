#pragma once

namespace avenoir::cli {

/// Prépare la console : Ctrl+C n'arrête plus le programme mais lève une
/// demande d'interruption (voir `take_interrupt`) ; sous Windows, la console
/// passe en UTF-8.
void install_console();

/// L'entrée standard est-elle un terminal (et non un fichier ou un tube) ?
[[nodiscard]] bool stdin_is_terminal() noexcept;

/// Vrai si Ctrl+C a été pressé depuis le dernier appel (et remet à zéro).
[[nodiscard]] bool take_interrupt() noexcept;

/// Lève une demande d'interruption depuis le code (ex. Ctrl+C capté par la
/// saisie pendant une confirmation).
void raise_interrupt() noexcept;

} // namespace avenoir::cli
