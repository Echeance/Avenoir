#pragma once

// Recherche web et lecture de pages (ROADMAP 2.3, docs/PROJECT.md décision 45).
//
// - Recherche : SearXNG par défaut (instance de la configuration, ou instance
//   locale lancée par Avenoir : searxng.hpp), sinon DuckDuckGo (version
//   HTML), par simple requête HTTP ; aucun navigateur, aucun captcha
//   contourné.
// - Lecture d'une page **en deux temps** : la page est téléchargée et réduite
//   à son texte, puis lue par le modèle **sans outils** (`PageReader`), qui
//   répond à la question ; seule cette réponse entre dans la conversation.
//   Une consigne cachée dans la page n'atteint qu'un modèle qui ne peut rien
//   faire.
// - Jamais d'adresse locale ou privée : l'adresse IP réellement contactée est
//   vérifiée à chaque requête, redirections comprises.

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/tool.hpp"
#include "avenoir/tools/searxng.hpp"

namespace avenoir::tools {

/// Page téléchargée.
struct HttpPage {
    long status = 0;
    std::string content_type; ///< En-tête Content-Type, tel quel.
    std::string body;         ///< Tronqué à la taille maximale.
    std::string final_url;    ///< Après redirections.
};

/// Requête GET. `allow_private` : adresse locale permise (instance SearXNG
/// de la configuration) ; faux pour toute adresse venue du modèle ou du web.
using HttpGet = std::function<core::Result<HttpPage>(const std::string& url, bool allow_private,
                                                     const std::stop_token& stop)>;

/// GET réel (cpr) : 15 s au plus, 5 redirections `http(s)` au plus, 3 Mo au
/// plus, en-tête User-Agent « Avenoir/<version> », adresse contactée vérifiée.
[[nodiscard]] HttpGet default_http_get();

/// Lecture d'un texte par le modèle actif, sans outils ni historique
/// (`core::Agent::ask_aside`).
using PageReader = std::function<core::Result<std::string>(
    std::string_view instructions, std::string text, const std::stop_token& stop)>;

/// Moteur de recherche actif, et ceux qu'on peut choisir : « duckduckgo »
/// toujours, « searxng » si une instance est configurée ou si Avenoir peut
/// en lancer une (`local`).
class SearchEngines {
  public:
    /// `preferred` : moteur de la configuration ; sinon SearXNG s'il est
    /// disponible, sinon DuckDuckGo. `local` (peut être nul) doit leur
    /// survivre ; une instance configurée (`searxng_url`) passe avant.
    SearchEngines(std::optional<std::string> searxng_url, SearxngHost* local,
                  std::optional<std::string> preferred);

    [[nodiscard]] const std::string& active() const noexcept { return active_; }
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] const std::optional<std::string>& searxng_url() const noexcept {
        return searxng_url_;
    }
    /// Instance locale lancée par Avenoir, si aucune n'est configurée.
    [[nodiscard]] SearxngHost* local() const noexcept { return searxng_url_ ? nullptr : local_; }

    /// Change de moteur (nom sans casse). Renvoie la phrase à afficher.
    [[nodiscard]] core::Result<std::string> select(std::string_view name);

    /// Rétablit un moteur retenu au lancement précédent, sans prévenir ;
    /// ignoré s'il n'est plus disponible.
    void restore(std::string_view name);

    /// Appelée après chaque changement réussi (pour le retenir).
    void on_change(std::function<void(const std::string&)> callback) {
        on_change_ = std::move(callback);
    }

    /// « DuckDuckGo », « SearXNG (http://localhost:8888) » ou « SearXNG
    /// (local) ».
    [[nodiscard]] std::string label() const;

  private:
    std::optional<std::string> searxng_url_;
    SearxngHost* local_;
    std::string active_;
    std::function<void(const std::string&)> on_change_;
};

/// Outils `web_search` (lecture seule), `set_search_engine` (réversible,
/// annulable) et, si `reader` est fourni, `read_webpage` (lecture seule).
/// `engines` doit leur survivre. `get` vide : `default_http_get()`.
[[nodiscard]] std::vector<std::shared_ptr<core::Tool>>
make_web_tools(SearchEngines& engines, PageReader reader, HttpGet get = {});

/// Règles de voie rapide : « cherche sur le web X », « utilise searxng pour
/// chercher »…
[[nodiscard]] core::Result<void> register_web_rules(core::FastRouter& router);

namespace detail {

/// Un résultat de recherche.
struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;

    bool operator==(const SearchResult&) const = default;
};

/// Résultats de la page HTML de DuckDuckGo (liens de redirection décodés).
/// Erreur lisible si DuckDuckGo demande de prouver qu'on n'est pas un robot.
[[nodiscard]] core::Result<std::vector<SearchResult>> parse_duckduckgo(std::string_view html);

/// Résultats de l'API JSON de SearXNG (`format=json`).
[[nodiscard]] core::Result<std::vector<SearchResult>> parse_searxng(std::string_view json);

/// Titre et texte principal d'une page HTML : `<main>` ou `<article>` s'il y
/// en a, sinon le corps ; scripts, styles, menus et pieds de page retirés ;
/// entités décodées, espaces resserrés, une ligne par bloc.
struct PageText {
    std::string title;
    std::string text;
};
[[nodiscard]] PageText html_to_text(std::string_view html);

/// Vrai pour une adresse IP publique (IPv4 ou IPv6 texte). Faux pour une
/// adresse locale, privée, de lien local, réservée, ou illisible.
[[nodiscard]] bool is_public_ip(std::string_view ip);

/// Hôte d'une URL `http(s)` déjà validée (sans port ni crochets). Vide si
/// l'URL contient des identifiants (`user@host`), refusés.
[[nodiscard]] std::optional<std::string> url_host(std::string_view url);

/// Vrai si l'hôte est un nom local évident (`localhost`, `*.localhost`,
/// `*.local`, `*.internal`, `*.lan`) ou une adresse IP non publique : refusé
/// avant même de résoudre le nom.
[[nodiscard]] bool is_local_host(std::string_view host);

/// Texte venu du web pour le modèle : caractères de contrôle (sauf saut de
/// ligne) remplacés, marques bidirectionnelles et invisibles retirées, coupé
/// à `max_bytes` sans couper un caractère UTF-8.
[[nodiscard]] std::string clean_web_text(std::string_view text, std::size_t max_bytes);

} // namespace detail

} // namespace avenoir::tools
