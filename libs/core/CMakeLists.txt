# avenoir::core — cœur de l'assistant, sans dépendance à l'OS, au LLM ou à la
# voix. Ne dépend d'aucune autre bibliothèque interne.

avenoir_add_library(core
    SOURCES
        src/agent.cpp
        src/config.cpp
        src/env.cpp
        src/error.cpp
        src/permission.cpp
        src/router.cpp
        src/tool.cpp
        src/version.cpp
    DEPS
        nlohmann_json::nlohmann_json)

# Utilisées seulement dans les .cpp : non exposées aux bibliothèques clientes.
target_link_libraries(avenoir_core PRIVATE
    tomlplusplus::tomlplusplus
    spdlog::spdlog)

target_compile_definitions(avenoir_core PRIVATE
    AVENOIR_VERSION="${AVENOIR_FULL_VERSION}")

avenoir_add_tests(core
    SOURCES
        tests/agent_test.cpp
        tests/config_test.cpp
        tests/permission_test.cpp
        tests/router_test.cpp
        tests/tool_test.cpp
        tests/version_test.cpp)

if(TARGET avenoir_core_tests)
    # Pour charger config.example.toml depuis les tests.
    target_compile_definitions(avenoir_core_tests PRIVATE
        AVENOIR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
