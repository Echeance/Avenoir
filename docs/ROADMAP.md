# Roadmap d'Avenoir

> Plan de travail détaillé, découpé en jalons livrables. La vision et les grandes phases sont dans [`PROJECT.md`](../PROJECT.md) (§ 6) ; ce document dit **quoi faire, dans quel ordre, et quand c'est fini**.
>
> Légende : ✅ fait · 🔄 en cours · ⬜ à faire · **P1** indispensable · **P2** important · **P3** confort

---

## Vue d'ensemble

| Jalon | Objectif | État |
|---|---|---|
| **0. Conception** | Vision, architecture, directives, choix de la stack | ✅ |
| **1. Socle** | Projet CMake, CI, cœur (agent, permissions, voie rapide), LLM, premiers outils, hôte de modules v1, CLI, conversations persistantes | 🔄 en cours |
| **2. Assistant du quotidien** | Contrôle système (volume, écran, multimédia), recherche web, applications installées | ⬜ |
| **3. Résident** | Démon en arrière-plan, IPC, journal structuré, permissions durables et révocables | ⬜ |
| **4. Tâches et modules** | Fichiers, mémoire, contexte long, appels entre modules, fonctions de fond | ⬜ |
| **5. Voix** | Mot de réveil, STT, TTS, confirmations vocales | ⬜ |
| **6. Interface graphique** | Widget en haut de l'écran (pingouin animé), application principale, onglets de modules | ⬜ |

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

