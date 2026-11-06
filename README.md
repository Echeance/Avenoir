# Avenoir

[![CI](https://github.com/ympax14/Avenoir/actions/workflows/ci.yml/badge.svg)](https://github.com/ympax14/Avenoir/actions/workflows/ci.yml)

Assistant de système d'exploitation pour Windows et Linux, écrit en C++23, piloté par un LLM configurable et extensible par des plugins MCP.

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

```sh
cmake --preset debug             # configuration (+ dépendances vcpkg)
cmake --build --preset debug     # compilation
ctest --preset debug             # tests

cmake --preset release && cmake --build --preset release
./build/release/apps/cli/avenoir
```

Préréglage `asan` (Linux) : compilation avec AddressSanitizer + UndefinedBehaviorSanitizer.

Avant de committer :

```sh
git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror
clang-tidy -p build/debug <fichiers .cpp modifiés>
```

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
