#pragma once

namespace avenoir::cli {

/// Prépare la console : Ctrl+C n'arrête plus le programme mais lève une
/// demande d'interruption (voir `take_interrupt`) ; sous Windows, la console
/// passe en UTF-8.
void install_console();

/// Vrai si Ctrl+C a été pressé depuis le dernier appel (et remet à zéro).
[[nodiscard]] bool take_interrupt() noexcept;

/// Lève une demande d'interruption depuis le code (ex. Ctrl+C capté par la
/// saisie pendant une confirmation).
void raise_interrupt() noexcept;

} // namespace avenoir::cli
