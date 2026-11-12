# Démon résident : exécutable `avenoird` (décision 53). Placé à côté de
# `avenoir` (même dossier de sortie) : la CLI le lance, et les modules fournis
# sont cherchés à côté de l'exécutable.

if(WIN32)
    set(_avenoir_daemon_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_daemon_platform src/platform/linux.cpp)
else()
    set(_avenoir_daemon_platform src/platform/unsupported.cpp)
endif()

add_library(avenoir_daemon_lib STATIC
    ${_avenoir_daemon_platform}
    src/conversations.cpp
    src/modules.cpp
    src/runtime.cpp
    src/server.cpp
    src/settings_catalog.cpp
    src/setup.cpp
    src/setup_plan.cpp
    src/ui.cpp)
target_include_directories(avenoir_daemon_lib PUBLIC src)
target_link_libraries(avenoir_daemon_lib PUBLIC
    avenoir::core
    avenoir::ipc
    avenoir::llm
    avenoir::modules
    avenoir::storage
    avenoir::terminal
    avenoir::tools
    spdlog::spdlog
    PRIVATE avenoir::options reproc++)
if(WIN32)
    target_link_libraries(avenoir_daemon_lib PRIVATE advapi32)
endif()

add_executable(avenoird src/main.cpp)
target_link_libraries(avenoird PRIVATE avenoir_daemon_lib avenoir::options)
avenoir_utf8_manifest(avenoird)
set_target_properties(avenoird PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/cli")

if(AVENOIR_BUILD_TESTS)
    # Ce qui se décide sans terminal ni réseau : création guidée, réglages.
    add_executable(avenoir_daemon_tests
        tests/settings_catalog_test.cpp
        tests/setup_plan_test.cpp)
    target_link_libraries(avenoir_daemon_tests PRIVATE
        avenoir_daemon_lib avenoir::options Catch2::Catch2WithMain)
    avenoir_utf8_manifest(avenoir_daemon_tests)
    catch_discover_tests(avenoir_daemon_tests TEST_PREFIX "daemon: ")
endif()
