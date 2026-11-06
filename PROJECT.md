# Avenoir — Assistant de système d'exploitation

> Document de référence du projet : vision, architecture, décisions et directives.
> À tenir à jour à chaque décision structurante.

---

## 1. Vision

Un assistant **résident** pour **Windows et Linux**, piloté à la **voix** ou au **clavier**, capable de :

| Capacité | Exemples |
|---|---|
| Contrôle système | « monte le son », « verrouille l'écran », « passe en mode sombre » |
| Lancement d'applications | « ouvre Firefox », « lance VS Code » |
| Recherche internet | « cherche la météo à Lyon », « trouve la doc de std::expected » |
| Questions ouvertes | « explique-moi la différence entre TCP et UDP » |
| Exécution de tâches | « range mes téléchargements par type », « fais-moi un résumé de ce PDF » |

Et **extensible** par des **modules** que n'importe qui peut écrire, dans n'importe quel langage : chacun apporte ses outils, ses commandes, ses fonctions de fond et, s'il le souhaite, son onglet dans l'interface (ex. un module météo qui récupère et affiche la météo, un module calendrier que l'assistant sollicite pour « déplace ma réunion de demain »).

### Principes directeurs

1. **Sûr par défaut** : l'assistant agit sur une vraie machine. Aucune action sensible sans confirmation explicite.
2. **Le LLM au choix de l'utilisateur** : local (Ollama, llama.cpp…) ou distant (Anthropic, OpenAI, Mistral…), sélectionné par configuration.
3. **Réactif** : les commandes simples ne passent pas par le LLM.
4. **Léger** : un démon en arrière-plan doit consommer peu de ressources.
5. **Standards plutôt que formats maison** : JSON-RPC (compatible MCP) pour les modules, JSON Schema pour les outils.

---

## 2. Architecture

### 2.1 Vue d'ensemble

Trois couches, dans des processus distincts :

```
┌─ Démon Avenoir (toujours actif) ───────────────────────────────────┐
│  Agent (LLM + outils) · Permissions · Voie rapide · Commandes      │
│  Hôte de modules · Journal structuré (conversations, actions)      │
└───────┬───────────────────────────────────────────────┬────────────┘
        │ IPC locale                                    │ JSON-RPC (stdio)
┌───────┴───────────────── Interface ─────────┐   ┌─────┴──────────────┐
│  Hôte C++ des fenêtres + vue web système    │   │ Modules            │
│  ├─ Widget (haut de l'écran, plié/déplié)   │   │ (processus séparés │
│  └─ Application principale                  │   │  ou serveurs MCP)  │
└─────────────────────────────────────────────┘   └────────────────────┘
```

- **Démon** : le cerveau. Il tourne en permanence, exécute les requêtes, héberge les modules et tient le journal. Les interfaces (widget, application, CLI, voix) en sont des clients.
- **Interface** : un seul processus, deux fenêtres (le widget et l'application principale), qui partagent l'état et la connexion au démon. Voir § 2.7.
- **Modules** : programmes externes qui étendent l'assistant. Voir § 2.6.

### 2.2 Traitement d'une requête

```
            ┌──────────── Entrées ────────────┐
            │  Micro → mot de réveil → STT    │   (avenoir_voice, phase 5)
            │  Clavier : CLI, widget          │   (avenoir-cli, widget)
            └───────────────┬─────────────────┘
                            │ texte
                            ▼
                 ┌─────────────────────┐
                 │  Voie rapide        │── correspond ──┐
                 │  (FastRouter)       │                │
                 └─────────┬───────────┘                │
                           │ sinon                      │
                           ▼                            │
                 ┌─────────────────────┐                │
                 │  Boucle agent       │◄──────┐        │
                 │  LLM + outils       │       │        │
                 └─────────┬───────────┘       │        │
                           │ appel d'outil     │résultat│
                           ▼                   │        ▼
                 ┌─────────────────────────────┴──────────────┐
                 │  Contrôle des permissions                  │
                 │  RiskLevel × PermissionPolicy → Allow /    │
                 │  Confirm (ConfirmationHandler) / Deny      │
                 └─────────┬──────────────────────────────────┘
                           ▼
          ┌────────────────┴──────────────────┐
          │ Outils natifs      Outils modules │
          │ (avenoir_tools)  (avenoir_modules)│
          └───────────────────────────────────┘
                           │
                           ▼
                Réponse → affichage + TTS
```

### 2.3 Structure du dépôt (cible)

```
avenoir/
├── CMakeLists.txt          Projet racine, options et avertissements communs
├── CMakePresets.json       Préréglages : debug, release, asan (Linux / Windows)
├── vcpkg.json              Dépendances (mode manifeste)
├── config.example.toml     Configuration commentée
├── PROJECT.md              Ce document
├── libs/
│   ├── core/               Cœur, sans dépendance à l'OS, au LLM ou à la voix
│   │   ├── include/avenoir/core/
│   │   │   ├── agent.hpp       Boucle agent + exécution sous contrôle des permissions
│   │   │   ├── tool.hpp        Interface Tool, ToolSpec, RiskLevel, ToolRegistry
│   │   │   ├── llm.hpp         Interface LlmProvider
│   │   │   ├── message.hpp     Historique de conversation (format interne)
│   │   │   ├── permission.hpp  PermissionPolicy, ConfirmationHandler
│   │   │   ├── router.hpp      Voie rapide (règles regex)
│   │   │   └── config.hpp      AppConfig (TOML)
│   │   ├── src/
│   │   └── tests/
│   ├── llm/                Fournisseurs : API compatible OpenAI, Anthropic, Claude Code
│   ├── tools/              Outils natifs ; code par OS dans src/platform/{linux,windows}.cpp
│   ├── modules/            Hôte de modules : manifestes, processus JSON-RPC, compatibilité MCP
│   ├── ipc/                Protocole et transport démon ↔ interfaces
│   └── voice/              Interfaces voix (déclarations uniquement au départ)
├── apps/
│   ├── cli/                Interface texte (exécutable `avenoir`)
│   ├── daemon/             Démon résident (exécutable `avenoird`)
│   └── ui/                 Hôte C++ des fenêtres (widget + application), vue web système
└── ui/                     Interface web (TypeScript) : widget, application, rendu des onglets
```

Chaque dossier de `libs/` est une bibliothèque CMake (`avenoir::core`, `avenoir::llm`…) avec ses en-têtes publics dans `include/avenoir/<nom>/` et ses tests dans `tests/`.

**Règle de dépendance** : `avenoir::core` ne dépend d'aucune autre bibliothèque interne. Toutes les autres dépendent de `avenoir::core`. Les applications (`apps/`) assemblent le tout. L'interface web (`ui/`) ne parle qu'à l'hôte C++ des fenêtres, jamais directement au démon ni aux modules.

### 2.4 Concepts clés

**`Tool`** : tout ce que l'assistant peut faire. Classe abstraite qui fournit une `ToolSpec` (nom, description pour le LLM, JSON Schema des arguments, niveau de risque) et une méthode `call(args) -> std::expected<ToolOutput, ToolError>`. Outils natifs et outils de modules implémentent la même interface.

**`RiskLevel`** :

| Niveau | Définition | Exemples |
|---|---|---|
| `read_only` | Aucun effet de bord | heure, infos système, lecture de fichier |
| `reversible` | Effet de bord annulable | lancer une app, ouvrir une URL, volume |
| `sensitive` | Destructeur, irréversible, coûteux ou qui sort de la machine | supprimer, envoyer un mail, shell, paiement, éteindre le PC |

**`PermissionPolicy`** : associe à chaque niveau `allow`, `confirm` ou `deny`. Invariant codé en dur : `sensitive` n'est **jamais** `allow`.

**`ConfirmationHandler`** : demande l'accord de l'utilisateur. L'implémentation dépend de l'interface : terminal (CLI), popup (UI), question vocale (voix), `AutoReject` (mode sans surveillance).

**`LlmProvider`** : une méthode `complete(system, messages, tools) -> (texte, appels d'outils)`. Le format interne des messages est converti par chaque fournisseur.

**Boucle agent** (`Agent::handle`) :
1. voie rapide si une règle correspond ;
2. sinon, envoi au LLM avec la liste des outils ;
3. pour chaque appel d'outil : contrôle des permissions → exécution → résultat renvoyé au LLM (les erreurs aussi, en texte, pour que le modèle s'adapte) ;
4. répéter jusqu'à une réponse sans appel d'outil, ou `max_steps`.

**Voie rapide** (`FastRouter`) : règles regex strictes pour les commandes fréquentes. Elle passe **par le même contrôle de permissions** que le LLM. En cas de doute, la requête part au LLM. Sa réponse est le texte de l'outil, affiché tel quel (sans LLM) : les outils concernés renvoient donc des phrases françaises lisibles, y compris à voix haute.

### 2.5 Fournisseurs LLM

| `provider` | Fonctionnement | Coût |
|---|---|---|
| `openai_compatible` | HTTP `/chat/completions` (Ollama, llama.cpp server, LM Studio, vLLM, OpenAI, Mistral, OpenRouter…), appels d'outils natifs | gratuit en local, sinon au token |
| `anthropic` | HTTP API Messages, appels d'outils natifs | au token (clé d'API) |
| `claude_code` | Lance la CLI officielle `claude -p` connectée à l'abonnement de l'utilisateur. Ses outils, plugins, hooks et réglages sont désactivés (`--tools ""`, `--strict-mcp-config`, `--setting-sources ""`) ; les outils d'Avenoir sont décrits dans le prompt et la réponse est imposée par `--json-schema` | inclus dans l'abonnement Pro / Max |

