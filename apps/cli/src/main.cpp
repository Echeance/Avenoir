// Interface en ligne de commande d'Avenoir.
//
// Pour l'instant, affiche seulement la version : la boucle de conversation
// arrive avec le jalon 1.7 (voir docs/ROADMAP.md).

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>

#include "avenoir/core/version.hpp"

namespace {

/// Écrit une erreur sur stderr sans pouvoir lever d'exception. Un échec
/// d'écriture est ignoré : il n'y a plus rien à faire à ce stade.
void report_error(const char* message) noexcept {
    (void)std::fputs("Erreur : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

} // namespace

int main() {
    // Seul endroit où les exceptions sont tolérées (PROJECT.md § 4.2) :
    // elles sont toutes arrêtées ici.
    try {
        std::println("avenoir {}", avenoir::core::version());
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        report_error(e.what());
    } catch (...) {
        report_error("exception inconnue");
    }
    return EXIT_FAILURE;
}
