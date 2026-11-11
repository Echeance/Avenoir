# avenoir::storage — conversations persistantes (SQLite), secrets (trousseau
# du système) et réglages scellés. Le code propre à chaque OS est isolé dans
# src/platform/ et choisi ici.

if(WIN32)
    set(_avenoir_storage_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_storage_platform src/platform/linux.cpp)
else()
    set(_avenoir_storage_platform src/platform/unsupported.cpp)
endif()

avenoir_add_library(storage
    SOURCES
        src/message_json.cpp
        src/paths.cpp
        src/secrets.cpp
        src/settings.cpp
        src/store.cpp
        src/transcript.cpp
        ${_avenoir_storage_platform}
    DEPS
        avenoir::core)

target_link_libraries(avenoir_storage PRIVATE
    unofficial::sqlite3::sqlite3
    unofficial-sodium::sodium
    reproc++
    spdlog::spdlog)
if(WIN32)
    target_link_libraries(avenoir_storage PRIVATE advapi32)
endif()

avenoir_add_tests(storage
    SOURCES
        tests/message_json_test.cpp
        tests/secrets_test.cpp
        tests/settings_test.cpp
        tests/store_test.cpp
        tests/transcript_test.cpp)