### 2.6 Modules

Un **module** étend l'assistant : outils pour le LLM, commandes, règles de voie rapide, fonctions de fond, contenu d'interface. C'est un **programme séparé** (n'importe quel langage) lancé par le démon, qui dialogue avec lui en **JSON-RPC 2.0 sur stdio**. Un module qui plante ne fait pas tomber l'assistant.

**Emplacement** : un sous-dossier par module dans `<installation>/modules/` (modules fournis) ou dans le dossier utilisateur (`%APPDATA%\avenoir\modules`, `~/.local/share/avenoir/modules`), qui ne demande pas de droits administrateur.

**Manifeste** `module.toml` : tout ce que le module déclare, lu **avant** de le lancer.

```toml
id = "meteo"                         # unique ; préfixe de ses outils : meteo__…
name = "Météo"
version = "1.0.0"
run = ["meteo"]                      # commande lancée (relative au dossier du module)
protocol = "avenoir"                 # ou "mcp" : serveur MCP existant (outils seulement)
permissions = ["network", "ui.web"]  # montrées à l'installation, révocables

[[tools]]                            # appelables par le LLM (et d'autres modules si public)
name = "get_forecast"                # → « meteo__get_forecast »
risk = "read_only"                   # proposé ; l'utilisateur peut le relever
public = true                        # accessible aux autres modules

[[commands]]                         # commande d'interface : /meteo
name = "meteo"
tool = "get_forecast"

[[fast_path]]                        # voie rapide, limitée aux outils du module
pattern = '^(?:quel temps fait-il|météo)\s*\??$'
tool = "get_forecast"

[[tabs]]                             # onglet dans le widget (§ 2.7)
id = "today"
title = "Météo"
kind = "declarative"                 # ou "web" (exige la permission ui.web)
```

