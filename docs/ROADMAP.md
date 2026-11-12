# Roadmap d'Avenoir

> Plan de travail détaillé, découpé en jalons livrables. La vision et les grandes phases sont dans [`PROJECT.md`](PROJECT.md) (§ 6) ; ce document dit **quoi faire, dans quel ordre, et quand c'est fini**.
>
> Légende : ✅ fait · 🔄 en cours · ⬜ à faire · **P1** indispensable · **P2** important · **P3** confort

---

## Vue d'ensemble

| Jalon | Objectif | État |
|---|---|---|
| **0. Conception** | Vision, architecture, directives, choix de la stack | ✅ |
| **1. Socle** | Projet CMake, CI, cœur (agent, permissions, voie rapide), LLM, premiers outils, hôte de modules v1, CLI, conversations persistantes | ✅ |
| **2. Assistant du quotidien** | Contrôle système (volume, écran, multimédia), recherche web, applications installées, changer de LLM à tout moment | 🔄 |
| **3. Résident** | Démon en arrière-plan, IPC, journal structuré, permissions durables et révocables, magasin de modèles | ⬜ |
| **4. Tâches et modules** | Orchestrateur multi-agents (agents en parallèle, plusieurs projets isolés), fichiers, mémoire, contexte long, appels entre modules, fonctions de fond | ⬜ |
| **5. Voix** | Mot de réveil, STT, TTS, confirmations vocales | ⬜ |
| **6. Interface graphique** | Widget en haut de l'écran (pingouin animé), application principale, onglets de modules | ⬜ |
| **7. Identité et présence** | Propriétaire et liste blanche, vérification par la voix et le visage, verrouillage et connexion automatiques | ⬜ |

L'ordre suit un principe : **chaque jalon produit un assistant utilisable**, plus capable que le précédent. La voix et l'UI viennent tard parce qu'elles reposent sur un cœur stable (PROJECT.md, décision 7).

---

## Jalon 0 — Conception ✅

- ✅ Vision, principes directeurs, directives de sécurité (`PROJECT.md`)
- ✅ Architecture : agent + voie rapide + permissions + outils ; démon / interface / modules
- ✅ Modules en processus JSON-RPC, compatibles MCP ; interface web dans la vue web système ; onglets de modules déclaratifs ou web isolés
- ✅ Stack : C++23, CMake, vcpkg, `std::expected`, modèle synchrone + threads
- ✅ Fournisseurs LLM prévus : OpenAI-compatible, Anthropic, Claude Code (abonnement)
- ✅ Format de configuration (`config.example.toml`)
- ✅ Instructions de travail (`CLAUDE.md`)

---

## Jalon 1 — Socle ✅

*But : un assistant texte complet, sûr et testé, sur Windows comme sur Linux.*

### 1.1 Projet et outillage — P1 ✅
- ✅ `CMakeLists.txt` racine : C++23, avertissements stricts en erreurs, options `AVENOIR_BUILD_TESTS`, `AVENOIR_SANITIZE`
- ✅ `cmake/AvenoirOptions.cmake` : cible `avenoir::options` (avertissements, sanitizers) et fonctions `avenoir_add_library` / `avenoir_add_tests`
- ✅ `CMakePresets.json` : `debug`, `release`, `asan` ; générateur Ninja ; toolchain vcpkg
- ✅ `vcpkg.json` : nlohmann-json, tomlplusplus, cpr, reproc, spdlog, catch2 (baseline figée)
- ✅ `.clang-format`, `.clang-tidy` (conventions de nommage vérifiées), `.gitignore`
- ✅ `avenoir::core` (version) + `apps/cli` (affiche la version) + premier test Catch2. Les autres bibliothèques sont créées à leur étape (1.4 à 1.6)
- ✅ Chaîne complète vérifiée sous Linux avec vcpkg (`debug` et `asan` : build sans avertissement, tests, clang-tidy)
- ✅ Même vérification sous Windows (MSVC), par la CI

