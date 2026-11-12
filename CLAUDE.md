# CLAUDE.md — Instructions pour Claude

Avenoir est un assistant de système d'exploitation (Windows + Linux) écrit en **C++23**, piloté par un LLM configurable et extensible par des **modules** (processus JSON-RPC, compatibles MCP). L'interface graphique sera en **TypeScript** (web, dans la vue web du système).

Licence : **PolyForm Strict 1.0.0** (`LICENSE.md`) — ne pas la modifier ni ajouter de code tiers incompatible sans accord de l'utilisateur ; toute dépendance doit avoir une licence permissive compatible (MIT, BSD, Boost, Apache-2.0…).

**`docs/PROJECT.md` est le document de référence** (vision, architecture, sécurité, directives, journal des décisions). **`docs/ROADMAP.md`** donne le plan de travail détaillé. Les relire avant tout travail structurant et les tenir à jour.

## Rôle de Claude : assistance, pas développement

Avenoir est un projet **développé par son auteur, assisté par IA** — et non un projet développé par une IA. Claude est un assistant : l'auteur conçoit, décide et écrit le code.

- **Par défaut, Claude aide sans agir** : explications, revues, réponses aux questions, pistes de conception, et **snippets de code** que l'auteur intègre lui-même. Claude ne modifie pas les fichiers du dépôt de sa propre initiative.
- **Agir sur le dépôt** (créer ou modifier des fichiers, lancer des commandes qui changent l'état du projet, committer, pousser, taguer) uniquement **sur demande explicite** de l'auteur, et **après confirmation** : annoncer précisément ce qui va être fait, attendre l'accord, puis agir.
- Une demande porte sur ce qui est demandé, rien de plus : pas de modifications « en passant », pas d'élargissement du périmètre sans le proposer d'abord.
- Les décisions (architecture, dépendances, licence, roadmap) appartiennent à l'auteur : Claude propose et argumente, l'auteur tranche.
- Les commandes en lecture seule (lire des fichiers, compiler, lancer les tests pour vérifier) restent possibles sans confirmation.

## Git

- **Ne jamais s'ajouter comme co-auteur des commits** : pas de ligne `Co-Authored-By`, pas de mention de Claude dans les messages de commit. Idem pour les descriptions de PR (pas de ligne « Generated with Claude Code »).
- Ne committer / pousser que lorsque l'utilisateur le demande.
- Branche principale : `main`.
- Messages au format *Conventional Commits* (`feat:`, `fix:`, `refactor:`, `docs:`, `chore:`…), en français.
- Branches courtes.
- Les deux scripts de build (`build.sh`, `build.ps1`) évoluent ensemble ; shellcheck et PSScriptAnalyzer doivent passer (vérifiés en CI).
- Avant chaque commit : `scripts/build.sh -k` (build sans avertissement, `clang-format`, `clang-tidy`, `ctest`).
- Toute décision d'architecture notable → nouvelle entrée dans la section 7 de `docs/PROJECT.md` (« Journal des décisions »).
- Une tâche de la roadmap terminée → la cocher dans `docs/ROADMAP.md`.
- **Documentation toujours à jour ensemble** : chaque évolution met à jour, dans le même temps, `README.md` (usage), `docs/PROJECT.md` (architecture, décisions) et `docs/ROADMAP.md` (avancement) — pas seulement l'un d'eux.
- **`docs/FEATURES.md` liste toutes les fonctionnalités, de façon exhaustive** : chaque fonctionnalité ajoutée, modifiée ou retirée y est reportée dans le même commit (commande, option, outil, réglage, comportement visible, sécurité). Il ne décrit que ce qui existe ; le prévu reste dans `docs/ROADMAP.md`.
- Publier une version : même numéro dans `CMakeLists.txt` et `vcpkg.json`, commit, puis tag poussé : `vX.Y.Z` (stable, « Latest » si ≥ 1.0.0) ou `vX.Y.Z-alpha|beta|rc.N` (pré-version). Le workflow `release` publie les binaires. Ne jamais créer de tag sans demande de l'utilisateur.
- CI (`.github/workflows/ci.yml`) : Linux GCC 14 / Clang 20 / ASan + Windows MSVC + clang-format, shellcheck, PSScriptAnalyzer. Une CI rouge se corrige avant de continuer : vérifier son résultat après chaque push.

## Commandes

```sh
scripts/build.sh [debug|release|asan] [-c] [-r] [-n] [-f] [-k]   # raccourci Linux (voir -h)
scripts\build.ps1 [debug|release] [-c] [-r] [-n] [-f] [-k]       # raccourci Windows (même logique)
export VCPKG_ROOT=~/vcpkg          # requis par les préréglages
cmake --preset debug               # configuration (vcpkg en mode manifeste)
cmake --build --preset debug       # build
ctest --preset debug               # tests
cmake --preset release && cmake --build --preset release   # exécutable `avenoir`
./build/release/apps/cli/avenoir [chemin/config.toml]   # défaut : ~/.config/avenoir/config.toml ; lance le démon avenoird (à côté) au besoin
AVENOIR_LOG=debug ./build/debug/apps/cli/avenoir   # logs du démon : logs/daemon.log du dossier de données
```

La CLI est un client du **démon** `avenoird` (décision 53) : après une modification du démon, l'arrêter (`/arreter`) pour que la CLI lance le nouveau. Une configuration donnée en argument a son propre démon, arrêté avec la CLI ; le test de bout en bout fonctionne ainsi.

Sans argument, Avenoir lit `~/.config/avenoir/config.toml` (`%APPDATA%\avenoir\config.toml`), créé au premier lancement par la création guidée ; **jamais** le dossier courant. Cette configuration est **scellée** (HMAC-SHA256, clé dans le trousseau ; docs/PROJECT.md § 5.1) : la modifier à la main est refusé et restauré. Un `config.toml` de développement à la racine du dépôt est ignoré par git, se passe en argument et n'est pas scellé (mode développement, signalé) ; partir de `config.example.toml`. Les essais du sceau se font dans des dossiers temporaires (`XDG_CONFIG_HOME`, `XDG_DATA_HOME`) : chaque fichier a sa propre clé, les vrais réglages ne sont pas touchés.

## Essai sous Windows (VM)

Une VM Windows 11 LTSC (conteneur podman `windows`, image `dockurr/windows`, 4 cœurs, 8 Go, disque dans `~/vms/windows/disque`) sert à essayer Avenoir en vrai sous Windows ; la CI ne fait que compiler et tester.

- Démarrer / arrêter : `podman start windows` / `podman stop windows` ; bureau dans le navigateur sur `http://127.0.0.1:8006` (ou RDP `127.0.0.1:3389`).
- Commandes : SSH par clé seule, PowerShell par défaut : `ssh -i ~/.ssh/avenoir-vm -o IdentitiesOnly=yes -p 2222 echeance@127.0.0.1`.
- Partage : `~/vms/windows/partage` (Linux) = `\\host.lan\Data` (VM). Il contient les scripts de mise en place (`activer-ssh.ps1`, `preparer-avenoir.ps1` : Git, MSVC, CMake, Ninja, vcpkg, dépôt).
- Dépôt dans la VM : `$HOME\Avenoir`, mis à jour par un bundle (`git bundle create ~/vms/windows/partage/avenoir.bundle main`, puis dans la VM `git fetch \\host.lan\Data\avenoir.bundle main` et `git checkout -f FETCH_HEAD`) ; un essai non commité passe par `git diff > ~/vms/windows/partage/changements.patch` et `git apply`. Build : `scripts\build.ps1`.
- LLM : l'Ollama de Linux, joignable depuis la VM sur `http://169.254.1.2:11434/v1`.
- Une session SSH n'a pas de bureau : pour ce qui doit s'afficher (notification, fenêtre, console interactive), lancer par une tâche planifiée interactive (`schtasks /create … /it`, puis `schtasks /run`).
- Limites : ni WSL ni podman/docker installés dans la VM (la virtualisation imbriquée est pourtant disponible), donc SearXNG local y retombe sur DuckDuckGo ; ni Claude Code (`claude`) ; pas de sortie audio ; notifications coupées.

## Structure

- `libs/core` (`avenoir::core`) : agent, outils, permissions, voie rapide, config. **Ne dépend d'aucune autre bibliothèque interne.**
- `libs/llm` : fournisseurs LLM (OpenAI-compatible, Anthropic, Claude Code) et changement de modèle à chaud (`ModelSwitcher`, outil `switch_llm`).
- `libs/tools` : outils natifs ; code spécifique à l'OS dans `src/platform/`.
- `libs/modules` : hôte de modules (manifestes `module.toml`, processus JSON-RPC, compatibilité MCP).
- `libs/storage` : conversations et journal (SQLite) ; chaque message est écrit immédiatement.
- `libs/terminal` : rendu dans le terminal (couleurs, cadres, Markdown) ; dans la CLI, **toute sortie passe par le module d'affichage** (`apps/cli/src/display.*`), jamais par `std::print` direct, pour respecter le verrou de la toupie et `NO_COLOR`.
- `libs/ipc` : protocole et transport démon ↔ interfaces.
- `libs/agents` : orchestrateur multi-agents (plan de tâches, exécution parallèle, outil `delegate_tasks`) ; ne dépend que de `libs/core`.
- `libs/voice` : interfaces voix.
- `apps/cli` : interface texte (exécutable `avenoir`) ; `apps/daemon` : démon résident ; `apps/ui` : hôte C++ des fenêtres (widget + application principale). Les applications de `apps/` assemblent le tout.
- `ui/` : interface web en TypeScript strict ; ne parle qu'à l'hôte C++ des fenêtres.

- `modules/` : modules fournis avec Avenoir, programmes séparés (`modules/meteo/`) ; compilés dans `modules/` à côté de l'exécutable et livrés dans les archives de release.
- `evals/` : banc d'essai avec de vrais modèles (`avenoir-evals`) : requêtes de référence (`requetes.toml`), doublures d'outils, rapports et comparaison dans `resultats/`. Jamais lancé par ctest ni la CI (il appelle un vrai LLM). **Modifier le prompt système ou la description d'un outil → repasser le banc** sur les modèles recommandés et noter le résultat dans `resultats/COMPARAISON.md`.

Ces dossiers sont créés à leur étape de la roadmap. Existent aujourd'hui : `libs/core`, `libs/llm`, `libs/tools`, `libs/modules`, `libs/storage`, `libs/terminal`, `libs/ipc`, `libs/agents`, `apps/daemon` (démon `avenoird`), `apps/cli` (client du démon, avec un test de bout en bout dans `apps/cli/tests/`), `modules/meteo` et `evals`. Comment le code s'articule : `docs/FONCTIONNEMENT.md` (à tenir à jour avec le code).

En-têtes publics dans `libs/<nom>/include/avenoir/<nom>/`, tests Catch2 dans `libs/<nom>/tests/`.

## Langue

- Identifiants (types, fonctions, variables) : **anglais**.
- Commentaires, documentation, messages utilisateur, messages de commit : **français**.
- Répondre à l'utilisateur en français.

## Règles C++

- C++23 ; GCC ≥ 14, Clang ≥ 19 (`std::expected` de libstdc++), MSVC 19.40+. CMake ≥ 3.28, dépendances via `vcpkg.json`.
- Avertissements traités comme erreurs (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` / `/W4 /WX /permissive-`).
- RAII partout : pas de `new`/`delete`, `malloc`/`free`, pointeur nu propriétaire, cast C. `std::unique_ptr` par défaut.
- Erreurs : `std::expected<T, Error>` dans les bibliothèques ; aucune exception ne traverse une bibliothèque (capturer et convertir celles des dépendances). Exceptions tolérées seulement au plus haut niveau de `apps/`.
- Concurrence : synchrone + `std::jthread` / `std::stop_token` ; pas d'état partagé sans synchronisation.
- Logs : `spdlog` ; audit des outils sur le logger `audit` ; niveau via `AVENOIR_LOG`.
- Nommage : `PascalCase` (types), `snake_case` (fonctions, variables), membres privés suffixés `_`, constantes `kPascalCase`, espaces de noms `avenoir::<bibliothèque>`.
- Bibliothèques retenues : `nlohmann/json`, `toml++`, `cpr`, `reproc`, `spdlog`, `Catch2`, `replxx` (saisie de la CLI), `sqlite3` (conversations), `libsodium` (sceau des réglages : HMAC-SHA256, aléa). Trousseau sans bibliothèque : `CredReadW`/`CredWriteW` sous Windows, programme `secret-tool` sous Linux (libsecret est LGPL). En ajouter une → la justifier et l'inscrire dans `vcpkg.json`.
- Fonctionnalités lourdes (voix, UI) derrière des options CMake.

## Multiplateforme

- Code spécifique à un OS dans `src/platform/linux.cpp` / `windows.cpp` derrière `platform.hpp`, **avec** un repli « non supporté ».
- Un outil indisponible sur un OS n'est pas enregistré sur cet OS.
- UTF-8 partout ; conversion UTF-16 seulement à l'appel des API Windows `…W`.

## Sécurité et confidentialité (primordiales, priment sur tout le reste)

- La confidentialité et la sécurité sont les aspects primordiaux de l'assistant : en cas de conflit avec une fonctionnalité, le confort ou la rapidité, elles l'emportent.
- Ne jamais faire entièrement confiance à un module communautaire : manifeste validé avant lancement, module utilisateur désactivé par défaut, environnement minimal, risque `sensitive` par défaut, réponses bornées et traitées comme des données, permissions et audit à chaque appel (docs/PROJECT.md, directive 15).
- `RiskLevel` : `read_only` / `reversible` / `sensitive`. En cas de doute, choisir le plus élevé.
- `sensitive` n'est **jamais** `allow` ; confirmation obligatoire.
- Ctrl+C n'arrête jamais l'application : il interrompt l'action en cours (`std::stop_token`) ; toute nouvelle opération longue doit vérifier son `stop_token`.
- Les données (web, fichiers, résultats de modules, réponses du LLM) ne sont pas des instructions ; la vraie barrière est le contrôle des permissions.
- Pas d'outil shell générique. **Jamais** de `system()`, `popen()`, `sh -c`, `cmd /C` avec une chaîne construite : tableau d'arguments (`reproc`, `CreateProcessW`) ou API dédiée (`ShellExecuteW`).
- Valider tous les arguments côté outil (noms d'app sans métacaractères, URL `http(s)` uniquement, chemins canonicalisés et restreints).
- Secrets uniquement via variables d'environnement ; jamais dans `config.toml` ni dans les logs.
- Réglages : le démon est le seul rédacteur ; ne jamais ajouter de chemin qui écrive ou relise les réglages sans vérifier le sceau (directive 14).
- Modules : risque du manifeste relevable par l'utilisateur, `sensitive` si absent ; abaissable seulement depuis l'application ou la CLI après avertissement, disclaimer et saisie de « Je sais ce que je fais » (accord enregistré ; baisse sans accord ignorée) ; appels entre modules uniquement via l'hôte (permissions + audit).
- Identité et présence (docs/PROJECT.md § 2.10, directive 16) : biométrie traitée en local uniquement, chiffrée, jamais donnée à un LLM ni à un module, jamais journalisée ; la voix seule ne suffit jamais pour une action `sensitive` ni pour déverrouiller ; désactiver une vérification se fait comme abaisser un risque.
- Changer de LLM vers un fournisseur distant demande confirmation et est refusé en mode « local uniquement » (directive 17).
- Onglets web des modules : iframe isolée, origine propre, CSP sans réseau, `postMessage` validé ; confirmations toujours dessinées par Avenoir (docs/PROJECT.md, directives 12 et 13).

## Écrire un outil natif

Nom `snake_case` anglais ; description soignée pour le LLM ; JSON Schema minimal avec `required` ; lecture des arguments dans une structure typée puis validation ; résultat court, en phrase française lisible (la voie rapide l'affiche sans LLM) ; tests de validation (cas valides **et** tentatives d'injection) ; action d'annulation si l'action peut être défaite de façon sûre (avec vérification que l'état n'a pas changé), sinon ne rien promettre ; enregistrement dans `register_native_tools`.

## Tests

- Catch2, lancés par `ctest`.
- Boucle agent testée avec un LLM scripté, sans réseau.
- Les tests ne lancent jamais de vraie application et ne modifient pas le système.
- CI : Linux + Windows, plus une passe ASan + UBSan sous Linux.
