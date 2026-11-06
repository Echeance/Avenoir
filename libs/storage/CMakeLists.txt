# avenoir::storage — conversations persistantes (SQLite).

avenoir_add_library(storage
    SOURCES
        src/message_json.cpp
        src/paths.cpp
        src/store.cpp
    DEPS
        avenoir::core)

target_link_libraries(avenoir_storage PRIVATE
    unofficial::sqlite3::sqlite3)

avenoir_add_tests(storage
    SOURCES
        tests/message_json_test.cpp
        tests/store_test.cpp)
