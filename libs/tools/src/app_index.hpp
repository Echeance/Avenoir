#pragma once

// Index des applications installées (ROADMAP 2.2) : « ouvre le bloc-notes »,
// « lance le navigateur » trouvent la bonne application, quel que soit le nom
// de son exécutable. Logique portable et testée ; la liste vient de l'OS
// (fichiers .desktop sous Linux, dossier Applications sous Windows).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avenoir::tools {

/// Une application installée.
struct AppEntry {
    /// Ce qui la lance : identifiant .desktop (Linux), nom d'analyse du
    /// dossier Applications ou chemin de l'exécutable (Windows).
    std::string id;
    std::string name;                 ///< Nom affiché, dans la langue du système.
    std::vector<std::string> aliases; ///< Nom générique, mots-clés, exécutable.

    bool operator==(const AppEntry&) const = default;
};

namespace detail {

/// Lit un fichier .desktop ; rien s'il ne décrit pas une application à
/// montrer (`Type` autre qu'Application, `NoDisplay`, `Hidden`, sans nom).
/// `languages` : par ordre de préférence (« fr_FR », « fr »).
[[nodiscard]] std::optional<AppEntry>
parse_desktop_entry(std::string_view text, std::string id,
                    const std::vector<std::string>& languages);

/// Langues de l'utilisateur pour un .desktop, d'après `LC_ALL`, `LC_MESSAGES`
/// ou `LANG` (« fr_FR.UTF-8 » → « fr_FR », « fr »).
[[nodiscard]] std::vector<std::string> desktop_languages(std::string_view locale);

/// Texte comparable : minuscules, sans accents ni ponctuation ni espaces
/// (« Bloc-notes » → « blocnotes »).
[[nodiscard]] std::string normalize_app_name(std::string_view text);

/// Pertinence de `entry` pour la demande (0 : sans rapport ; 100 : nom exact).
[[nodiscard]] int match_score(const AppEntry& entry, std::string_view query);

/// Applications qui correspondent à la demande, les plus pertinentes d'abord.
[[nodiscard]] std::vector<AppEntry> search_apps(const std::vector<AppEntry>& apps,
                                                std::string_view query, std::size_t limit);

/// Résultat d'une recherche pour lancer une application.
struct AppMatch {
    std::optional<AppEntry> best;     ///< La seule bonne réponse, s'il y en a une.
    std::vector<AppEntry> candidates; ///< Sinon : les meilleures à proposer.
};

/// Choisit l'application à lancer : une correspondance claire (nom exact,
/// ou meilleure sans ex æquo), sinon les candidates.
[[nodiscard]] AppMatch pick_app(const std::vector<AppEntry>& apps, std::string_view query);

} // namespace detail

/// Applications installées (lues une fois, puis relues si une demande ne
/// trouve rien : une application vient peut-être d'être installée).
class AppIndex {
  public:
    [[nodiscard]] static std::vector<AppEntry> apps(bool refresh = false);
};

} // namespace avenoir::tools
