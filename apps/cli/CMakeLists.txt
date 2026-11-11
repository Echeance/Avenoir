# Interface texte : exécutable `avenoir`.

add_executable(avenoir_cli
    src/console.cpp
    src/conversations.cpp
    src/display.cpp
    src/main.cpp
    src/modules.cpp
    src/prompt.cpp
    src/setup.cpp
    src/setup_plan.cpp)
set_target_properties(avenoir_cli PROPERTIES OUTPUT_NAME avenoir)
avenoir_utf8_manifest(avenoir_cli)
target_link_libraries(avenoir_cli PRIVATE
    avenoir::core
    avenoir::llm
    avenoir::modules
    avenoir::storage
    avenoir::terminal
    avenoir::tools
    avenoir::options
    replxx::replxx
    spdlog::spdlog)

# Test de bout en bout (sans LLM ni réseau), lancé par ctest sur Linux et Windows.
if(AVENOIR_BUILD_TESTS)
    # Création guidée : la partie sans terminal ni réseau.
    add_executable(avenoir_cli_tests tests/setup_plan_test.cpp src/setup_plan.cpp)
    target_include_directories(avenoir_cli_tests PRIVATE src)
    target_link_libraries(avenoir_cli_tests PRIVATE
        avenoir::core avenoir::llm avenoir::options Catch2::Catch2WithMain)
    avenoir_utf8_manifest(avenoir_cli_tests)
    catch_discover_tests(avenoir_cli_tests TEST_PREFIX "cli: ")

    add_test(NAME "cli: dialogue par la voie rapide"
             COMMAND "${CMAKE_COMMAND}"
                     "-DAVENOIR=$<TARGET_FILE:avenoir_cli>"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cmake")
endif()
