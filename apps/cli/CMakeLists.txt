# Interface texte : exécutable `avenoir`.

add_executable(avenoir_cli
    src/console.cpp
    src/conversations.cpp
    src/main.cpp
    src/prompt.cpp)
set_target_properties(avenoir_cli PROPERTIES OUTPUT_NAME avenoir)
avenoir_utf8_manifest(avenoir_cli)
target_link_libraries(avenoir_cli PRIVATE
    avenoir::core
    avenoir::llm
    avenoir::storage
    avenoir::tools
    avenoir::options
    replxx::replxx
    spdlog::spdlog)