**Fonctionnement** :
- l'**hôte de modules** (dans le démon) découvre les dossiers, valide les manifestes, lance les modules activés et enregistre leurs outils, commandes, règles et onglets dans les registres du cœur ;
- **appels entre modules** : toujours **via l'hôte** (`avenoir/call_tool`), jamais en direct ; seuls les outils `public` sont accessibles ; l'appel subit le même contrôle des permissions et le même audit que le LLM, avec l'identité du module appelant ;
- **fonctions de fond** : un module peut travailler en continu (ex. récupérer la météo toutes les 30 min) et pousser des mises à jour de son onglet à l'hôte (notifications JSON-RPC) ;
- **découverte par le LLM** : les outils des modules apparaissent dans la liste des outils de l'agent (ex. `calendrier__update_event`). Quand les modules seront nombreux, un outil de recherche de capacités évitera d'envoyer toute la liste au LLM ;
- **compatibilité MCP** : `protocol = "mcp"` permet d'utiliser un serveur MCP existant comme module (outils seulement : `initialize`, `tools/list`, `tools/call`) ;
- **risque** : celui du manifeste n'est qu'une proposition ; l'utilisateur peut le relever. Défaut `sensitive` si absent (on ne fait pas confiance à l'auto-déclaration d'un module).

### 2.7 Interface

