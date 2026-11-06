#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <replxx.hxx>

namespace avenoir::cli {

/// Saisie de ligne (replxx) : édition, historique conservé entre deux
/// lancements, complétion des commandes, Ctrl+C distingué de Ctrl+D.
class Prompt {
  public:
    enum class Status : unsigned char {
        Line,        ///< Une ligne a été saisie.
        Interrupted, ///< Ctrl+C : saisie abandonnée.
        EndOfInput,  ///< Ctrl+D ou fin de l'entrée.
    };

    struct Input {
        Status status = Status::EndOfInput;
        std::string text;
    };

    /// `commands` : commandes proposées à la complétion (Tab).
    /// `history_file` : fichier d'historique, ou rien pour ne pas le conserver.
    Prompt(std::vector<std::string> commands, std::optional<std::filesystem::path> history_file);

    /// Lit une ligne. `remember` : l'ajouter à l'historique (faux pour les
    /// réponses aux confirmations).
    [[nodiscard]] Input read(const std::string& prompt, bool remember);

    /// Pose une question fermée ; seuls « o », « oui », « y », « yes » valent
    /// oui. Ctrl+C vaut non et lève une demande d'interruption.
    [[nodiscard]] bool ask_yes_no(const std::string& question);

  private:
    replxx::Replxx replxx_;
    std::vector<std::string> commands_;
    std::optional<std::filesystem::path> history_file_;
};

} // namespace avenoir::cli