**Fini quand :** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` fonctionne sur Linux et Windows.

### 1.2 Intégration continue et releases — P1 ✅
- ✅ Workflow `ci` à chaque push / PR : Linux GCC 14, Linux Clang 20, Linux ASan + UBSan, Windows MSVC ; build sans avertissement + `ctest`
- ✅ Vérification `clang-format` (même version majeure qu'en local)
- ✅ Cache binaire vcpkg (action commune `.github/actions/setup-vcpkg`)
- ✅ Workflow `release` sur tag `vX.Y.Z` : contrôle tag = version, build + tests Linux / Windows, publication de la release avec les binaires
- ✅ Premier passage au vert sur GitHub (dépôt public), y compris Windows MSVC
- ✅ Première release `v0.1.0` (pré-version, binaires Linux et Windows)
- ✅ Release `v0.2.0` (pré-version) : CLI complète — outils natifs, confirmations, annulation, conversations persistantes, apparence soignée

**Fini quand :** chaque push sur `main` est vérifié sur les deux OS, badge vert dans le README, et une release publie des binaires téléchargeables.

### 1.3 Cœur (`avenoir::core`) — P1 ✅
- ✅ Messages (`User`, `Assistant`, `ToolResult`) et `ToolCall`
- ✅ `Tool`, `ToolSpec`, `RiskLevel`, `ToolRegistry` (refus des doublons)
- ✅ `PermissionPolicy` (`sensitive` jamais `allow`), `ConfirmationHandler`, `AutoReject`
- ✅ `FastRouter` : règles « ouvre <url> », « lance <app> », « quelle heure est-il »
- ✅ `Agent::handle` : voie rapide, boucle LLM ↔ outils, `max_steps`, résultats tronqués, erreurs renvoyées au LLM en texte, audit
- ✅ `AppConfig` lu depuis TOML ; test qui charge `config.example.toml`
- ✅ Tests : LLM scripté, action sensible refusée non exécutée, troncature, règles de la voie rapide
- ✅ 32 tests (agent avec LLM scripté, permissions, voie rapide, registre, configuration) ; annulation par `std::stop_token`

### 1.4 Fournisseurs LLM (`avenoir::llm`) — P1 ✅
- ✅ `openai_compatible` (cpr + JSON) : arguments d'outils en chaîne ou en objet, arguments illisibles transmis à l'outil (qui les refuse)
- ✅ `openai_compatible` testé avec un vrai serveur (Ollama ROCm, 10 modèles, en 1.8)
- ✅ `anthropic` (fusion des messages consécutifs de même rôle)
- ✅ `claude_code` : `claude -p` via `reproc` (boucle `poll` non bloquante : entrée de toute taille, annulation, délai), outils de Claude Code désactivés, clés d'API neutralisées dans l'environnement, prompt sur stdin, `--json-schema` (`text` et `tool_calls` obligatoires), dossier de travail temporaire — testé en réel
- ✅ Fabrique `make_provider` : clé lue dans la variable nommée par la config, jamais citée dans les erreurs
- ✅ Consommation (`Usage` : tokens, coût indicatif) remontée par les trois fournisseurs
- ✅ Annulation par `std::stop_token` (requêtes HTTP et processus `claude`)
- ✅ Réponse finale vide → erreur explicite (cœur), au lieu d'une ligne blanche
- ✅ Tests de conversion des messages et de lecture des réponses, sans réseau ; test d'intégration masqué `[.integration]` (`AVENOIR_TEST_CONFIG`)
- ℹ️ La limite de l'abonnement n'apparaît pas dans la sortie `claude -p --output-format json` (Claude Code 2.1.319) : seules la consommation et le coût indicatif par appel sont disponibles

### 1.5 Outils natifs (`avenoir::tools`) — P1 ✅
- ✅ `current_time` : phrase française (« Il est 13 h 41, vendredi 6 novembre 2026. »), fuseau du système (`std::chrono::current_zone`)
- ✅ `system_info` : système, distribution (`/etc/os-release`), architecture, machine, cœurs, en une phrase
- ✅ `launch_app` : nom validé strictement ; Linux `gtk-launch` puis exécutable du PATH via `setsid -f` (détaché, existence vérifiée avant) ; Windows **`ShellExecuteW`** (jamais `cmd.exe`)
- ✅ `open_url` : `http(s)` uniquement ; Linux `setsid -f xdg-open` ; Windows `ShellExecuteW`
- ✅ Code par OS dans `src/platform/{linux,windows,unsupported}.cpp` ; `launch_app` et `open_url` non enregistrés là où ils ne sont pas pris en charge
- ✅ Tests : validation (cas valides et 24 tentatives d'injection), phrases de l'heure et du système ; aucun test ne lance d'application
- ✅ Branchés dans la CLI : « quelle heure est-il ? » passe par la voie rapide ; `system_info` testé en réel via Claude Code
- ✅ `launch_app` (`gtk-launch`, application détachée) et `open_url` essayés en réel sous Linux ; application inexistante → message clair
- ⬜ Essai réel sous Windows — à faire en 1.8 ; d'ici là, le code Windows n'est vérifié que par la CI
- ✅ Annulation : aucun des quatre outils n'est annulable de façon sûre (`launch_app` et `open_url` seront listés comme non annulables) ; les outils du jalon 2 (volume, luminosité, mode sombre) le seront

### 1.6 Hôte de modules v1 (`avenoir::modules`) — P1 ✅
- ✅ Découverte des modules : `<installation>/modules/` et dossier de données de l'utilisateur ; lecture et validation stricte de `module.toml` (PROJECT.md § 2.6) ; module utilisateur désactivé par défaut
- ✅ Lancement du module (API C de `reproc`, sans shell), JSON-RPC 2.0 sur stdio, thread de lecture, délai max par requête, messages bornés, SIGPIPE ignoré
- ✅ Protocole `avenoir` v1 = sur-ensemble de MCP : poignée de main, `tools/list` paginé, `tools/call` ; outils exposés en `module__outil` ; risque du manifeste relevable par l'utilisateur, `sensitive` si absent
- ✅ Commandes (`/meteo`) et règles de voie rapide déclarées par les modules (limitées à leurs propres outils, arguments fixes possibles), sans masquer celles d'Avenoir
- ✅ Compatibilité MCP (`protocol = "mcp"`) : `initialize`, `tools/list`, `tools/call`
- ✅ stderr de chaque module redirigé vers `logs/modules/<id>.log` ; un module défaillant n'empêche pas le démarrage ; environnement minimal
- ✅ Configuration `[modules.<id>]` (activation, risque relevé) ; remplace `[[mcp_servers]]` et `McpServerConfig`
- ✅ Baisse de risque : prise en compte seulement avec un accord enregistré (sinon ignorée + avertissement) ; commande `/risque` qui affiche avertissement et disclaimer et exige « Je sais ce que je fais » ; appliquée à chaud
- ✅ Annulation pour les outils de modules : action d'annulation renvoyée par l'outil (`_meta`), exécutée par `avenoir/undo`
- ✅ Ctrl+C pendant un appel de module : `Tool::call` reçoit un `std::stop_token`, le module reçoit `notifications/cancelled`
- ✅ Module de test (plantage, lenteur, sorties piégées, mode MCP) pour les tests d'intégration de l'hôte
- ✅ **Module météo** fourni (`modules/meteo/`) : conditions actuelles, prévisions sur 7 jours, ville par défaut annulable ; Open-Meteo, sans clé ; `/meteo`, `/previsions`, `/ville` et voie rapide ; livré dans les archives de release

**Fini quand :** un module (fourni ou installé) expose ses outils, commandes et règles ; un module qui plante, traîne ou triche ne fait pas tomber l'assistant ; le module météo répond à « quel temps fait-il à Lyon ? ».

### 1.7 CLI (`apps/cli`) — P1 ✅
- ✅ Câblage minimal (fait avec 1.4) : lecture de la config, fournisseur, agent, boucle de saisie, `/reset`, `/quitter`, logs sur stderr (`AVENOIR_LOG`)
- ✅ Commandes `/aide` et `/outils`
- ✅ Confirmations au terminal (« Autoriser ? [o/N] ») ; tout sauf o/oui/y/yes vaut refus, Ctrl+C aussi — testé en réel
- ✅ Ctrl+C **ne ferme pas** la CLI : la requête tourne dans un thread de travail ; le gestionnaire de signal (`sigaction` / `SetConsoleCtrlHandler`) ne lève qu'un indicateur, le thread principal demande l'arrêt (`std::stop_token`) ; sans action en cours, il efface la saisie et rappelle `/quitter` — testé en réel (arrêt d'un appel à Claude Code, aucun processus orphelin)
- ✅ Cœur : `ToolOutput` peut contenir une action d'annulation (`UndoAction`) ; journal d'annulation par requête dans l'agent (20 dernières requêtes, outils à effet de bord seulement) ; annulation en ordre inverse, échecs signalés sans bloquer, audit
- ✅ À l'interruption : liste des actions effectuées (✓ annulables, ✗ non annulables) et question « Annuler N action(s) annulable(s) ? [o/N] »
- ✅ Commande `/annuler` : même proposition pour la dernière requête ; répétée, remonte aux précédentes
- ✅ Tests : ordre inverse, refus si l'état a changé (sans écraser), remontée des requêtes, requête interrompue, outils en lecture seule non journalisés
- ✅ Saisie avec **replxx** : édition de ligne, historique des saisies conservé (`cli_history.txt` dans le dossier de données), complétion des commandes (Tab), Ctrl+C distingué de Ctrl+D
- ⬜ Essai interactif de Ctrl+C sous Windows (le code n'est vérifié que par la CI) — à faire en 1.8
- ✅ Apparence (PROJECT.md § 2.9) : barre de saisie fixe en bas du terminal (zone de défilement DECSTBM ; grandit pour les saisies longues ; suit les redimensionnements, même pendant la frappe ; confirmations dans la barre ; terminal restauré à la sortie ; Linux et Windows, avec ou sans couleurs), toupie avec les réponses, réponses découpées aux mots ; test de bout en bout de la CLI lancé par ctest sur Linux et Windows, cadre d'accueil et cadre d'au revoir (terminal vidé à la sortie) avec pingouin en pixel art (demi-blocs colorés ; ASCII sans couleurs), styles distincts (requête `›`, réponse `◆ Avenoir`, action `» … ✓/✗` en direct, confirmation encadrée, badges de risque, erreurs), Markdown rendu, toupie « Réflexion… » ; bibliothèque `avenoir::terminal` (11 tests) ; couleurs désactivées hors terminal et avec `NO_COLOR` ; journal d'audit masqué par défaut (`AVENOIR_LOG=info`)
- ⬜ P3 : `/config`

### 1.8 Essais avec de vrais modèles — P1 ✅
- ✅ Banc d'essai `evals/` (`avenoir-evals`) : 26 requêtes de référence en TOML (simples, multi-outils, suite de conversation, ambiguës, hors de portée, erreur d'outil, injections dans les résultats d'outils et dans un message, arguments piégés, refus et accord, données inventées), vrai agent et vrai fournisseur, **doublures d'outils**, voie rapide coupée ; répétitions (`--repeat`), prompt à essayer (`--prompt`), rapport Markdown par passage dans `evals/resultats/` ; logique testée sans LLM (6 tests)
- ✅ Passé sous Linux sur 12 modèles : `claude_code` (haiku, sonnet, utilisés comme API distante) et 10 modèles locaux via Ollama ROCm (RX 7900 XTX), du très léger (Qwen3.5 4B, Gemma 4 E4B, Ministral 3 3B) au lourd (Qwen3.8 27B, Gemma 4 26B-A4B, Qwen3.6 35B-A3B, GLM-4.7-Flash) ; tableaux dans `evals/resultats/COMPARAISON.md`
- ✅ `reasoning_effort` pour `openai_compatible` (et `--reasoning` dans le banc) : sans lui, les modèles qui réfléchissent ajoutent des centaines de tokens par réponse
- ✅ Ajustements : consigne de protocole de `claude_code` (haiku croyait ses outils indisponibles : 20/24 → 24/24), description de `open_url` (`http(s)` seulement), prompt système v3 (v2 abandonnée : trop hésitante) ; tous les finalistes progressent (Gemma 4 12B 92 % → 97 %, Ministral 3 8B 92 % → 96 %)
- ✅ Modèles recommandés notés dans PROJECT.md § 2.5 : Gemma 4 26B-A4B (local 24 Go), Ministral 3 8B (léger), Claude (distant)
- ✅ `docs/FONCTIONNEMENT.md` : démarrage, trajet d'une requête, rôle de chaque bibliothèque, hôte de modules, annulation, banc d'essai
- ✅ Prompt système v4 (aucune donnée sans outil, rien d'annoncé avant le résultat) et critère `preamble_excludes` du banc, après l'essai sous Windows : Ministral 73/78 → 75/78, Gemma 4 26B-A4B 78/78, haiku 52/52
- ✅ Essai sous Windows (Windows 11 LTSC en machine virtuelle, MSVC, Ministral 3 8B servi par l'Ollama de Linux) : compilation et tests, banc 69/72 comme sous Linux, outils natifs, Ctrl+C, module météo. Corrigés en chemin : test de bout en bout dépendant de l'encodage de la console, Ctrl+C sur la voie rapide affiché comme un échec, symboles mal rendus par les polices de console, saisie décalée après un redimensionnement de la console

**Fini quand :** les requêtes de référence passent avec au moins un modèle local et un modèle distant, sur Linux et Windows.

### 1.9 Conversations persistantes (`avenoir::storage`) — P1 ✅
- ✅ Dépendance `sqlite3` (vcpkg) ; base `avenoir.db` dans le dossier de données de l'utilisateur (dossier en `700`, base en `600`)
- ✅ Schéma versionné (`PRAGMA user_version`) : conversations (titre, dates, compteur d'activité pour un ordre fiable), messages (rôle, contenu JSON avec appels et résultats d'outils et consommation, horodatage) ; suppression en cascade ; WAL
- ✅ Écriture immédiate de chaque message, requêtes toujours paramétrées (aucune injection SQL possible)
- ✅ Agent : `load_history` et observateur de l'historique (le cœur ne dépend pas du stockage) ; consommation conservée dans chaque réponse
- ✅ CLI : reprise de la dernière conversation au lancement, réaffichage des 50 derniers messages
- ✅ CLI : `/nouvelle [titre]`, `/reset` (efface la conversation en cours et l'écran), `/conversations`, `/ouvrir <n° ou titre>`, `/renommer <titre>`, `/supprimer [n° ou titre]` (avec confirmation), `/historique`
- ✅ Titre automatique (premiers mots de la première question, sans couper un caractère UTF-8)
- ✅ Tests : aller-retour JSON de chaque message, données conservées après fermeture, renommage, suppression en cascade, titres piégés (injection SQL), ordre des conversations, observateur et reprise dans l'agent
- ✅ Essai réel : conversation, fermeture, réouverture — le LLM se souvient ; toutes les commandes essayées

**Fini quand :** on ferme la CLI au milieu d'une conversation, on la rouvre, et tout est là ; on peut créer, lister, ouvrir, renommer et supprimer des conversations.

---

## Jalon 2 — Assistant du quotidien 🔄

*But : couvrir les demandes qu'on fait vraiment tous les jours.*

### 2.1 Contrôle système — P1 ✅
| Outil | Risque | Linux | Windows |
|---|---|---|---|
| ✅ `set_volume` (niveau, variation, son coupé) | reversible | `wpctl` (PipeWire), sinon `pactl` | Core Audio (`IAudioEndpointVolume`) |
| ✅ `media_control` (lecture, pause, suivant, ce qui passe) | reversible | `playerctl` (MPRIS) | SMTC (C++/WinRT) |
| ✅ `set_brightness` | reversible | `brightnessctl` | WMI (`WmiMonitorBrightness`) |
| ✅ `set_dark_mode` | reversible | `gsettings` (GNOME, portail XDG) | registre `Personalize` |
| ✅ `lock_screen` | reversible | `loginctl lock-session` | `LockWorkStation` |
| ✅ `notify` | read_only | `notify-send` | icône de notification (`Shell_NotifyIconW`) |
| ✅ `power` (veille, extinction, redémarrage) | **sensitive** | `systemctl` (logind) | `SetSuspendState`, `ExitWindowsEx` |

- ✅ Programmes du système lancés par `reproc` (tableau d'arguments) plutôt que D-Bus par `sdbus-c++` : aucune dépendance de plus (PROJECT.md, décision 41)
- ✅ Annulation du volume, de la luminosité et du mode sombre, refusée si l'état a changé depuis
- ✅ Règles de voie rapide associées (« monte le son », « coupe le son », « pause », « verrouille l'ordinateur », « mode sombre »…) ; extinction et redémarrage jamais par la voie rapide
- ✅ Un outil indisponible sur un OS ou une machine n'est pas enregistré (PROJECT.md § 4.3) : pas de sortie audio, pas d'écran réglable, programme absent
- ✅ Banc d'essai : 6 cas de plus (contrôle, refus d'extinction, injection par un titre de morceau), prompt v5 (evals/resultats/COMPARAISON.md, série 6)
- ✅ Banc d'essai : outils de modèle et 5 cas « modeles » (bascule, chargement, « quel modèle es-tu ? », bascule après des réponses sans outil) (COMPARAISON.md, séries 7 et 8)
- ✅ Vérifié sous Windows (VM) : mode sombre, multimédia, notification émise, volume absent sans sortie audio
- ℹ️ La notification n'a pas pu être vue à l'écran dans la VM (notifications coupées, la bulle de référence .NET non plus) : à vérifier sur un vrai poste Windows

### 2.2 Applications installées — P1 ✅
- ✅ Index des applications : fichiers `.desktop` (Linux, dossiers XDG et Flatpak, traductions françaises en alias), raccourcis du menu Démarrer et applications du Store (Windows, noms localisés)
- ✅ `launch_app` tolérant : « ouvre le bloc-notes », « ouvre le navigateur » (navigateur par défaut), « lance code » → bonne application ; plusieurs candidates proposées ; vérifié sous Windows (Bloc-notes, Edge)
- ✅ Outil `list_apps` (read_only) pour que le LLM puisse chercher
- ✅ Voie rapide : article et accents permis (« ouvre le bloc-notes »)

### 2.3 Recherche web — P1 ✅
- ✅ Moteur : **SearXNG local par défaut**, lancé par Avenoir avec podman ou docker à la première recherche (image téléchargée d'elle-même), sur 127.0.0.1 seulement, arrêté après 10 min sans recherche ou à la fermeture (décision 46) ; ou une instance SearXNG configurée (`[web] searxng_url`) ; DuckDuckGo (version HTML) sinon ; moteur retenu et changeable (`/moteur`, outil `set_search_engine` annulable, voie rapide « utilise searxng pour chercher ») ; mesuré : Google et Brave Search bloquent les recherches automatiques, même sans fenêtre (décision 45)
- ✅ Outil `web_search` (read_only) → titres, URL, extraits ; voie rapide « cherche sur le web X »
- ✅ Outil `read_webpage` (read_only) : page téléchargée (HTTP, 3 Mo, 15 s, 5 redirections), réduite à son texte, **lue à part par le modèle sans outils** ; seule sa réponse entre dans la conversation
- ✅ Adresses locales et privées refusées, y compris par redirection ou nom qui s'y résout (adresse IP vérifiée à chaque requête)
- ✅ Contenu web toujours traité comme donnée : cas `injection-recherche` au banc, consigne du lecteur, texte nettoyé (contrôles, marques bidirectionnelles)
- ⬜ Pages construites en JavaScript : refusées aujourd'hui ; lecture par le navigateur sans fenêtre (`--dump-dom`, profil jetable) si le besoin se confirme

### 2.4 Streaming des réponses — P2 ✅
- ✅ `LlmProvider` : variante en flux (`LlmRequest::on_text`, un rappel par fragment) pour `openai_compatible` et `anthropic` (Server-Sent Events) ; appels d'outils reçus en flux ; `claude_code` en 2.5
- ✅ Affichage progressif dans la CLI (ligne par ligne, blocs de code et tableaux une fois complets ; réponse écartée par la vérification signalée)
- ⬜ Plus tard : synthèse vocale phrase par phrase (jalon 5)

### 2.5 Fournisseur `claude_code` — P2 ✅
- ✅ Réduire la latence : processus `claude` gardé ouvert (`--input-format stream-json`), seule la suite de la conversation envoyée, session relancée si la conversation change ; réglage `persistent` ; mesuré : 6,1 s au lieu de 14,8 s pour trois requêtes enchaînées (haiku)
- ✅ Message d'erreur clair si la CLI n'est pas connectée (« lancez `claude` dans un terminal, puis `/login` »)
- ⬜ Plus tard : texte au fil de l'eau (la réponse structurée arrive dans un JSON partiel)

### 2.6 Changer de LLM à tout moment — P1 ✅
- ✅ Plusieurs modèles dans la configuration : un par section `[models.<nom>]`, le premier par défaut ; sans modèle, seule la voie rapide répond
- ✅ Dernier modèle utilisé retenu dans la base et repris au lancement suivant
- ✅ Commande `/modele [nom]` : sans argument, liste les modèles (actif marqué) ; avec, bascule sur ce modèle ; la conversation continue avec le même historique
- ✅ Outil `switch_llm` (reversible) et règles de voie rapide (« passe sur le modèle léger », « utilise le modèle claude ») ; annulable (retour au modèle précédent) ; nom reconnu sans casse ni accents
- ✅ Passer à un modèle distant demande confirmation (directive de sécurité n° 17) ; local = adresse de la machine ou `local = true` déclaré
- ✅ Modèle actif affiché dans le cadre d'accueil ; changement journalisé (journal d'audit de l'outil)
- ✅ Ancien modèle déchargé en changeant de modèle, s'il est sur le même serveur (réglable : `unload_on_switch`) ; « Chargement du modèle… » quand le modèle actif doit être chargé, puis le temps de chargement
- ✅ Modèles chargés ou déchargés hors d'Avenoir annoncés pendant la saisie ; état des modèles : `/memoire`, outil `llm_status`, voie rapide
- ✅ Changer le raisonnement à chaud, sans recharger le modèle : `/raisonnement`, outil `set_reasoning`, voie rapide ; retenu par modèle
- ✅ Charger, décharger, recharger un modèle local (Ollama) : `/charger`, `/decharger`, `/recharger`, outils `load_llm` / `unload_llm`, voie rapide (« décharge le modèle », « libère la carte graphique », « allume le modèle gemma ») ; déchargement du modèle actif différé à la fin de la réponse
- ✅ Le modèle connaît son nom (modèle actif ajouté au prompt système) ; voie rapide par nom seul (« bascule sur qwen », « charge qwen », « tu es quel modèle ? ») ; un changement ou un chargement annoncé sans appel d'outil relance le modèle une fois, puis est signalé (étendu aux actions des outils natifs : lancer, son, lecture, mode sombre, verrouillage, alimentation, notification ; désactivable : `verify_replies`) ; outils de modèle et cas « modeles » au banc
- ✅ Mode « local uniquement » (`[agent] local_only`) : un modèle distant n'est jamais utilisé, ni au lancement (premier modèle local à la place, sinon voie rapide seule), ni par un changement (refusé sans confirmation possible)

**Fini quand :** « monte le son », « mets en pause », « cherche la météo à Lyon », « ouvre le navigateur », « passe sur le modèle léger » fonctionnent sur les deux OS.

- ✅ Vérifié sous Windows (VM, 11 novembre 2026) : compilation MSVC et 204 tests ; réponses du LLM en flux ; `web_search` par la voie rapide et par le LLM, avec repli sur DuckDuckGo et avis quand ni podman ni docker n'est installé ; `read_webpage` (page lue, adresse locale refusée) ; `/moteur` ; changement de modèle par la voie rapide et par le LLM (ancien modèle déchargé, le nouveau connaît son nom)

État au 11 novembre 2026 : tout fonctionne sous Linux ; sous Windows (VM), le contrôle du système, la recherche web avec repli DuckDuckGo, le flux et le changement de modèle ont été vérifiés. Restent à essayer en vrai sous Windows : l'instance SearXNG locale (podman ou docker) et la session Claude Code.

---

## Jalon 3 — Résident ⬜

*But : Avenoir tourne en permanence et répond instantanément.*

- ✅ **P1** Config à l'emplacement standard : `$XDG_CONFIG_HOME/avenoir/`, `%APPDATA%\avenoir\` (jamais le dossier courant) ; création guidée au premier lancement : serveurs de modèles de la machine (Ollama, LM Studio, llama.cpp), du réseau local sur demande (confirmés « à moi »), adresse saisie, Claude Code, API Anthropic ; modèles conseillés par le banc en tête ; « local uniquement » proposé ; « aucun » : voie rapide seule, enregistré ; `/configurer` pour refaire le choix ensuite (autres réglages gardés) ; écriture par Avenoir (`core::save_config`, `0600`, atomique)
- 🔄 **P1** Réglages modifiables uniquement par Avenoir (PROJECT.md § 5.1) : démon seul rédacteur (via l'IPC), changements journalisés — fait : le démon est le seul à lire et écrire les réglages, par `/reglages`, `/reglage <clé> <valeur>` (écrits par le sceau, notés à l'audit ; affaiblir demande « Je sais ce que je fais », décision 52) et `/configurer`, appliqués par `/redemarrer` ; reste la surveillance du fichier pendant que tourne le démon
- ✅ **P1** Sceau HMAC-SHA256 des réglages, clé dans le trousseau du système (repli : fichier `0600`, signalé) : libsodium ; Credential Manager sous Windows, `secret-tool` sous Linux ; une clé par fichier ; dernier sceau gardé au trousseau contre le rejeu ; fichier donné en argument non scellé (mode développement, signalé)
- 🔄 **P1** Surveillance du fichier : sceau invalide → refus, restauration de la dernière version valide, alerte (avec le détail des changements), journal — fait **à chaque lancement** (fichier modifié ou supprimé : copie scellée remise, lignes changées montrées ; sceau absent, falsifié ou rejoué : fichier mis de côté, création guidée ; audit) ; reste la surveillance pendant que tourne le démon
- ✅ **P1** Tests : modification à la main, suppression du fichier ou du sceau, sceau refait sans la clé, sceau d'une autre version, rejeu d'un ancien couple, sceau d'un autre fichier → tous refusés (restaurés quand une copie scellée sûre existe) ; secrets en fichier `0600`
- ✅ **P1** Journal d'audit dans un fichier dédié, avec rotation (`spdlog` rotating sink) : `logs/audit.log` du dossier de données (`0700`), toujours écrit quel que soit `AVENOIR_LOG`, chaque entrée aussitôt sur le disque, 5 × 5 Mo ; vérifié par le test de bout en bout
- ✅ **P1** Mode démon (`avenoird`, décision 53) : processus de l'utilisateur, lancé à la demande par la CLI (détaché, une seule instance), seul à porter l'agent, les modules, les conversations et les réglages ; démon privé pour une configuration donnée en argument ; `/redemarrer`, `/arreter` ; journal `logs/daemon.log` ; démarrage à l'ouverture de session sur demande (`/demarrage`) : unité systemd utilisateur liée à la session graphique (sinon autostart XDG), entrée `Run` du compte sous Windows ; pas de service Windows (il n'aurait pas accès au bureau)
- ✅ **P1** IPC locale (socket Unix / named pipe) entre le démon et les interfaces (`avenoir::ipc`) ; la CLI devient un client — `libs/ipc` (transport écrit à la main, pair vérifié des deux côtés, une seule instance, messages bornés ; `ipc::Peer` : JSON-RPC 2.0 dans les deux sens, une requête reçue par fil, annulation, attente de la fin après Ctrl+C) ; la CLI n'est plus qu'un client (affichage, saisie, toupie) qui lance le démon au besoin
- ⬜ **P1** Journal structuré (dans la base de l'étape 1.9) : actions, décisions de permission, module concerné ; historique par conversation et global ; le démon devient le seul à écrire dans la base
- ⬜ **P1** Permissions durables (« toujours autoriser cet outil de ce module ») et révocables ; permissions de module accordées à l'installation, révocables
- ✅ **P2** Réévaluer le modèle de concurrence (décision 12) pour le démon : on garde le modèle synchrone (un fil par client et par requête reçue, `std::stop_token`), décision 53
- ⬜ **P2** Raccourci clavier global qui ouvre une saisie rapide
- ⬜ **P2** **Mémoire vidéo partagée avec les jeux** (déjà possible à la main : `/decharger`, « libère la carte graphique », 2.6) : le démon libère la carte graphique quand une application exigeante passe en plein écran (déchargement du modèle local, `keep_alive = 0` d'Ollama) ou bascule sur le modèle léger réglé par l'utilisateur, puis revient au modèle habituel ensuite ; jamais de changement silencieux (notification, journal) ; réglable (désactivé, décharger, basculer). Mesures de départ : Gemma 4 26B-A4B ~21 Go, Ministral 3 8B ~11 Go, rechargement en 19 s et 6 s (RX 7900 XTX)
- ✅ **P2** Mode « local uniquement » : refuse tout fournisseur distant (directive de sécurité n° 10) — fait avec 2.6 (`[agent] local_only`)
- ⬜ **P2** **Magasin de modèles** (CLI `/modeles`, puis application, 6.4) : modèles recommandés avec leur score au banc d'essai, mémoire vidéo et disque nécessaires ; installation et désinstallation par Ollama (ou fichier GGUF vérifié par somme de contrôle) ; ajout d'un fournisseur distant avec clé d'API dans le trousseau ; modèle non évalué signalé (directive n° 17)
- ⬜ **P2** Secrets (clés d'API) dans le trousseau du système (Credential Manager, Secret Service), qui sert déjà au sceau des réglages

---

## Jalon 4 — Tâches et modules avancés ⬜

### Orchestrateur multi-agents (PROJECT.md § 2.11, décision 48)
- ✅ **P1** Agent configurable : rôle, consigne, modèle, outils et modules permis, autonomie (`[agents.<nom>]`, puis application et CLI : créer, modifier, supprimer, lister)
- ✅ **P1** Coordinateur : découpe une demande en tâches, confie chacune à l'agent qui convient, suit l'avancement, assemble le résultat
- ✅ **P1** Exécution parallèle : un agent par fil, graphe de tâches avec dépendances (un agent attend le résultat d'un autre sans bloquer les autres ; échec ou annulation propagés ; cycle refusé) ; file d'attente par modèle local
- 🔄 **P1** Plusieurs projets en parallèle, isolés (fait : `/projet`, décision 56 — coordinateur, contexte, équipes et audit propres à chaque projet, 4 en parallèle, actions à confirmer refusées en arrière-plan ; restent les dossiers autorisés par projet, avec les outils fichiers, et la conservation des projets entre deux démarrages) : chacun son instance de la configuration agentique, ses agents, son contexte, son tableau de tâches, ses dossiers autorisés et ses journaux ; sans configuration, rôles estimés par Avenoir selon le projet et ses tâches
- ✅ **P1** Mode automatique : agents créés à la volée avec les seuls outils de leur tâche, supprimés à la fin ; mode configuré : équipe de l'utilisateur (chef de projet, développeur, relecteur…), modèles d'agents réutilisables
- 🔄 **P1** Outils agentiques : confier une tâche, rendre un résultat, demander une précision, tableau de tâches partagé
- ✅ **P1** Sécurité : moindre privilège par agent, permissions et confirmations par agent (confirmation toujours par l'utilisateur), audit et annulation avec l'agent qui a agi, messages entre agents traités comme des données, « local uniquement » pour tous, bornes (agents, profondeur, étapes, durée), Ctrl+C pour toute l'équipe
- 🔄 **P2** Affichage de l'activité de chaque agent (CLI, puis application) ; banc d'essai multi-agents

État (12 novembre 2026, `libs/agents`, décision 54) : configuration `[agents.<nom>]` et `[orchestration]`, commandes `/agents` et `/agent …` ; outil `delegate_tasks` du coordinateur (Avenoir) : plan validé (agents connus ou créés à la volée selon le mode, outils compris dans ceux permis aux agents, identifiants uniques, dépendances existantes, pas de cycle, bornes), tâches en parallèle (`max_parallel`), une tâche attend ses dépendances, un échec ou Ctrl+C se propage, un modèle local sert un agent à la fois ; chaque agent : ses seuls outils, son modèle (refusé en « local uniquement » s'il est distant, confirmé sinon), confirmations par l'utilisateur, audit « agent X : … » ; résultats transmis comme des données ; activité montrée en direct (« ◇ agent : tâche »). Projets en arrière-plan : `/projet` (décision 56). Restent : outils agentiques au-delà de la délégation (demander une précision, tableau de tâches), une seule profondeur de délégation aujourd'hui.

- ✅ **P1** Provenance des arguments (décision 55) : un argument tiré d'un résultat d'outil, et non d'une demande de l'utilisateur, fait confirmer l'action, y compris pour les agents d'une équipe (tâche du coordinateur et résultats transmis comptent comme des données)

### Autres tâches
- ⬜ **P1** Outils fichiers (lister, lire, déplacer, renommer, ranger) **restreints à des dossiers autorisés** dans la config, chemins canonicalisés (`std::filesystem::weakly_canonical`)
- ⬜ **P1** Suppression = `sensitive`, avec un résumé avant confirmation (« 12 fichiers dans Téléchargements »)
- ⬜ **P2** Gestion du contexte long : résumé automatique de l'historique au-delà d'un seuil
- ⬜ **P1** Appels entre modules via l'hôte (`avenoir/call_tool`) : outils `public` seulement, permissions et audit avec l'identité du module appelant
- ⬜ **P1** Fonctions de fond des modules : notifications vers l'hôte, mises à jour d'onglets
- ⬜ **P2** Outil de recherche de capacités (quand les modules sont nombreux, ne pas envoyer tous les outils au LLM)
- ⬜ **P2** Compatibilité MCP étendue (ressources, prompts, HTTP, `list_changed`) si besoin
- ⬜ **P2** Bac à sable des processus de modules (réseau, fichiers) selon leurs permissions
- ⬜ **P3** Mémoire persistante optionnelle (préférences de l'utilisateur), consultable et effaçable
- ⬜ **P3** Tâches planifiées (« rappelle-moi à 18 h ») avec `AutoReject` pour les actions sensibles hors présence

---

## Jalon 5 — Voix ⬜

- ⬜ **P1** Capture audio (`miniaudio`) + détection d'activité vocale
- ⬜ **P1** STT local (`whisper.cpp` ou `sherpa-onnx`), derrière l'option CMake `AVENOIR_WITH_VOICE`
- ⬜ **P1** TTS local (Piper)
- ⬜ **P1** Confirmations vocales qui **reformulent** l'action (directive de sécurité n° 9)
- ⬜ **P2** Mot de réveil (openWakeWord / sherpa-onnx)
- ⬜ **P2** Mesure de la latence de bout en bout (objectif : < 1,5 s pour une commande de la voie rapide)

---

## Jalon 6 — Interface graphique ⬜

*Interface web (HTML / CSS / TypeScript) dans la vue web du système, hébergée par un petit hôte C++ (PROJECT.md § 2.7, décisions 18 à 20).*

### 6.1 Fondations — P1
- ⬜ Choisir le framework web (Svelte, Solid, React…) et la bibliothèque C++ de vue web (`saucer`, `webview`…)
- ⬜ Hôte C++ des fenêtres (`apps/ui`) : WebView2 sous Windows, WebKitGTK sous Linux ; un processus, deux fenêtres
- ⬜ Pont typé interface ↔ hôte C++ ↔ démon (IPC du jalon 3)
- ⬜ Build de `ui/` intégré à CMake et à la CI ; contrôle de types TypeScript strict

### 6.2 Widget principal — P1
- ⬜ Fenêtre sans bordure, transparente, au premier plan, en haut au centre ; layer-shell sous Wayland (niri, KDE, Hyprland, Sway)
- ⬜ Barre pliée ; widget déplié au survol, avec animations
- ⬜ Pingouin animé (Rive ou Lottie) qui reflète l'état de l'assistant (repos, écoute, réflexion, action)
- ⬜ Onglets : activités en cours, historique défilable, informations système, version et mises à jour, chatbox, consommation et limites Claude Code
- ⬜ Confirmations dessinées par Avenoir (`ConfirmationHandler` graphique), hors de tout contenu de module
- ⬜ Bouton « Arrêter » (équivalent de Ctrl+C) et proposition d'annulation ; bouton « Annuler » sur la dernière requête (équivalent de `/annuler`)

### 6.3 Onglets des modules — P1
- ⬜ Onglets déclaratifs : format des composants (textes, listes, images, graphiques, boutons, formulaires) et rendu
- ⬜ Onglets web (permission `ui.web`) : iframe `sandbox="allow-scripts"`, origine `avenoir-module://<id>/`, CSP sans réseau, `postMessage` validé (directive de sécurité n° 12)
- ⬜ Tests de sécurité : un onglet web ne peut ni accéder au réseau, ni à l'interface, ni aux autres modules

