# Interface texte : exécutable `avenoir`.

if(WIN32)
    set(_avenoir_cli_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_cli_platform src/platform/linux.cpp)
else()
    set(_avenoir_cli_platform src/platform/unsupported.cpp)
endif()

add_executable(avenoir_cli
    src/console.cpp
    src/display.cpp
    src/main.cpp
    src/prompt.cpp
    ${_avenoir_cli_platform})
set_target_properties(avenoir_cli PROPERTIES OUTPUT_NAME avenoir)
avenoir_utf8_manifest(avenoir_cli)
target_link_libraries(avenoir_cli PRIVATE
    avenoir::core
    avenoir::ipc
    avenoir::storage
    avenoir::terminal
    avenoir::options
    replxx::replxx
    spdlog::spdlog)
# La CLI lance le démon, placé à côté d'elle.
add_dependencies(avenoir_cli avenoird)

# Test de bout en bout (sans LLM ni réseau), lancé par ctest sur Linux et Windows.
if(AVENOIR_BUILD_TESTS)
    add_test(NAME "cli: dialogue par la voie rapide"
             COMMAND "${CMAKE_COMMAND}"
                     "-DAVENOIR=$<TARGET_FILE:avenoir_cli>"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cmake")
endif()