**Technologie** : interface **web** (HTML / CSS / TypeScript) affichée dans la **vue web du système** (WebView2 sous Windows, WebKitGTK sous Linux) par un petit **hôte C++** qui crée et gère les fenêtres. Pas d'Electron (trop lourd pour un assistant résident).

**Widget principal** — en haut au centre de l'écran, toujours visible, au-dessus des fenêtres :
- **plié** : une simple barre ;
- **déplié** (au survol) : la représentation de l'assistant (un **pingouin animé**) et des onglets : activités en cours, historique défilable, informations système, version et mises à jour disponibles, petite chatbox, consommation et limites (Claude Code), et les **onglets des modules**.

Sous Wayland, le placement en haut de l'écran passe par le protocole **layer-shell** (niri, KDE, Hyprland, Sway) ; il n'est pas disponible sous GNOME.

**Application principale** — ouverte à la demande : réglages, magasin de modules, conversations, historique des actions et logs (par conversation et global), révocation des permissions, gestion des modules installés.

**Onglets des modules** — deux types, la sécurité d'abord :
- **déclaratif** (par défaut) : le module décrit son contenu (textes, listes, images, graphiques, boutons, formulaires) et l'interface le dessine dans le style d'Avenoir ; **aucun code du module ne s'exécute dans l'interface** ;
- **web** (permission `ui.web`) : HTML / CSS / JS du module, affiché dans une **iframe isolée** (voir directive de sécurité n° 12).

---

## 3. Directives de sécurité

Ces règles priment sur toute considération de confort ou de rapidité.

