# Avenoir

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
