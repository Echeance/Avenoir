# avenoir::modules — hôte de modules : manifestes, processus JSON-RPC sur
# stdio, compatibilité MCP (PROJECT.md § 2.6).

if(WIN32)
    set(_avenoir_modules_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_modules_platform src/platform/linux.cpp)
else()
    set(_avenoir_modules_platform src/platform/unsupported.cpp)
endif()

avenoir_add_library(modules
    SOURCES
        src/discovery.cpp
        src/host.cpp
        src/manifest.cpp
        src/module.cpp
        src/rpc_client.cpp
        ${_avenoir_modules_platform}
    DEPS
        avenoir::core)

target_include_directories(avenoir_modules PRIVATE src)
target_link_libraries(avenoir_modules PRIVATE
    reproc++
    tomlplusplus::tomlplusplus
    spdlog::spdlog)

avenoir_add_tests(modules
    SOURCES
        tests/discovery_test.cpp
        tests/host_test.cpp
        tests/manifest_test.cpp)

if(TARGET avenoir_modules_tests)
    # Module de test lancé par les tests de l'hôte (aucun réseau).
    add_executable(avenoir_fixture_module tests/fixture_module.cpp)
    target_link_libraries(avenoir_fixture_module PRIVATE nlohmann_json::nlohmann_json
                                                         avenoir::options)
    avenoir_utf8_manifest(avenoir_fixture_module)
    add_dependencies(avenoir_modules_tests avenoir_fixture_module)
    target_compile_definitions(avenoir_modules_tests PRIVATE
        AVENOIR_FIXTURE_MODULE="$<TARGET_FILE:avenoir_fixture_module>")
endif()
