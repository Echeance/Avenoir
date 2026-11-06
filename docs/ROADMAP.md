# Roadmap d'Avenoir

> Plan de travail détaillé, découpé en jalons livrables. La vision et les grandes phases sont dans [`PROJECT.md`](../PROJECT.md) (§ 6) ; ce document dit **quoi faire, dans quel ordre, et quand c'est fini**.
>
> Légende : ✅ fait · 🔄 en cours · ⬜ à faire · **P1** indispensable · **P2** important · **P3** confort

---

## Vue d'ensemble

| Jalon | Objectif | État |
|---|---|---|
| **0. Conception** | Vision, architecture, directives, choix de la stack | ✅ |
| **1. Socle** | Projet CMake, CI, cœur (agent, permissions, voie rapide), LLM, premiers outils, MCP, CLI | 🔄 prochain |
| **2. Assistant du quotidien** | Contrôle système (volume, écran, multimédia), recherche web, applications installées | ⬜ |
| **3. Résident** | Démon en arrière-plan, config et logs aux emplacements standards, raccourci global | ⬜ |
| **4. Tâches et plugins** | Fichiers, mémoire, contexte long, client MCP complet, permissions par plugin | ⬜ |
| **5. Voix** | Mot de réveil, STT, TTS, confirmations vocales | ⬜ |
| **6. Interface graphique** | Icône de notification, fenêtre de conversation, écran de configuration | ⬜ |

L'ordre suit un principe : **chaque jalon produit un assistant utilisable**, plus capable que le précédent. La voix et l'UI viennent tard parce qu'elles reposent sur un cœur stable (PROJECT.md, décision 7).

---

## Jalon 0 — Conception ✅

- ✅ Vision, principes directeurs, directives de sécurité (`PROJECT.md`)
- ✅ Architecture : agent + voie rapide + permissions + outils / plugins MCP
- ✅ Stack : C++23, CMake, vcpkg, `std::expected`, modèle synchrone + threads
- ✅ Fournisseurs LLM prévus : OpenAI-compatible, Anthropic, Claude Code (abonnement)
- ✅ Format de configuration (`config.example.toml`)
- ✅ Instructions de travail (`CLAUDE.md`)

---

## Jalon 1 — Socle 🔄

*But : un assistant texte complet, sûr et testé, sur Windows comme sur Linux.*

### 1.1 Projet et outillage — P1 🔄
- ✅ `CMakeLists.txt` racine : C++23, avertissements stricts en erreurs, options `AVENOIR_BUILD_TESTS`, `AVENOIR_SANITIZE`
- ✅ `cmake/AvenoirOptions.cmake` : cible `avenoir::options` (avertissements, sanitizers) et fonctions `avenoir_add_library` / `avenoir_add_tests`
- ✅ `CMakePresets.json` : `debug`, `release`, `asan` ; générateur Ninja ; toolchain vcpkg
- ✅ `vcpkg.json` : nlohmann-json, tomlplusplus, cpr, reproc, spdlog, catch2 (baseline figée)
- ✅ `.clang-format`, `.clang-tidy` (conventions de nommage vérifiées), `.gitignore`
- ✅ `avenoir::core` (version) + `apps/cli` (affiche la version) + premier test Catch2. Les autres bibliothèques sont créées à leur étape (1.4 à 1.6)
- ✅ Chaîne complète vérifiée sous Linux avec vcpkg (`debug` et `asan` : build sans avertissement, tests, clang-tidy)
- ⬜ Même vérification sous Windows (MSVC) — sera couverte par la CI (1.2)

