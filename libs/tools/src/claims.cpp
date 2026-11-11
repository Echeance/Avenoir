// Phrases par lesquelles le modèle annonce une action des outils natifs
// (décision 44). Première personne, au présent ou au passé, et formes de
// constat (« session verrouillée ») : une négation (« je ne peux pas
// lancer ») ne correspond à aucune. Une fausse alerte ne coûte qu'une
// relance du modèle, jamais une action.

#include "avenoir/core/claims.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avenoir/tools/tools.hpp"

namespace avenoir::tools {

std::vector<core::ClaimRule>
action_claim_rules(const std::function<bool(std::string_view)>& available) {
    std::vector<core::ClaimRule> all{
        {.tools = {"launch_app", "open_url"},
         .phrases = {"j'ai lancé", "j'ai démarré", "j'ai ouvert", "je lance l'application",
                     "je démarre l'application", "j'ouvre l'application", "j'ouvre le site",
                     "j'ouvre la page", "je t'ouvre", "application lancée", "est lancé",
                     "est maintenant ouvert", "page ouverte", "site ouvert"},
         .action = "lancer une application ou ouvrir une page"},
        {.tools = {"set_volume", "set_brightness"},
         .phrases = {"j'ai baissé", "j'ai monté", "j'ai augmenté", "j'ai diminué",
                     "j'ai coupé le son", "j'ai remis le son", "je baisse le", "je baisse la",
                     "je monte le", "j'augmente le", "j'augmente la", "je diminue le",
                     "je diminue la", "je coupe le son", "je remets le son", "son coupé",
                     "volume réglé", "luminosité réglée"},
         .action = "régler le son ou la luminosité"},
        {.tools = {"media_control"},
         .phrases = {"je mets en pause", "je mets la musique en pause", "j'ai mis en pause",
                     "je reprends la lecture", "j'ai repris la lecture", "je relance la lecture",
                     "je passe au morceau", "je passe à la piste", "je reviens au morceau",
                     "j'ai passé au morceau"},
         .action = "contrôler la lecture"},
        {.tools = {"set_dark_mode"},
         .phrases = {"j'active le mode", "j'ai activé le mode", "je désactive le mode",
                     "j'ai désactivé le mode", "je passe en mode", "je suis passé en mode",
                     "mode sombre activé", "mode clair activé"},
         .action = "changer le mode sombre"},
        {.tools = {"lock_screen"},
         .phrases = {"je verrouille", "j'ai verrouillé", "session verrouillée",
                     "ordinateur verrouillé"},
         .action = "verrouiller la session"},
        {.tools = {"power"},
         .phrases = {"j'éteins", "je vais éteindre", "va éteindre", "vais éteindre", "j'ai éteint",
                     "je redémarre", "va redémarrer", "vais redémarrer",
                     "je mets l'ordinateur en veille", "va mettre l'ordinateur en veille",
                     "extinction en cours", "redémarrage en cours", "mise en veille en cours",
                     "confirmez-vous l'extinction", "confirmes-tu l'extinction",
                     "confirmez-vous le redémarrage", "confirmes-tu le redémarrage"},
         .action = "éteindre, redémarrer ou mettre en veille l'ordinateur"},
        {.tools = {"notify"},
         .phrases = {"je t'envoie une notification", "je t'ai envoyé une notification",
                     "notification envoyée", "notification affichée"},
         .action = "afficher une notification"},
        {.tools = {"set_search_engine"},
         .phrases = {"je vais utiliser searxng", "je vais utiliser duckduckgo",
                     "j'utiliserai searxng", "j'utiliserai duckduckgo", "j'utilise maintenant",
                     "je passe à searxng", "je passe sur searxng", "je passe à duckduckgo",
                     "je passe sur duckduckgo", "moteur de recherche changé"},
         .action = "changer le moteur de recherche"},
    };
    std::vector<core::ClaimRule> rules;
    for (auto& rule : all) {
        std::erase_if(rule.tools,
                      [&available](const std::string& tool) { return !available(tool); });
        if (!rule.tools.empty()) {
            rules.push_back(std::move(rule));
        }
    }
    return rules;
}

} // namespace avenoir::tools
