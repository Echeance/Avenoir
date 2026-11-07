# Interface texte : exécutable `avenoir`.

add_executable(avenoir_cli
    src/console.cpp
    src/conversations.cpp
    src/display.cpp
    src/main.cpp
    src/modules.cpp
    src/prompt.cpp)
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
    add_test(NAME "cli: dialogue par la voie rapide"
             COMMAND "${CMAKE_COMMAND}"
                     "-DAVENOIR=$<TARGET_FILE:avenoir_cli>"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cmake")
endif()