**Fini quand :** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` fonctionne sur Linux et Windows.

### 1.2 Intégration continue — P1
- ⬜ GitHub Actions : matrice **Linux (GCC + Clang) + Windows (MSVC)**
- ⬜ Build sans avertissement, `ctest`, vérification `clang-format`
- ⬜ Passe ASan + UBSan sous Linux
- ⬜ Cache vcpkg (binary caching) pour des CI rapides

**Fini quand :** chaque push sur `main` est vérifié sur les deux OS, badge vert dans le README.

### 1.3 Cœur (`avenoir::core`) — P1
- ⬜ Messages (`User`, `Assistant`, `ToolResult`) et `ToolCall`
- ⬜ `Tool`, `ToolSpec`, `RiskLevel`, `ToolRegistry` (refus des doublons)
- ⬜ `PermissionPolicy` (`sensitive` jamais `allow`), `ConfirmationHandler`, `AutoReject`
- ⬜ `FastRouter` : règles « ouvre <url> », « lance <app> », « quelle heure est-il »
- ⬜ `Agent::handle` : voie rapide, boucle LLM ↔ outils, `max_steps`, résultats tronqués, erreurs renvoyées au LLM en texte, audit
- ⬜ `AppConfig` lu depuis TOML ; test qui charge `config.example.toml`
- ⬜ Tests : LLM scripté, action sensible refusée non exécutée, troncature, règles de la voie rapide

### 1.4 Fournisseurs LLM (`avenoir::llm`) — P1
- ⬜ `openai_compatible` (cpr + JSON) — testé avec Ollama
- ⬜ `anthropic` (fusion des messages consécutifs de même rôle)
- ⬜ `claude_code` : `claude -p` via `reproc`, outils de Claude Code désactivés, clés d'API retirées de l'environnement, prompt sur stdin, `--json-schema`, dossier de travail temporaire
- ⬜ Tests de conversion des messages et de lecture des réponses, sans réseau

### 1.5 Outils natifs (`avenoir::tools`) — P1
- ⬜ `current_time` : phrase française (« Il est 13 h 41, vendredi 6 novembre 2026. »)
- ⬜ `system_info`
- ⬜ `launch_app` : nom validé strictement ; Linux `gtk-launch` puis PATH ; Windows **`ShellExecuteW`** (jamais `cmd.exe`)
- ⬜ `open_url` : `http(s)` uniquement ; Linux `xdg-open` ; Windows `ShellExecuteW`
- ⬜ Tests de validation, y compris tentatives d'injection

### 1.6 Client MCP (`avenoir::mcp`) — P1
- ⬜ Lancement du serveur (`reproc`), JSON-RPC sur stdio, thread de lecture, délai max par requête
- ⬜ `initialize`, `tools/list`, `tools/call` ; outils exposés en `plugin__outil` (nom assaini)
- ⬜ stderr de chaque plugin redirigé vers un log dédié
- ⬜ Un plugin défaillant n'empêche pas le démarrage

### 1.7 CLI (`apps/cli`) — P1
- ⬜ Boucle de saisie, commandes `/outils`, `/reset`, `/quitter`
- ⬜ Confirmations au terminal (« Autoriser ? [o/N] »)
- ⬜ Ctrl+C annule la requête en cours sans quitter (`std::stop_token`)
- ⬜ P3 : historique et édition de ligne (`replxx` ou `linenoise-ng`), indicateur « réflexion en cours… », `/config`

### 1.8 Essais avec de vrais modèles — P1
- ⬜ Jeu de ~20 requêtes de référence (simples, multi-outils, ambiguës, tentatives d'injection) dans `evals/`
- ⬜ Les passer sur : Ollama (qwen3 8B), `claude_code` (haiku, sonnet), une API distante
- ⬜ Ajuster le prompt système et les descriptions d'outils selon les échecs constatés
- ⬜ Noter dans PROJECT.md les modèles recommandés (local / distant)
- ⬜ Rédiger `docs/FONCTIONNEMENT.md` : comment le code s'articule (démarrage, trajet d'une requête, rôle de chaque bibliothèque)

**Fini quand :** les requêtes de référence passent avec au moins un modèle local et un modèle distant, sur Linux et Windows.

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
- ⬜ **P1** Journal d'audit dans un fichier dédié, avec rotation (`spdlog` rotating sink)
- ⬜ **P1** Mode démon : unité systemd utilisateur / service Windows (ou démarrage à l'ouverture de session)
- ⬜ **P1** IPC locale (socket Unix / named pipe) entre le démon et les interfaces ; la CLI devient un client
- ⬜ **P2** Réévaluer le modèle de concurrence (décision 12) pour le démon
- ⬜ **P2** Raccourci clavier global qui ouvre une saisie rapide
- ⬜ **P2** Mode « local uniquement » : refuse tout fournisseur distant (directive de sécurité n° 10)
- ⬜ **P3** Secrets dans le trousseau du système (Credential Manager, Secret Service)

---

## Jalon 4 — Tâches et plugins avancés ⬜

- ⬜ **P1** Outils fichiers (lister, lire, déplacer, renommer, ranger) **restreints à des dossiers autorisés** dans la config, chemins canonicalisés (`std::filesystem::weakly_canonical`)
- ⬜ **P1** Suppression = `sensitive`, avec un résumé avant confirmation (« 12 fichiers dans Téléchargements »)
- ⬜ **P2** Gestion du contexte long : résumé automatique de l'historique au-delà d'un seuil
- ⬜ **P2** Client MCP complet (ressources, prompts, HTTP, `list_changed`) ou bibliothèque C++ existante
- ⬜ **P2** Permissions déclarées par plugin (réseau, fichiers)
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

- ⬜ Choisir la technologie (Qt 6 / QML, Dear ImGui ou WebView) — question ouverte
- ⬜ Icône dans la zone de notification, fenêtre de conversation
- ⬜ Confirmations en popup (`ConfirmationHandler` graphique)
- ⬜ Écran de configuration et gestion des plugins

---

## Décisions à prendre (bloquantes plus tard)

| Question | Bloque | Quand trancher |
|---|---|---|
| Licence (proposée : MIT OR Apache-2.0) | publication, contributions | jalon 1 |
| Client MCP : bibliothèque existante ou maison ? | 1.6 / 4 | début du jalon 1.6 |
| Moteur de recherche web par défaut | 2.3 | début du jalon 2 |
| Technologie d'UI | jalon 6 | fin du jalon 3 |
| Catalogue / registre de plugins ? | diffusion des plugins | jalon 4 |

---

## Prochaines actions

Par ordre conseillé :

1. Projet CMake + vcpkg + préréglages + un test qui passe (1.1)
2. CI GitHub Actions Linux + Windows (1.2)
3. Cœur : messages, outils, permissions, agent avec LLM scripté (1.3)
4. Fournisseur `openai_compatible` + outil `current_time` + CLI minimale : premier dialogue de bout en bout (1.4, 1.5, 1.7)
5. Les autres fournisseurs, outils et le client MCP
