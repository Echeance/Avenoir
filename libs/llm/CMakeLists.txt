# avenoir::llm — fournisseurs LLM : API compatible OpenAI, Anthropic, Claude Code.

avenoir_add_library(llm
    SOURCES
        src/anthropic.cpp
        src/claude_code.cpp
        src/factory.cpp
        src/http.cpp
        src/models.cpp
        src/openai_compatible.cpp
    DEPS
        avenoir::core)

# Utilisées seulement dans les .cpp.
target_link_libraries(avenoir_llm PRIVATE
    cpr::cpr
    reproc++
    spdlog::spdlog)

avenoir_add_tests(llm
    SOURCES
        tests/anthropic_test.cpp
        tests/claude_code_test.cpp
        tests/factory_test.cpp
        tests/integration_test.cpp
        tests/models_test.cpp
        tests/openai_compatible_test.cpp)
