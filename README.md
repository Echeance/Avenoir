# Avenoir

[![CI](https://github.com/ympax14/Avenoir/actions/workflows/ci.yml/badge.svg)](https://github.com/ympax14/Avenoir/actions/workflows/ci.yml)

Assistant de système d'exploitation pour Windows et Linux, écrit en C++23, piloté par un LLM configurable et extensible par des modules (compatibles MCP).

Vision, architecture et directives : voir **[PROJECT.md](PROJECT.md)**.
Feuille de route : **[docs/ROADMAP.md](docs/ROADMAP.md)**.

> 🚧 Jalon 1 en cours : le projet compile (exécutable `avenoir` qui affiche sa version) ; la boucle de conversation arrive ensuite.

## Prérequis

- Compilateur C++23 : GCC ≥ 14, Clang ≥ 18 ou MSVC (Visual Studio 2022 17.10+)
- CMake ≥ 3.28 et Ninja
- [vcpkg](https://github.com/microsoft/vcpkg), avec la variable `VCPKG_ROOT` qui pointe dessus :

```sh
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics      # Windows : bootstrap-vcpkg.bat
export VCPKG_ROOT=~/vcpkg                        # fish : set -Ux VCPKG_ROOT ~/vcpkg
```

Les dépendances (`vcpkg.json`) sont téléchargées et compilées automatiquement à la première configuration.

## Compiler et tester

Le plus rapide : le script `scripts/build.sh` (Linux) enchaîne configuration, compilation et tests.

```sh
scripts/build.sh                  # debug + tests
scripts/build.sh release -r       # release, puis lance avenoir
scripts/build.sh -r -- cc.toml    # debug + tests, puis `avenoir cc.toml`
scripts/build.sh asan -c          # build propre avec ASan + UBSan
scripts/build.sh -f               # reformate le code avant de compiler
scripts/build.sh -k               # avant de committer : formatage, clang-tidy, tests
scripts/build.sh -h               # toutes les options
```

`VCPKG_ROOT` vaut `~/vcpkg` par défaut si la variable n'est pas définie.

Sous Windows, `scripts\build.ps1` fait la même chose (PowerShell 5.1 ou 7) et charge tout seul l'environnement Visual Studio (MSVC, Ninja) :

```powershell
scripts\build.ps1                       # debug + tests
scripts\build.ps1 release -r            # release, puis lance avenoir
scripts\build.ps1 -r -RunArgs cc.toml   # debug + tests, puis `avenoir cc.toml`
scripts\build.ps1 -k                    # avant de committer
Get-Help scripts\build.ps1 -Detailed    # toutes les options
```

Si PowerShell refuse d'exécuter le script : `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

Avec CMake directement :

```sh
cmake --preset debug             # configuration (+ dépendances vcpkg)
cmake --build --preset debug     # compilation
ctest --preset debug             # tests

cmake --preset release && cmake --build --preset release
./build/release/apps/cli/avenoir
```

Préréglage `asan` (Linux) : compilation avec AddressSanitizer + UndefinedBehaviorSanitizer.

Avant de committer : `scripts/build.sh -k` (vérifie le formatage, lance clang-tidy sur tous les `.cpp` et les tests).

## Configuration

En développement, copier `config.example.toml` en `config.toml` et l'éditer. À partir du jalon 3, les réglages ne se modifieront **que depuis Avenoir** : le fichier sera scellé, et toute modification faite en dehors sera refusée et annulée (voir [PROJECT.md § 5.1](PROJECT.md)).

## Fournisseurs LLM (à venir)

- **Local** avec Ollama : `ollama pull qwen3:8b` (ou tout modèle gérant le *tool calling*).
- **API distante** : exporter la variable indiquée par `api_key_env`.
- **Abonnement Claude** (Pro / Max), sans clé d'API : installer Claude Code, s'y connecter (`claude` puis `/login`), et choisir `provider = "claude_code"` (option D de `config.example.toml`).

## Publier une version

1. Mettre à jour la version dans `CMakeLists.txt` (`project(... VERSION x.y.z)`) et `vcpkg.json`.
2. Committer et pousser `main`, puis pousser un tag :

| Type | Tag | Sur GitHub |
|---|---|---|
| Stable | `v1.2.0` | **Latest** |
| Pré-version | `v1.2.0-alpha.1`, `v1.2.0-beta.1`, `v1.2.0-rc.1` | Pre-release |

```sh
git tag v1.2.0-beta.1 && git push origin v1.2.0-beta.1
```

Le workflow `release` vérifie que le numéro du tag correspond à la version du projet, compile et teste sous Linux et Windows, puis publie la release avec `avenoir-linux-x64.tar.gz` et `avenoir-windows-x64.zip`. Les versions `0.x` sont toujours publiées en pré-version. Le binaire affiche la version complète (`avenoir 1.2.0-beta.1`).

## Licence

Avenoir est publié sous la **[PolyForm Strict License 1.0.0](LICENSE.md)** : le code est consultable et utilisable à des fins **personnelles et non commerciales**, mais toute **modification, œuvre dérivée ou redistribution** (y compris d'un fork) est interdite sans l'accord écrit de l'auteur.

Pour toute autre utilisation (modification, fork, contribution, usage commercial), ouvrez une *issue* ou contactez [@ympax14](https://github.com/ympax14) pour obtenir une autorisation.
