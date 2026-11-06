cmake_minimum_required(VERSION 3.28)

project(avenoir
    VERSION 0.1.0
    DESCRIPTION "Assistant de système d'exploitation piloté par un LLM"
    LANGUAGES CXX)

# ─── Options ────────────────────────────────────────────────────────────────
option(AVENOIR_BUILD_TESTS "Compiler les tests (Catch2)" ON)
option(AVENOIR_WARNINGS_AS_ERRORS "Traiter les avertissements comme des erreurs" ON)
option(AVENOIR_SANITIZE "Activer AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
set(AVENOIR_VERSION_SUFFIX "" CACHE STRING
    "Suffixe de pré-version ajouté à PROJECT_VERSION (ex. « -beta.1 »), fourni par le workflow de release")

# Version complète affichée par l'application : 1.2.0, 1.2.0-beta.1…
set(AVENOIR_FULL_VERSION "${PROJECT_VERSION}${AVENOIR_VERSION_SUFFIX}")

# ─── Langage ────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Pas de modules C++20 : évite des options GCC inconnues de clang-tidy.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AvenoirOptions)

# ─── Dépendances (vcpkg.json) ───────────────────────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(replxx CONFIG REQUIRED)

# ─── Tests ──────────────────────────────────────────────────────────────────
if(AVENOIR_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

# ─── Bibliothèques et applications ──────────────────────────────────────────
add_subdirectory(libs/core)
add_subdirectory(libs/llm)
add_subdirectory(libs/tools)
add_subdirectory(apps/cli)