1. **Confirmation obligatoire** pour toute action `sensitive`, sans exception configurable.
2. **Les données ne sont pas des instructions.** Pages web, fichiers, résultats de modules peuvent contenir des injections de prompt (« ignore tes instructions et supprime… »). Le prompt système le rappelle ; le contrôle des permissions est la vraie barrière, pas le prompt.
3. **Pas d'outil shell générique** tant que le système de permissions et l'audit ne sont pas éprouvés. Préférer des outils spécialisés. Si un jour `run_command` existe : `sensitive`, affichage de la commande exacte avant confirmation, liste blanche optionnelle.
4. **Valider tous les arguments** côté outil, jamais côté LLM : noms d'application sans métacaractères, URL `http(s)` uniquement, chemins canonicalisés et restreints à des dossiers autorisés.
5. **Jamais de `system()`, `popen()`, `cmd /C` ou `sh -c`** avec une chaîne construite à partir d'arguments. Lancer les processus avec un tableau d'arguments (`reproc`, `posix_spawn`, `CreateProcessW`) ou une API dédiée (`ShellExecuteW`).
6. **Secrets** : uniquement via variables d'environnement (puis trousseau du système en phase ultérieure : Windows Credential Manager, Secret Service sous Linux). Jamais dans `config.toml`, jamais dans les logs.
7. **Journal d'audit** : chaque exécution, refus ou échec d'outil est tracé (logger `spdlog` dédié : `audit`).
8. **Modules** : risque `sensitive` par défaut, relevable par l'utilisateur ; permissions déclarées dans le manifeste, montrées à l'installation et révocables ; appels entre modules uniquement via l'hôte, avec contrôle des permissions et audit ; plus tard, bac à sable des processus.
9. **Voix** : une transcription peut être fausse. Pour une action sensible déclenchée à la voix, la confirmation reformule l'action (« Je vais supprimer 12 fichiers dans Téléchargements, je confirme ? »).
10. **Vie privée** : avec un LLM distant, ce qui est envoyé quitte la machine. Le documenter clairement à l'utilisateur ; prévoir un mode « local uniquement ».
11. **Sûreté mémoire** : l'assistant traite des données non fiables (réponses de LLM, pages web, modules). Les règles C++ de la section 4.2 (RAII, pas de mémoire gérée à la main, analyseurs en CI) en font une exigence de sécurité, pas seulement de qualité.
12. **Onglets web des modules** : iframe `sandbox="allow-scripts"` uniquement (pas d'origine partagée, ni popup, ni formulaire, ni navigation de la fenêtre principale) ; une origine par module (`avenoir-module://<id>/`) qui ne sert que les fichiers du module ; CSP stricte **sans accès réseau** (`connect-src 'none'`) : les données passent par le processus du module, soumis à ses permissions ; seul canal : `postMessage`, avec une API minimale (recevoir les données de *son* module, lui envoyer des événements), chaque message étant validé par l'hôte.
13. **Confirmations non imitables** : les demandes de confirmation et de permission sont toujours dessinées par Avenoir, hors de tout contenu de module.

---

## 4. Directives de développement

### 4.1 Langue

- **Identifiants** (types, fonctions, variables) : anglais.
- **Commentaires, documentation, messages utilisateur** : français.
- Les textes destinés à l'utilisateur seront externalisés (i18n) quand une deuxième langue sera nécessaire.

### 4.2 C++

- **C++23**, compilateurs cibles : GCC ≥ 14, Clang ≥ 18, MSVC 19.40+ (Visual Studio 2022 17.10+).
- **CMake ≥ 3.28** avec `CMakePresets.json` ; dépendances via **vcpkg** en mode manifeste (`vcpkg.json`).
- Avertissements stricts et traités comme erreurs : `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` (GCC/Clang), `/W4 /WX /permissive-` (MSVC).
- `clang-format` (style du dépôt, `.clang-format`) et `clang-tidy` (`.clang-tidy`) **sans avertissement** avant chaque commit.
- **Mémoire** : RAII partout ; pas de `new` / `delete` ni de `malloc` / `free` ; `std::unique_ptr` par défaut, `std::shared_ptr` seulement si la propriété est réellement partagée ; pas de pointeur nu propriétaire ; pas de cast C ni de `reinterpret_cast` hors couche système isolée et commentée.
- **Erreurs** : `std::expected<T, Error>` dans les bibliothèques ; pas d'exception qui traverse la frontière d'une bibliothèque (les exceptions des dépendances sont capturées et converties). Les exceptions ne sont tolérées que dans `apps/`, au plus haut niveau.
- **Concurrence** : modèle synchrone + threads (`std::jthread`, `std::stop_token` pour l'annulation) ; aucun état partagé sans synchronisation explicite. Les traitements lourds (STT, TTS) ne bloquent jamais le thread d'interface.
- **Logs** : `spdlog`, niveau contrôlé par la variable `AVENOIR_LOG`.
- **Nommage** : types en `PascalCase`, fonctions et variables en `snake_case`, membres privés suffixés `_`, constantes en `kPascalCase`, espaces de noms `avenoir::<bibliothèque>`.
- En-têtes publics minimaux ; détails d'implémentation dans les `.cpp` ou un espace de noms `detail`.
- Fonctionnalités optionnelles lourdes (moteurs vocaux, UI) derrière des **options CMake** (`AVENOIR_WITH_VOICE`…).
- Bibliothèques de référence : `nlohmann/json`, `toml++`, `cpr` (HTTP), `reproc` (sous-processus), `spdlog`, `Catch2` (tests).

### 4.3 Multiplateforme

- Tout code spécifique à un OS va dans `src/platform/linux.cpp` et `src/platform/windows.cpp`, sélectionnés par CMake, derrière une interface commune (`platform.hpp`), **avec** une implémentation de repli qui renvoie « non supporté ».
- Les outils eux-mêmes restent multiplateformes et appellent `platform::…`.
- Si une fonction n'est pas disponible sur un OS, l'outil n'est **pas enregistré** sur cet OS (le LLM ne doit pas voir un outil inutilisable).
- API de référence : Win32 / WinRT (C++/WinRT), D-Bus via `sdbus-c++` (notifications, MPRIS, NetworkManager, logind), `miniaudio` (audio).
- Sous Windows, chaînes en UTF-8 dans tout le code ; conversion en UTF-16 uniquement à l'appel des API `…W`.
- La CI compile et teste sur **Windows et Linux**.

### 4.4 Écrire un outil natif

1. Choisir un nom `snake_case` court, en anglais, unique.
2. Rédiger une **description pour le LLM** : ce que fait l'outil, quand l'utiliser, exemples d'arguments. C'est elle qui détermine si le modèle s'en sert bien.
3. Définir un JSON Schema minimal avec `required`.
4. Choisir le `RiskLevel` le plus élevé applicable en cas de doute.
5. Lire les arguments depuis le `nlohmann::json` reçu dans une structure typée, puis **valider**.
6. Renvoyer un résultat court et factuel, en phrase française lisible (il est relu par le LLM, affiché tel quel par la voie rapide, et parfois dit à voix haute).
7. Ajouter des tests unitaires sur la validation.
8. L'enregistrer dans `register_native_tools`.

### 4.5 Écrire un module

- Un dossier avec un `module.toml` (§ 2.6) et le programme du module, dans n'importe quel langage.
- Descriptions d'outils claires, schémas stricts, résultats textuels concis, en phrases lisibles.
- Demander le minimum de permissions ; préférer un onglet déclaratif à un onglet web.
- Renvoyer une erreur explicite plutôt que planter.
- Ne rien écrire sur stdout d'autre que du JSON-RPC (les logs vont sur stderr).
- Un serveur MCP existant devient un module avec `protocol = "mcp"`.

### 4.6 Interface web

- **TypeScript** en mode strict pour tout le code de `ui/` ; framework à choisir (§ 8).
- L'interface ne parle qu'à l'hôte C++ des fenêtres, par un pont typé ; aucun accès direct au démon, aux modules ou au réseau.
- Contenu des modules : rendu déclaratif par défaut ; onglets web uniquement dans les iframes isolées (directive n° 12).

### 4.7 Tests

- Tests unitaires `Catch2` dans `libs/<nom>/tests/`, lancés par `ctest`.
- La boucle agent se teste avec un **LLM scripté** (faux `LlmProvider` qui renvoie des réponses préparées), sans réseau.
- Tout outil a des tests de validation d'arguments (cas valides **et** tentatives d'injection).
- Les tests ne doivent jamais lancer de vraie application ni modifier le système.
- En CI : une passe avec AddressSanitizer + UndefinedBehaviorSanitizer sous Linux.

### 4.8 Git

- Branches courtes, messages de commit au format *Conventional Commits* (`feat:`, `fix:`, `refactor:`…).
- Toute décision d'architecture notable : ajout d'une entrée dans la section 7 (« Journal des décisions »).

---

## 5. Configuration

Fichier `config.toml` (voir `config.example.toml`) :

| Section | Rôle |
|---|---|
| `[llm]` | Fournisseur (`openai_compatible`, `anthropic` ou `claude_code`), modèle, URL, **nom** de la variable d'environnement de la clé |
| `[agent]` | `max_steps`, `fast_path`, `system_prompt` |
| `[permissions]` | Politique par niveau de risque |
| `[modules.<id>]` | Réglages de l'utilisateur par module : activation, risque relevé par outil |

Emplacement cible (phase 3) : `%APPDATA%\avenoir\config.toml` sous Windows, `$XDG_CONFIG_HOME/avenoir/config.toml` sous Linux.

---

## 6. Feuille de route

> Plan détaillé, priorités et critères de fin : [`docs/ROADMAP.md`](docs/ROADMAP.md).

### Phase 1 : socle en mode texte
- [x] Projet CMake + vcpkg + préréglages
- [x] CI Windows + Linux au vert, releases automatiques
- [x] Boucle agent, permissions, voie rapide
- [ ] Fournisseurs LLM configurables (OpenAI-compatible, Anthropic, Claude Code)
- [ ] Outils : `current_time`, `system_info`, `launch_app`, `open_url`
- [ ] Hôte de modules v1 (manifeste, JSON-RPC, outils, voie rapide, commandes) + compatibilité MCP
- [ ] CLI avec confirmations
- [ ] Tester avec plusieurs LLM réels (local et distant), ajuster le prompt système

### Phase 2 : contrôle système
- [ ] Volume, luminosité, mode sombre, verrouillage, veille (`sensitive` : extinction, redémarrage)
- [ ] Contrôle multimédia (MPRIS / SMTC)
- [ ] Notifications système
- [ ] Recherche d'applications installées (fichiers `.desktop`, menu Démarrer) pour `launch_app` tolérant
- [ ] Recherche web (module : SearXNG auto-hébergé, ou API Brave/Tavily) + lecture de page
- [ ] Streaming des réponses du LLM

### Phase 3 : résident
- [ ] Emplacement standard de la config, rotation des logs, fichier d'audit
- [ ] Mode démon (service Windows / unité systemd utilisateur) + IPC locale pour les interfaces
- [ ] Journal structuré : historique par conversation et global
- [ ] Permissions durables (« toujours autoriser ») et révocables
- [ ] Raccourci clavier global

### Phase 4 : tâches et modules avancés
- [ ] Outils fichiers restreints à des dossiers autorisés
- [ ] Appels entre modules via l'hôte ; fonctions de fond des modules ; journal par module
- [ ] Gestion du contexte long (résumé de l'historique)
- [ ] Mémoire persistante optionnelle (préférences utilisateur)

### Phase 5 : voix
- [ ] Capture audio (`miniaudio`) + détection d'activité vocale (VAD)
- [ ] STT (`whisper.cpp` ou `sherpa-onnx`)
- [ ] TTS (Piper)
- [ ] Mot de réveil (openWakeWord / sherpa-onnx)
- [ ] Confirmations vocales

### Phase 6 : interface graphique (web dans la vue web système)
- [ ] Hôte C++ des fenêtres (WebView2 / WebKitGTK, layer-shell sous Wayland)
- [ ] Widget en haut de l'écran : barre pliée, widget déplié au survol, pingouin animé
- [ ] Onglets du widget : activités, historique, infos système, version et mises à jour, chatbox, consommation Claude Code
- [ ] Onglets des modules : déclaratifs, puis web isolés
- [ ] Application principale : réglages, magasin de modules, conversations, historiques et logs, permissions, modules installés

---

## 7. Journal des décisions

| # | Décision | Raison |
|---|---|---|
| 1 | C++23 pour le cœur | Démon léger, binaire natif, accès direct aux API Win32 / WinRT et Linux, écosystème mûr pour l'audio et l'inférence locale (whisper.cpp, llama.cpp, ONNX Runtime) |
| 2 | LLM au centre + voie rapide par règles | Questions ouvertes et tâches multi-étapes impossibles avec des règles seules ; règles gardées pour la réactivité |
| 3 | Fournisseur LLM configurable | Liberté local / distant, coût, vie privée |
| 4 | MCP comme protocole de plugins | Standard ouvert, multi-langage, isolation par processus, écosystème existant |
| 5 | Risque des plugins fixé par l'utilisateur, `sensitive` par défaut | Ne pas faire confiance à l'auto-déclaration d'un plugin |
| 6 | `sensitive` jamais en `allow` | Garde-fou non contournable par erreur de configuration |
| 7 | Texte d'abord, voix ensuite | Mettre au point la logique agent sans la complexité audio |
| 8 | Nom du projet : **Avenoir** (exécutable `avenoir`, espace de noms `avenoir`) | Remplace le nom de travail « osa » |
| 9 | Fournisseur `claude_code` : la CLI officielle `claude -p` sert de moteur de langage (outils intégrés, MCP, hooks et réglages désactivés ; outils d'Avenoir décrits dans le prompt ; réponse en sortie structurée) | Utiliser l'abonnement Claude (Pro / Max) sans clé d'API. On passe par la CLI officielle plutôt que de réutiliser le jeton OAuth de l'abonnement, ce qu'Anthropic n'autorise pas pour des applications tierces. Plus lent que l'API (un processus par appel) ; usage personnel |
| 10 | CMake + vcpkg (manifeste) | Standard de fait, bon support de MSVC et des IDE ; dépendances reproductibles sur Windows comme sur Linux |
| 11 | `std::expected` plutôt qu'exceptions dans les bibliothèques | Erreurs explicites dans les signatures, coût prévisible, pas d'exception qui s'échappe d'un thread ou d'un plugin |
| 12 | Modèle synchrone + threads plutôt que coroutines / Asio | La boucle agent est séquentielle par nature ; plus simple à lire et à déboguer. À réévaluer pour le démon (phase 3) |
| 13 | Voie rapide sans LLM, réponses en phrases françaises | Latence quasi nulle et fonctionnement hors ligne ; le texte de l'outil est affiché (et lu) tel quel |
| 14 | Dépôt public, CI GitHub Actions (Linux + Windows) à chaque push, releases publiées automatiquement sur tag `vX.Y.Z` | Actions gratuit et illimité pour un dépôt public ; le code Windows est compilé et testé en continu sans machine Windows ; binaires téléchargeables à chaque version |
| 15 | Licence **PolyForm Strict 1.0.0** (source consultable, pas open source) | Code lisible et utilisable à titre personnel et non commercial ; modification, œuvre dérivée et redistribution soumises à l'accord écrit de l'auteur. GitHub permet toujours de forker un dépôt public, mais la licence n'accorde aucun droit sur ce fork |
| 16 | **Modules** en processus séparés, dialoguant avec le démon en JSON-RPC 2.0 sur stdio, décrits par un manifeste `module.toml` (outils, commandes, voie rapide, onglets, permissions). Étend la décision 4 | Isolation (un module qui plante ne fait pas tomber l'assistant), n'importe quel langage, déclarations vérifiables avant lancement ; MCP seul ne couvre ni l'interface, ni les commandes, ni la voie rapide, ni les appels entre modules |
| 17 | **Compatibilité MCP** : un serveur MCP existant s'utilise comme module (`protocol = "mcp"`, outils seulement) | Profiter de l'écosystème MCP sans en faire la limite du système de modules |
| 18 | **Démon et interface séparés** ; l'interface est un seul processus avec deux fenêtres (widget et application principale) | Le démon reste léger et toujours actif ; l'interface peut redémarrer ou être fermée sans interrompre l'assistant ; un seul état d'interface partagé par les deux fenêtres |
| 19 | **Interface web** (HTML / CSS / TypeScript) dans la vue web du système, hébergée par un petit hôte C++ ; ni Electron, ni Qt | Écosystème le plus riche pour une interface moderne et très soignée (composants, animations Rive / Lottie) ; onglets web des modules naturels ; vue web système plutôt qu'Electron pour la légèreté. Contreparties acceptées : TypeScript en plus du C++, petits écarts de rendu WebView2 / WebKitGTK, mémoire supérieure à Qt Quick |
| 20 | **Onglets de modules déclaratifs par défaut**, web isolés sur permission `ui.web` (directive de sécurité n° 12) | Liberté pour les modules qui en ont besoin, sans exécuter de code de module dans l'interface par défaut ni lui donner accès au réseau ou aux autres modules |

---

## 8. Questions ouvertes

- Framework de l'interface web (Svelte, Solid, React…).
- Bibliothèque C++ de vue web (`saucer`, `webview`…) et intégration layer-shell sous Wayland.
- Magasin de modules : hébergement, signature et vérification des modules.
- Format exact des composants déclaratifs des onglets.
- Moteur de recherche web par défaut.
