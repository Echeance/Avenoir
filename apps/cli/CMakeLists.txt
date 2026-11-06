# Interface texte : exécutable `avenoir`.

add_executable(avenoir_cli src/main.cpp)
set_target_properties(avenoir_cli PROPERTIES OUTPUT_NAME avenoir)
target_link_libraries(avenoir_cli PRIVATE
    avenoir::core
    avenoir::llm
    avenoir::tools
    avenoir::options
    spdlog::spdlog)
