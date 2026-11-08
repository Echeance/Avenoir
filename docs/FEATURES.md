# Fonctionnalités d'Avenoir

Liste **exhaustive** de ce qu'Avenoir sait faire aujourd'hui, mise à jour à chaque fonctionnalité ajoutée. Ce qui est prévu mais pas encore fait est dans [`ROADMAP.md`](ROADMAP.md) ; le pourquoi des choix, dans [`PROJECT.md`](PROJECT.md) ; le fonctionnement du code, dans [`FONCTIONNEMENT.md`](FONCTIONNEMENT.md).

Plateformes : **Linux** et **Windows** (sauf mention contraire).

## Sommaire

1. [Dialogue et fournisseurs LLM](#1-dialogue-et-fournisseurs-llm)
2. [Voie rapide](#2-voie-rapide)
3. [Outils natifs](#3-outils-natifs)
4. [Sécurité et permissions](#4-sécurité-et-permissions)
5. [Interruption et annulation](#5-interruption-et-annulation)
6. [Conversations](#6-conversations)
7. [Modules](#7-modules)
8. [Module météo](#8-module-météo)
9. [Interface en ligne de commande](#9-interface-en-ligne-de-commande)
10. [Configuration](#10-configuration)
11. [Journaux](#11-journaux)
12. [Banc d'essai avec de vrais modèles](#12-banc-dessai-avec-de-vrais-modèles)
13. [Développement, qualité et publication](#13-développement-qualité-et-publication)

## 1. Dialogue et fournisseurs LLM

- Dialogue en langage naturel, en français, avec réponses brèves (prévues pour être lues à voix haute plus tard).
- **Boucle agent** : le LLM choisit des outils, Avenoir les exécute après contrôle des permissions, renvoie les résultats, et recommence jusqu'à la réponse finale ; nombre d'allers-retours limité (`max_steps`, 10 par défaut).
- Plusieurs appels d'outils dans une même réponse ; suite de conversation (« et demain ? ») grâce à l'historique.
- Résultats d'outils tronqués à 20 000 caractères avant d'être renvoyés au LLM (sans couper un caractère UTF-8).
- Réponse vide du modèle signalée par une erreur explicite (la question peut être reposée telle quelle).
- **Prompt système** par défaut ajusté sur le banc d'essai (prompt v3), remplaçable dans la configuration.
- **Trois fournisseurs** :
  - `openai_compatible` : toute API `/chat/completions` (Ollama, llama.cpp server, LM Studio, vLLM, OpenAI, Mistral, OpenRouter…), appels d'outils natifs, arguments acceptés en chaîne JSON ou en objet (Ollama), `temperature` et `reasoning_effort` (`none`, `low`, `medium`, `high`) réglables ;
  - `anthropic` : API Messages, appels d'outils natifs, fusion des messages consécutifs de même rôle ;
  - `claude_code` : abonnement Claude (Pro / Max) via la CLI officielle `claude -p`, sans clé d'API ; outils, plugins, hooks, MCP et réglages de Claude Code désactivés ; outils d'Avenoir décrits dans le prompt, réponse imposée par `--json-schema` ; prompt passé par stdin (taille illimitée) ; clés d'API retirées de l'environnement ; dossier de travail temporaire ; délai réglable.
- Clé d'API lue dans la variable d'environnement dont la configuration donne le **nom** ; jamais citée dans une erreur ni un log.
- Consommation remontée par les trois fournisseurs (tokens en entrée et en sortie, coût indicatif) et conservée avec chaque réponse.
- Annulation d'une requête en cours chez tous les fournisseurs (requête HTTP interrompue, processus `claude` arrêté).
- Modèles recommandés et mesurés : Gemma 4 26B-A4B (local, 24 Go), Ministral 3 8B (local léger), Claude (distant) — PROJECT.md § 2.5.

## 2. Voie rapide

- Commandes simples reconnues par des règles strictes et exécutées **sans LLM** : réponse quasi instantanée, même hors ligne.
- Règles d'Avenoir : « ouvre / va sur <URL> », « ouvre / lance / démarre <application> », « quelle heure est-il ? », « quel jour sommes-nous ? ».
- Règles déclarées par les modules (limitées à leurs propres outils, arguments fixes possibles) ; elles ne masquent jamais celles d'Avenoir.
- Insensible à la casse, sur la phrase entière ; dans le doute, la phrase part au LLM.
- Même contrôle des permissions et même journal d'annulation que les appels du LLM ; le texte de l'outil est affiché tel quel.
- Désactivable (`fast_path = false`).

## 3. Outils natifs

| Outil | Risque | Ce qu'il fait |
|---|---|---|
| `current_time` | lecture seule | date et heure locales en phrase française (« Il est 13 h 41, vendredi 6 novembre 2026. »), fuseau du système |
| `system_info` | lecture seule | système, distribution, architecture, nom de la machine, nombre de cœurs |
| `launch_app` | réversible | lance une application par son nom (Linux : `gtk-launch` puis exécutable du PATH, détaché ; Windows : `ShellExecuteW`) ; application inexistante signalée clairement |
| `open_url` | réversible | ouvre une adresse `http(s)` dans le navigateur par défaut (Linux : `xdg-open` ; Windows : `ShellExecuteW`) |

- Arguments validés côté outil : noms d'application sans métacaractère ni option, URL `http(s)` uniquement (24 tentatives d'injection testées).
- Un outil non pris en charge par le système n'est pas proposé.

## 4. Sécurité et permissions

- **Trois niveaux de risque** par outil : lecture seule, réversible, sensible.
- **Politique par niveau** (`allow`, `confirm`, `deny`), réglable ; une action **sensible n'est jamais exécutée sans confirmation**, quoi que dise la configuration.
- Confirmation au terminal (« Autoriser ? [o/N] ») : tout ce qui n'est pas o / oui / y / yes vaut refus, Ctrl+C aussi.
- Une action refusée revient au LLM comme une erreur, avec la consigne de ne pas la contourner par un autre outil.
- Les données (résultats d'outils, pages, messages, modules) sont traitées comme des données, jamais comme des instructions ; la vraie barrière reste le contrôle des permissions.
- Aucun outil shell générique ; aucun processus lancé par un shell ou une chaîne construite (tableaux d'arguments, `ShellExecuteW`).
- Secrets uniquement dans des variables d'environnement.
- Dossier de données en `700`, base des conversations en `600` (Linux).
- **Modules jamais entièrement dignes de confiance** : voir § 7.

## 5. Interruption et annulation

- **Ctrl+C n'arrête jamais l'application** : il interrompt l'action en cours (appel au LLM, outil, module) et rend la main tout de suite ; sans action en cours, il efface la saisie.
- Une commande ou une voie rapide interrompue s'affiche « Requête interrompue », jamais comme un échec, et l'historique garde « Action interrompue par l'utilisateur. ».
- Après une interruption : liste des actions effectuées (↺ annulables, ✗ non annulables) et proposition d'annuler celles qui peuvent l'être.
- **`/annuler`** : même proposition pour la dernière requête ; répétée, elle remonte aux requêtes précédentes (20 au plus).
- Annulations en ordre inverse, avec vérification que l'état n'a pas changé depuis (sinon refus, sans rien écraser) ; échecs signalés sans bloquer les suivantes ; chaque annulation est journalisée.
- Les outils de modules peuvent proposer leur propre annulation (ex. : ville par défaut de la météo).

## 6. Conversations

- Chaque message est **enregistré immédiatement** dans une base SQLite locale (`avenoir.db`), avec les appels et résultats d'outils et la consommation.
- Au lancement : reprise de la dernière conversation et réaffichage des 50 derniers messages ; le LLM se souvient.
- Commandes : `/nouvelle [titre]` (rien n'est effacé), `/reset` (efface les messages de la conversation en cours, qui garde son numéro et son titre ; s'il y en a : confirmation, puis proposition d'en garder une transcription Markdown dans `transcriptions/` du dossier de données, lisible par l'utilisateur seul — si elle échoue, rien n'est effacé ; l'écran et l'historique du terminal sont vidés, puis le cadre d'accueil réaffiché avec le chemin de la transcription), `/conversations`, `/ouvrir <n° ou titre>`, `/renommer <titre>`, `/supprimer [n° ou titre]` (avec confirmation, puis proposition de transcription), `/historique`, `/transcription` (même transcription Markdown que `/reset`, sans rien effacer). Transcriptions : en-tête (titre, numéro, date en toutes lettres, nombre d'échanges), une rubrique « › Vous » / « ◆ Avenoir » par message, appels d'outils repliés dans la réponse avec leurs arguments lisibles et leur résultat ; rien d'actif à l'ouverture dans un lecteur Markdown (résultats d'outils en blocs de code, HTML neutralisé et images désactivées hors code, bloc de code resté ouvert refermé), le code des messages restant intact.
- Titre automatique (premiers mots de la première question).
- Requêtes SQL toujours paramétrées ; schéma versionné ; suppression en cascade.
- Une base illisible n'empêche pas de démarrer (conversations alors non conservées, avec un message).

## 7. Modules

- Extension par des **programmes séparés**, dans n'importe quel langage, décrits par un manifeste `module.toml` : outils, commandes, règles de voie rapide, permissions, variables d'environnement.
- **Protocole** : JSON-RPC 2.0 sur stdio, un message par ligne ; protocole `avenoir` = sur-ensemble de MCP (`initialize`, `tools/list` paginé, `tools/call`, `notifications/cancelled`, `avenoir/undo`).
- **Compatibilité MCP** : un serveur MCP existant s'utilise comme module (`protocol = "mcp"`).
- Modules fournis à côté de l'exécutable (activés par défaut) et modules de l'utilisateur dans son dossier de données (**désactivés** tant qu'il ne les a pas activés) ; un module ne peut pas remplacer un module fourni.
- Outils exposés sous le nom `module__outil` ; commandes de module (`/meteo`) qui n'écrasent jamais celles d'Avenoir ; complétion (Tab) et aide incluses.
- **Sécurités** (directive 15) :
  - manifeste validé strictement avant tout lancement (clé inconnue refusée, programme sans `..` ni chemin absolu, règles et commandes limitées aux outils du module) ;
  - lancement sans shell ; **environnement minimal** (jamais les clés d'Avenoir ni les secrets des autres modules ; seulement les variables déclarées) ;
  - outils `sensitive` si le manifeste ne dit rien ;
  - messages bornés (8 Mio), 128 outils au plus, schémas et descriptions bornés, délai maximal par requête (10 s pour la poignée de main, 30 s par appel) ;
  - textes nettoyés (caractères de contrôle, C1, marques bidirectionnelles retirés) ;
  - chaque appel passe par les permissions et l'audit ;
  - un module qui plante, traîne ou triche n'empêche pas le démarrage et ne fait pas tomber l'assistant.
- **Risque des outils** : celui du manifeste, relevable par l'utilisateur (`[modules.<id>] risk` dans la configuration, ou `/risque`) ; une **baisse** exige avertissement, disclaimer et saisie de « Je sais ce que je fais » (accord enregistré dans la base) ; appliqué à chaud ; une baisse sans accord est ignorée avec un avertissement.
- `/modules` : état de chaque module (actif, désactivé, en échec), origine, permissions, outils et risque.
- Dossier de données par module (`module-data/<id>/`, variable `AVENOIR_MODULE_DATA`), stderr dans `logs/modules/<id>.log` (taille bornée).
- Ctrl+C pendant un appel de module : la main revient tout de suite, le module reçoit `notifications/cancelled`.

## 8. Module météo

- Fourni avec Avenoir, activé par défaut, données **Open-Meteo** (CC BY 4.0, sans clé d'API) ; seuls le nom de la ville et ses coordonnées sont envoyés.
- Conditions actuelles (température, ressenti, ciel, vent, humidité) et prévisions jour par jour jusqu'à 7 jours (ciel, minimum, maximum, probabilité et cumul de pluie), en phrases françaises.
- Ville par défaut réglée par l'utilisateur (« ma ville est Lyon », `/ville Lyon`), **annulable** ; pas de géolocalisation.
- Homonymes : pays précisé (« Brest, France ») ou, à défaut, pays de la ville par défaut préféré.
- Commandes `/meteo [ville]`, `/previsions [ville]`, `/ville <ville>` ; voie rapide (« quel temps fait-il à Lyon ? », « va-t-il pleuvoir demain ? », « prévisions météo pour Brest »).
- Noms de ville validés (pas de caractère de contrôle ni de symbole étranger à un nom de lieu).

## 9. Interface en ligne de commande

- Exécutable `avenoir [chemin/config.toml]`.
- **Commandes** : `/aide`, `/outils`, `/annuler`, `/modules`, `/risque [outil niveau]`, `/nouvelle`, `/reset`, `/conversations`, `/ouvrir`, `/renommer`, `/supprimer`, `/historique`, `/transcription`, `/quitter` (`/q`), et celles des modules.
- **Saisie** (replxx) : édition de ligne, historique des saisies conservé entre deux lancements, complétion des commandes (Tab), Ctrl+D pour quitter.
- **Apparence** :
  - barre de saisie fixe en bas du terminal (grandit avec la saisie, suit les redimensionnements, sous Windows aussi) ;
  - à la sortie, écran et historique du terminal vidés, puis cadre d'au revoir avec le pingouin (« À bientôt ! », conversation qui reprendra au prochain lancement) ;
  - cadre d'accueil avec le pingouin en pixel art (ASCII sans couleurs) ;
  - styles distincts pour la requête (›), la réponse (◆ Avenoir), les actions (» … ✓ / ✗ en direct), les avertissements (▲) — symboles d'une colonne présents dans les polices de console courantes, Windows compris, les confirmations encadrées, les badges de risque et les erreurs ;
  - réponses en Markdown rendu, découpées aux mots ; toupie « Réflexion… » ;
  - couleurs désactivées hors terminal et avec `NO_COLOR`.
- Avertissements de démarrage (modules ignorés ou en échec) affichés après l'accueil.

## 10. Configuration

- Fichier `config.toml` (exemple commenté : `config.example.toml`) ; une clé inconnue ou de mauvais type est une erreur qui cite la clé.
- Sections : `[llm]` (fournisseur, modèle, adresse, nom de la variable de la clé, `temperature`, `reasoning_effort`, `max_tokens`, commande et délai de Claude Code), `[agent]` (`max_steps`, `fast_path`, `system_prompt`), `[permissions]` (politique par niveau), `[modules.<id>]` (`enabled`, `risk` par outil).
- Dossier de données : `~/.local/share/avenoir` (ou `$XDG_DATA_HOME`), `%LOCALAPPDATA%\avenoir` sous Windows.

## 11. Journaux

- Logs sur stderr, niveau `warn` par défaut, réglable avec `AVENOIR_LOG` (`debug`, `info`…).
- **Journal d'audit** (logger `audit`) : chaque exécution, refus, échec et annulation d'outil ; affiché avec `AVENOIR_LOG=info`.
- stderr de chaque module dans son propre fichier de log.

## 12. Banc d'essai avec de vrais modèles

- `avenoir-evals` : passe les **requêtes de référence** (`evals/requetes.toml`, 24 cas : simples, multi-outils, suite de conversation, ambiguës, hors de portée, erreur d'outil, injections, arguments piégés, refus et accord) sur un vrai modèle.
- Vrai agent et vrai fournisseur, mais **doublures d'outils** (mêmes nom, description, schéma et risque ; résultats ou erreurs préparés) : rien n'est lancé ni modifié ; voie rapide coupée.
- Vérifications par cas : appels attendus (arguments partiels, sans casse), outils interdits, textes interdits dans les arguments, nombre d'appels maximal, mots attendus ou interdits dans la réponse, politique et réponse aux confirmations propres au cas.
- Options : `--config`, `--model`, `--reasoning`, `--cases`, `--only`, `--repeat`, `--prompt`, `--report`, `--modules`.
- Rapport Markdown (réussite par catégorie, durée, tokens, coût, détail des échecs) dans `evals/resultats/` ; synthèse dans `evals/resultats/COMPARAISON.md`.

## 13. Développement, qualité et publication

- C++23, CMake + vcpkg (préréglages `debug`, `release`, `asan`) ; options `AVENOIR_BUILD_TESTS`, `AVENOIR_SANITIZE`, `AVENOIR_MODULE_METEO`, `AVENOIR_EVALS`.
- Scripts `scripts/build.sh` et `scripts\build.ps1` (mêmes options) : `-c` build propre, `-r` lancer Avenoir, `-n` sans tests, `-f` reformater, `-k` vérifications d'avant commit ; sous Windows, environnement Visual Studio chargé automatiquement.
- Avertissements traités comme des erreurs, `clang-format`, `clang-tidy`, AddressSanitizer + UndefinedBehaviorSanitizer ; plus de 130 tests Catch2, dont un test de bout en bout de la CLI.
- **CI** à chaque push : Linux GCC 14, Clang 20, ASan + UBSan, Windows MSVC, formatage, shellcheck et PSScriptAnalyzer.
- **Releases** automatiques sur tag `vX.Y.Z` : contrôle de la version, build et tests Linux / Windows, binaires publiés (avec les modules fournis).
- Images du README générées par des scripts (`docs/readme/`).