### 6.4 Application principale — P1
- ⬜ Réglages (fournisseur LLM, permissions par défaut, démarrage automatique…)
- ⬜ Conversations ; historique des actions et logs, par conversation et global ; annulation possible depuis l'historique pour les actions encore annulables
- ⬜ Permissions : liste des accords durables et des permissions de modules, révocation
- ⬜ Modules installés : activation, risque, mise à jour, désinstallation
- ⬜ Baisse du risque d'un outil : avertissement, disclaimer de responsabilité, saisie de « Je sais ce que je fais » ; baisses listées et annulables dans l'écran Permissions
- ⬜ Magasin de modules (P2 : dépend de l'hébergement et de la signature)
- ⬜ Magasin de modèles et sélecteur de modèle, toujours visible (P2, après le magasin en CLI du jalon 3)
- ⬜ Écran Identité et présence : personnes autorisées, moyens de vérification, comportements, verrouillage (jalon 7)

---

## Jalon 7 — Identité et présence ⬜

*But : Avenoir n'obéit qu'aux personnes autorisées et protège l'ordinateur en leur absence (PROJECT.md § 2.10, directive de sécurité n° 16). Tout est optionnel et réglable ; les réglages par défaut sont les plus sûrs.*

- ⬜ **P1** Propriétaire et liste blanche : inscription par le propriétaire vérifié, depuis l'application ou la CLI, avec confirmation ; retrait ; liste consultable
- ⬜ **P1** Vérification du locuteur sur chaque requête vocale (timbre, rythme, prosodie…), en local
- ⬜ **P1** Reconnaissance faciale en local avec détection du vivant (photo, vidéo, écran refusés ; infrarouge si la caméra le permet)
- ⬜ **P1** Réglages : voix, visage, les deux (et / ou) ; personne non reconnue → refus (défaut), lecture seule ou confirmation du propriétaire ; caméra indisponible → assistant inutilisable (défaut) ou reconnaissance faciale sautée
- ⬜ **P1** Données biométriques locales uniquement, chiffrées (clé du trousseau), jamais données à un LLM ni à un module, jamais journalisées, effaçables ; chaque vérification journalisée
- ⬜ **P1** Voix seule jamais suffisante pour une action `sensitive` ni pour déverrouiller
- ⬜ **P2** Verrouillage automatique : visage absent pendant un délai réglable, ou inactivité suivie d'une question orale sans réponse reconnue (`lock_screen` du jalon 2)
- ⬜ **P2** Déverrouillage automatique de la session à la reconnaissance du visage (désactivé par défaut)
- ⬜ **P3** Connexion automatique à l'écran de connexion du système : intégration Windows Hello ou fournisseur d'identification sous Windows, module PAM sous Linux (à la manière de Howdy), sans stocker le mot de passe ; mot de passe toujours possible
- ⬜ **P1** Tests : photo et vidéo du propriétaire refusées, voix enregistrée refusée pour une action sensible, caméra débranchée selon chaque réglage

**Fini quand :** une personne hors liste blanche ne peut rien faire, le poste se verrouille quand le propriétaire s'en va et se déverrouille à son retour, et chaque réglage se change depuis l'application.

---

## Décisions à prendre (bloquantes plus tard)

| Question | Bloque | Quand trancher |
|---|---|---|
| Protocole `avenoir` v1 : messages exacts (poignée de main, appels, notifications) | 1.6 | début du jalon 1.6 |
| Moteur de recherche web par défaut | 2.3 | début du jalon 2 |
| Framework web et bibliothèque C++ de vue web | jalon 6 | fin du jalon 3 |
| Format des composants déclaratifs des onglets | 6.3 | début du jalon 6 |
| Magasin de modules : hébergement, signature, vérification | 6.4 | jalon 4 |
| Magasin de modèles : source de la liste, Ollama seul ou aussi GGUF | 3 | fin du jalon 2 |
| Bibliothèques locales de vérification du locuteur et de reconnaissance faciale (détection du vivant) ; intégration à la connexion du système | 7 | fin du jalon 5 |

---

## Prochaines actions

Par ordre conseillé :

1. ~~Projet CMake + vcpkg + préréglages + un test qui passe (1.1)~~ ✅
2. ~~CI au vert sur GitHub + release `v0.1.0` (1.2)~~ ✅
3. ~~Cœur : messages, outils, permissions, agent avec LLM scripté (1.3)~~ ✅
4. ~~Fournisseurs LLM + CLI minimale : premier dialogue de bout en bout (1.4)~~ ✅
5. ~~Outils natifs (1.5) ; confirmations, Ctrl+C, annulation et `/annuler` dans la CLI (1.7)~~ ✅
6. ~~Conversations persistantes (1.9)~~ ✅
7. ~~Hôte de modules v1 et module météo (1.6)~~ ✅
8. ~~Essais avec de vrais modèles et `docs/FONCTIONNEMENT.md` (1.8)~~ ✅
9. Jalon 2 — Assistant du quotidien : contrôle système, applications installées, recherche web (SearXNG local), réponses au fil de l'eau, Claude Code persistant, changer de LLM à tout moment — toutes les tâches faites ; reste à vérifier sous Windows l'instance SearXNG locale (podman ou docker) et la session Claude Code pour clore le jalon
10. Jalon 3 — Résident (démon, IPC, journal structuré, permissions durables, réglages scellés)
11. Jalon 4 — Orchestrateur multi-agents et tâches avancées
