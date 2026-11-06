# avenoir::core — cœur de l'assistant, sans dépendance à l'OS, au LLM ou à la
# voix. Ne dépend d'aucune autre bibliothèque interne.

avenoir_add_library(core
    SOURCES
        src/version.cpp)

target_compile_definitions(avenoir_core PRIVATE
    AVENOIR_VERSION="${AVENOIR_FULL_VERSION}")

avenoir_add_tests(core
    SOURCES
        tests/version_test.cpp)
