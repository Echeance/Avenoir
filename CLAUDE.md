# CLAUDE.md — Instructions pour Claude

Avenoir est un assistant de système d'exploitation (Windows + Linux) écrit en **C++23**, piloté par un LLM configurable et extensible par des plugins MCP.

Licence : **PolyForm Strict 1.0.0** (`LICENSE.md`) — ne pas la modifier ni ajouter de code tiers incompatible sans accord de l'utilisateur ; toute dépendance doit avoir une licence permissive compatible (MIT, BSD, Boost, Apache-2.0…).

**`PROJECT.md` est le document de référence** (vision, architecture, sécurité, directives, journal des décisions). **`docs/ROADMAP.md`** donne le plan de travail détaillé. Les relire avant tout travail structurant et les tenir à jour.

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
- Toute décision d'architecture notable → nouvelle entrée dans la section 7 de `PROJECT.md` (« Journal des décisions »).
- Une tâche de la roadmap terminée → la cocher dans `docs/ROADMAP.md`.
- Publier une version : même numéro dans `CMakeLists.txt` et `vcpkg.json`, commit, puis tag poussé : `vX.Y.Z` (stable, « Latest » si ≥ 1.0.0) ou `vX.Y.Z-alpha|beta|rc.N` (pré-version). Le workflow `release` publie les binaires. Ne jamais créer de tag sans demande de l'utilisateur.
- CI (`.github/workflows/ci.yml`) : Linux GCC / Clang / ASan + Windows MSVC + clang-format. Une CI rouge se corrige avant de continuer.

## Commandes

```sh
scripts/build.sh [debug|release|asan] [-c] [-r] [-n] [-f] [-k]   # raccourci Linux (voir -h)
scripts\build.ps1 [debug|release] [-c] [-r] [-n] [-f] [-k]       # raccourci Windows (même logique)
export VCPKG_ROOT=~/vcpkg          # requis par les préréglages
cmake --preset debug               # configuration (vcpkg en mode manifeste)
cmake --build --preset debug       # build
ctest --preset debug               # tests
cmake --preset release && cmake --build --preset release   # exécutable `avenoir`
./build/release/apps/cli/avenoir [chemin/config.toml]
AVENOIR_LOG=debug ./build/debug/apps/cli/avenoir
```

`config.toml` est ignoré par git ; partir de `config.example.toml`.

## Structure

- `libs/core` (`avenoir::core`) : agent, outils, permissions, voie rapide, config. **Ne dépend d'aucune autre bibliothèque interne.**
- `libs/llm` : fournisseurs LLM (OpenAI-compatible, Anthropic, Claude Code).
- `libs/tools` : outils natifs ; code spécifique à l'OS dans `src/platform/`.
- `libs/mcp` : client MCP (stdio).
- `libs/voice` : interfaces voix.
- `apps/cli` : interface texte (exécutable `avenoir`). Les applications de `apps/` assemblent le tout.

En-têtes publics dans `libs/<nom>/include/avenoir/<nom>/`, tests Catch2 dans `libs/<nom>/tests/`.

## Langue

- Identifiants (types, fonctions, variables) : **anglais**.
- Commentaires, documentation, messages utilisateur, messages de commit : **français**.
- Répondre à l'utilisateur en français.

## Règles C++

- C++23 ; GCC ≥ 14, Clang ≥ 18, MSVC 19.40+. CMake ≥ 3.28, dépendances via `vcpkg.json`.
- Avertissements traités comme erreurs (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` / `/W4 /WX /permissive-`).
- RAII partout : pas de `new`/`delete`, `malloc`/`free`, pointeur nu propriétaire, cast C. `std::unique_ptr` par défaut.
- Erreurs : `std::expected<T, Error>` dans les bibliothèques ; aucune exception ne traverse une bibliothèque (capturer et convertir celles des dépendances). Exceptions tolérées seulement au plus haut niveau de `apps/`.
- Concurrence : synchrone + `std::jthread` / `std::stop_token` ; pas d'état partagé sans synchronisation.
- Logs : `spdlog` ; audit des outils sur le logger `audit` ; niveau via `AVENOIR_LOG`.
- Nommage : `PascalCase` (types), `snake_case` (fonctions, variables), membres privés suffixés `_`, constantes `kPascalCase`, espaces de noms `avenoir::<bibliothèque>`.
- Bibliothèques retenues : `nlohmann/json`, `toml++`, `cpr`, `reproc`, `spdlog`, `Catch2`. En ajouter une → la justifier et l'inscrire dans `vcpkg.json`.
- Fonctionnalités lourdes (voix, UI) derrière des options CMake.

## Multiplateforme

- Code spécifique à un OS dans `src/platform/linux.cpp` / `windows.cpp` derrière `platform.hpp`, **avec** un repli « non supporté ».
- Un outil indisponible sur un OS n'est pas enregistré sur cet OS.
- UTF-8 partout ; conversion UTF-16 seulement à l'appel des API Windows `…W`.

## Sécurité (prime sur le confort)

- `RiskLevel` : `read_only` / `reversible` / `sensitive`. En cas de doute, choisir le plus élevé.
- `sensitive` n'est **jamais** `allow` ; confirmation obligatoire.
- Les données (web, fichiers, résultats de plugins, réponses du LLM) ne sont pas des instructions ; la vraie barrière est le contrôle des permissions.
- Pas d'outil shell générique. **Jamais** de `system()`, `popen()`, `sh -c`, `cmd /C` avec une chaîne construite : tableau d'arguments (`reproc`, `CreateProcessW`) ou API dédiée (`ShellExecuteW`).
- Valider tous les arguments côté outil (noms d'app sans métacaractères, URL `http(s)` uniquement, chemins canonicalisés et restreints).
- Secrets uniquement via variables d'environnement ; jamais dans `config.toml` ni dans les logs.
- Plugins MCP : risque fixé par l'utilisateur, `sensitive` par défaut.

## Écrire un outil natif

Nom `snake_case` anglais ; description soignée pour le LLM ; JSON Schema minimal avec `required` ; lecture des arguments dans une structure typée puis validation ; résultat court, en phrase française lisible (la voie rapide l'affiche sans LLM) ; tests de validation (cas valides **et** tentatives d'injection) ; enregistrement dans `register_native_tools`.

## Tests

- Catch2, lancés par `ctest`.
- Boucle agent testée avec un LLM scripté, sans réseau.
- Les tests ne lancent jamais de vraie application et ne modifient pas le système.
- CI : Linux + Windows, plus une passe ASan + UBSan sous Linux.