## Jalon 1 — Socle 🔄

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
- ⬜ `openai_compatible` testé avec un vrai serveur (Ollama non lancé lors de l'étape) — à faire en 1.8
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

### 1.6 Hôte de modules v1 (`avenoir::modules`) — P1
- ⬜ Découverte des modules : `<installation>/modules/` et dossier utilisateur ; lecture et validation de `module.toml` (PROJECT.md § 2.6)
- ⬜ Lancement du module (`reproc`), JSON-RPC 2.0 sur stdio, thread de lecture, délai max par requête
- ⬜ Protocole `avenoir` v1 : poignée de main, appel d'outil ; outils exposés en `module__outil` ; risque du manifeste relevable par l'utilisateur, `sensitive` si absent
- ⬜ Commandes (`/meteo`) et règles de voie rapide déclarées par les modules (limitées à leurs propres outils)
- ⬜ Compatibilité MCP (`protocol = "mcp"`) : `initialize`, `tools/list`, `tools/call`
- ⬜ stderr de chaque module redirigé vers un log dédié ; un module défaillant n'empêche pas le démarrage
- ⬜ Configuration `[modules.<id>]` (activation, risque relevé) ; remplace `[[mcp_servers]]` et `McpServerConfig` dans `config.hpp`
- ⬜ Baisse de risque : prise en compte seulement avec un accord enregistré (sinon ignorée + avertissement) ; commande CLI qui affiche avertissement et disclaimer et exige « Je sais ce que je fais »
- ⬜ Annulation pour les outils de modules : action d'annulation renvoyée par l'outil, exécutée par `avenoir/undo`
- ⬜ Un module d'exemple (ex. météo factice) qui sert de test d'intégration

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
- ✅ Apparence (PROJECT.md § 2.9) : barre de saisie fixe en bas du terminal (zone de défilement DECSTBM ; grandit pour les saisies longues ; suit les redimensionnements, même pendant la frappe ; confirmations dans la barre ; terminal restauré à la sortie ; Linux et Windows, avec ou sans couleurs), toupie avec les réponses, réponses découpées aux mots ; test de bout en bout de la CLI lancé par ctest sur Linux et Windows, cadre d'accueil avec pingouin en pixel art (demi-blocs colorés ; ASCII sans couleurs), styles distincts (requête `❯`, réponse `◆ Avenoir`, action `⚙ … ✓/✗` en direct, confirmation encadrée, badges de risque, erreurs), Markdown rendu, toupie « Réflexion… » ; bibliothèque `avenoir::terminal` (11 tests) ; couleurs désactivées hors terminal et avec `NO_COLOR` ; journal d'audit masqué par défaut (`AVENOIR_LOG=info`)
- ⬜ P3 : `/config`

### 1.8 Essais avec de vrais modèles — P1
- ⬜ Jeu de ~20 requêtes de référence (simples, multi-outils, ambiguës, tentatives d'injection) dans `evals/`
- ⬜ Les passer sur : Ollama (qwen3 8B), `claude_code` (haiku, sonnet), une API distante
- ⬜ Ajuster le prompt système et les descriptions d'outils selon les échecs constatés
- ⬜ Noter dans PROJECT.md les modèles recommandés (local / distant)
- ⬜ Rédiger `docs/FONCTIONNEMENT.md` : comment le code s'articule (démarrage, trajet d'une requête, rôle de chaque bibliothèque)

**Fini quand :** les requêtes de référence passent avec au moins un modèle local et un modèle distant, sur Linux et Windows.

### 1.9 Conversations persistantes (`avenoir::storage`) — P1 ✅
- ✅ Dépendance `sqlite3` (vcpkg) ; base `avenoir.db` dans le dossier de données de l'utilisateur (dossier en `700`, base en `600`)
- ✅ Schéma versionné (`PRAGMA user_version`) : conversations (titre, dates, compteur d'activité pour un ordre fiable), messages (rôle, contenu JSON avec appels et résultats d'outils et consommation, horodatage) ; suppression en cascade ; WAL
- ✅ Écriture immédiate de chaque message, requêtes toujours paramétrées (aucune injection SQL possible)
- ✅ Agent : `load_history` et observateur de l'historique (le cœur ne dépend pas du stockage) ; consommation conservée dans chaque réponse
- ✅ CLI : reprise de la dernière conversation au lancement, réaffichage des 50 derniers messages
- ✅ CLI : `/nouvelle [titre]` (et `/reset`, alias), `/conversations`, `/ouvrir <n° ou titre>`, `/renommer <titre>`, `/supprimer [n° ou titre]` (avec confirmation), `/historique`
- ✅ Titre automatique (premiers mots de la première question, sans couper un caractère UTF-8)
- ✅ Tests : aller-retour JSON de chaque message, données conservées après fermeture, renommage, suppression en cascade, titres piégés (injection SQL), ordre des conversations, observateur et reprise dans l'agent
- ✅ Essai réel : conversation, fermeture, réouverture — le LLM se souvient ; toutes les commandes essayées

**Fini quand :** on ferme la CLI au milieu d'une conversation, on la rouvre, et tout est là ; on peut créer, lister, ouvrir, renommer et supprimer des conversations.

---

## Jalon 2 — Assistant du quotidien ⬜

*But : couvrir les demandes qu'on fait vraiment tous les jours.*

### 2.1 Contrôle système — P1
| Outil | Risque | Linux | Windows |
|---|---|---|---|
| `set_volume` / `mute` | reversible | PipeWire / PulseAudio (D-Bus, `sdbus-c++`) | Core Audio (`IAudioEndpointVolume`) |
| `media_control` (lecture, pause, suivant) | reversible | MPRIS (D-Bus) | SMTC (C++/WinRT) |
| `set_brightness` | reversible | logind / sysfs (D-Bus) | WMI |
| `set_dark_mode` | reversible | portail XDG / gsettings | registre |
| `lock_screen` | reversible | logind (D-Bus) | `LockWorkStation` |
| `notify` | read_only | notifications D-Bus | toast Windows |
| `power` (veille, extinction, redémarrage) | **sensitive** | logind | `ExitWindowsEx` |

- ⬜ Règles de voie rapide associées (« monte le son », « coupe le son », « pause »…)
- ⬜ Un outil indisponible sur un OS n'est pas enregistré (PROJECT.md § 4.3)

### 2.2 Applications installées — P1
- ⬜ Index des applications : fichiers `.desktop` (Linux), menu Démarrer / App Paths (Windows)
- ⬜ `launch_app` tolérant : « ouvre le navigateur », « lance vscode » → bonne application
- ⬜ Outil `list_apps` (read_only) pour que le LLM puisse chercher

### 2.3 Recherche web — P1
- ⬜ Choisir le moteur par défaut (question ouverte de PROJECT.md § 8) : SearXNG auto-hébergé, Brave Search API ou Tavily
- ⬜ Outil `web_search` (read_only) → titres, URL, extraits
- ⬜ Outil `read_webpage` (read_only) → texte principal de la page, tronqué
- ⬜ Contenu web toujours traité comme donnée (test d'injection dédié)

### 2.4 Streaming des réponses — P2
- ⬜ `LlmProvider` : variante en flux (callback par fragment)
- ⬜ Affichage progressif dans la CLI (et plus tard TTS phrase par phrase)

### 2.5 Fournisseur `claude_code` — P2
- ⬜ Réduire la latence : garder un processus `claude` ouvert en mode `--input-format stream-json` au lieu d'un lancement par appel
- ⬜ Message d'erreur clair si la CLI n'est pas connectée (« lancez `claude` puis `/login` »)

**Fini quand :** « monte le son », « mets en pause », « cherche la météo à Lyon », « ouvre le navigateur » fonctionnent sur les deux OS.

---

## Jalon 3 — Résident ⬜

*But : Avenoir tourne en permanence et répond instantanément.*

- ⬜ **P1** Config à l'emplacement standard : `$XDG_CONFIG_HOME/avenoir/`, `%APPDATA%\avenoir\` ; création guidée au premier lancement
- ⬜ **P1** Réglages modifiables uniquement par Avenoir (PROJECT.md § 5.1) : démon seul rédacteur (via l'IPC), changements journalisés
- ⬜ **P1** Sceau HMAC-SHA256 des réglages, clé dans le trousseau du système (repli : fichier `0600`, signalé)
- ⬜ **P1** Surveillance du fichier : sceau invalide → refus, restauration de la dernière version valide, alerte (avec le détail des changements), journal
- ⬜ **P1** Tests : modification à la main, suppression du sceau, sceau d'une autre version → toutes rejetées et restaurées
- ⬜ **P1** Journal d'audit dans un fichier dédié, avec rotation (`spdlog` rotating sink)
- ⬜ **P1** Mode démon : unité systemd utilisateur / service Windows (ou démarrage à l'ouverture de session)
- ⬜ **P1** IPC locale (socket Unix / named pipe) entre le démon et les interfaces (`avenoir::ipc`) ; la CLI devient un client
- ⬜ **P1** Journal structuré (dans la base de l'étape 1.9) : actions, décisions de permission, module concerné ; historique par conversation et global ; le démon devient le seul à écrire dans la base
- ⬜ **P1** Permissions durables (« toujours autoriser cet outil de ce module ») et révocables ; permissions de module accordées à l'installation, révocables
- ⬜ **P2** Réévaluer le modèle de concurrence (décision 12) pour le démon
- ⬜ **P2** Raccourci clavier global qui ouvre une saisie rapide
- ⬜ **P2** Mode « local uniquement » : refuse tout fournisseur distant (directive de sécurité n° 10)
- ⬜ **P2** Secrets (clés d'API) dans le trousseau du système (Credential Manager, Secret Service), qui sert déjà au sceau des réglages

---

## Jalon 4 — Tâches et modules avancés ⬜

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

---

## Décisions à prendre (bloquantes plus tard)

| Question | Bloque | Quand trancher |
|---|---|---|
| Protocole `avenoir` v1 : messages exacts (poignée de main, appels, notifications) | 1.6 | début du jalon 1.6 |
| Moteur de recherche web par défaut | 2.3 | début du jalon 2 |
| Framework web et bibliothèque C++ de vue web | jalon 6 | fin du jalon 3 |
| Format des composants déclaratifs des onglets | 6.3 | début du jalon 6 |
| Magasin de modules : hébergement, signature, vérification | 6.4 | jalon 4 |

---

## Prochaines actions

Par ordre conseillé :

1. ~~Projet CMake + vcpkg + préréglages + un test qui passe (1.1)~~ ✅
2. ~~CI au vert sur GitHub + release `v0.1.0` (1.2)~~ ✅
3. ~~Cœur : messages, outils, permissions, agent avec LLM scripté (1.3)~~ ✅
4. ~~Fournisseurs LLM + CLI minimale : premier dialogue de bout en bout (1.4)~~ ✅
5. ~~Outils natifs (1.5) ; confirmations, Ctrl+C, annulation et `/annuler` dans la CLI (1.7)~~ ✅
6. ~~Conversations persistantes (1.9)~~ ✅
7. Hôte de modules v1 (1.6), puis essais avec de vrais modèles (1.8)
