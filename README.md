# Avenoir

Assistant de système d'exploitation pour Windows et Linux, écrit en C++23, piloté par un LLM configurable et extensible par des plugins MCP.

Vision, architecture et directives : voir **[PROJECT.md](PROJECT.md)**.
Feuille de route : **[docs/ROADMAP.md](docs/ROADMAP.md)**.

> 🚧 Le projet en est à sa phase de conception : le code arrive avec le jalon 1 de la feuille de route.

## Prérequis (prévus)

- Compilateur C++23 : GCC ≥ 14, Clang ≥ 18 ou MSVC (Visual Studio 2022 17.10+)
- CMake ≥ 3.28 et Ninja
- [vcpkg](https://github.com/microsoft/vcpkg) (variable `VCPKG_ROOT` définie)

## Démarrage rapide (prévu)

```sh
cp config.example.toml config.toml   # puis choisir le fournisseur LLM
cmake --preset release
cmake --build --preset release
./build/release/apps/cli/avenoir     # ou : avenoir chemin/config.toml
```

Fournisseurs LLM :
- **Local** avec Ollama : `ollama pull qwen3:8b` (ou tout modèle gérant le *tool calling*).
- **API distante** : exporter la variable indiquée par `api_key_env`.
- **Abonnement Claude** (Pro / Max), sans clé d'API : installer Claude Code, s'y connecter (`claude` puis `/login`), et choisir `provider = "claude_code"` (option D de `config.example.toml`).
