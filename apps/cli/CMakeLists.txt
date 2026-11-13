# Interface texte : exécutable `avenoir`.

if(WIN32)
    set(_avenoir_cli_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_cli_platform src/platform/linux.cpp)
else()
    set(_avenoir_cli_platform src/platform/unsupported.cpp)
endif()

add_library(avenoir_cli_lib STATIC
    src/console.cpp
    src/display.cpp
    src/packages.cpp
    src/prompt.cpp
    src/settings_menu.cpp
    src/wolf.cpp
    ${_avenoir_cli_platform})
target_include_directories(avenoir_cli_lib PUBLIC src)
target_link_libraries(avenoir_cli_lib PUBLIC
    avenoir::core
    avenoir::ipc
    avenoir::storage
    avenoir::terminal
    replxx::replxx
    spdlog::spdlog
    PRIVATE avenoir::options)

add_executable(avenoir_cli src/main.cpp)
set_target_properties(avenoir_cli PROPERTIES OUTPUT_NAME avenoir)
avenoir_utf8_manifest(avenoir_cli)
target_link_libraries(avenoir_cli PRIVATE avenoir_cli_lib avenoir::options)
# La CLI lance le démon, placé à côté d'elle.
add_dependencies(avenoir_cli avenoird)

# Test de bout en bout (sans LLM ni réseau), lancé par ctest sur Linux et Windows.
if(AVENOIR_BUILD_TESTS)
    # Ce qui se décide sans terminal : menu des réglages, loup.
    add_executable(avenoir_cli_tests tests/settings_menu_test.cpp tests/packages_test.cpp)
    target_link_libraries(avenoir_cli_tests PRIVATE
        avenoir_cli_lib avenoir::options Catch2::Catch2WithMain)
    avenoir_utf8_manifest(avenoir_cli_tests)
    catch_discover_tests(avenoir_cli_tests TEST_PREFIX "cli: ")

    add_test(NAME "cli: dialogue par la voie rapide"
             COMMAND "${CMAKE_COMMAND}"
                     "-DAVENOIR=$<TARGET_FILE:avenoir_cli>"
                     "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cmake")
endif()
