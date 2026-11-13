#pragma once

#include <optional>
#include <string>

#include "scroll.hpp"

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

/// Souris rapportée à Avenoir (plein écran : la molette fait défiler la
/// conversation) ou rendue au terminal. Linux : mode souris SGR (DECSET
/// 1000 et 1006), la sélection de texte se fait alors avec Maj ; Windows :
/// console en ENABLE_MOUSE_INPUT, sans la sélection rapide.
void set_mouse_reporting(bool on);

/// Touches lues pendant une requête, quand aucune saisie n'est en cours :
/// la molette et PgUp/PgDn font défiler la conversation ; le texte tapé est
/// gardé pour la saisie suivante. L'écho du terminal est coupé (sinon les
/// touches et les séquences de la souris s'afficheraient) ; Ctrl+C garde son
/// effet. Rien n'est lu hors d'un terminal.
class KeyReader {
  public:
    KeyReader();
    ~KeyReader();
    KeyReader(const KeyReader&) = delete;
    KeyReader& operator=(const KeyReader&) = delete;
    KeyReader(KeyReader&&) = delete;
    KeyReader& operator=(KeyReader&&) = delete;

    /// Prochain défilement demandé, sans attendre ; le texte tapé est mis de côté.
    [[nodiscard]] std::optional<ScrollStep> poll();
    /// Texte tapé depuis le début (sans les retours à la ligne).
    [[nodiscard]] std::string take_typed();

  private:
    void parse();

    bool active_ = false;
    std::string pending_; ///< Octets lus, pas encore interprétés.
    std::string typed_;
    std::optional<ScrollStep> step_;
    int idle_polls_ = 0; ///< Lectures sans rien de neuf (Échap seul).
#ifdef _WIN32
    unsigned long saved_mode_ = 0;
#endif
};

} // namespace avenoir::cli
