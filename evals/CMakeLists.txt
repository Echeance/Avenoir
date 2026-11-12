# Banc d'essai avec de vrais modèles (`avenoir-evals`) : requêtes de référence
# (requetes.toml), doublures d'outils, rapport Markdown dans resultats/.
# Compilé avec le reste pour ne pas se dégrader, mais jamais lancé par ctest
# ni par la CI : il appelle un vrai LLM. Seule sa logique y est testée.

add_library(avenoir_evals_core STATIC
    src/cases.cpp
    src/report.cpp
    src/runner.cpp)
target_include_directories(avenoir_evals_core PUBLIC src)
target_link_libraries(avenoir_evals_core
    PUBLIC avenoir::core
    PRIVATE tomlplusplus::tomlplusplus avenoir::options)
target_compile_features(avenoir_evals_core PUBLIC cxx_std_23)

add_executable(avenoir_evals src/main.cpp)
set_target_properties(avenoir_evals PROPERTIES OUTPUT_NAME avenoir-evals)
avenoir_utf8_manifest(avenoir_evals)
target_link_libraries(avenoir_evals PRIVATE
    avenoir_evals_core
    avenoir::agents
    avenoir::llm
    avenoir::modules
    avenoir::tools
    avenoir::options
    spdlog::spdlog)
target_compile_definitions(avenoir_evals PRIVATE
    AVENOIR_EVALS_CASES="${CMAKE_CURRENT_SOURCE_DIR}/requetes.toml"
    AVENOIR_EVALS_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/resultats"
    AVENOIR_EVALS_MODULES="${AVENOIR_MODULES_OUTPUT_DIR}")

if(AVENOIR_BUILD_TESTS)
    add_executable(avenoir_evals_tests tests/evals_test.cpp)
    target_link_libraries(avenoir_evals_tests PRIVATE
        avenoir_evals_core avenoir::options Catch2::Catch2WithMain)
    target_compile_definitions(avenoir_evals_tests PRIVATE
        AVENOIR_EVALS_CASES="${CMAKE_CURRENT_SOURCE_DIR}/requetes.toml")
    avenoir_utf8_manifest(avenoir_evals_tests)
    catch_discover_tests(avenoir_evals_tests TEST_PREFIX "evals: ")
endif()
