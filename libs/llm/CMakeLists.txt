# avenoir::llm — fournisseurs LLM : API compatible OpenAI, Anthropic, Claude Code.
# Le code propre à chaque OS est isolé dans src/platform/ et choisi ici.

if(WIN32)
    set(_avenoir_llm_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_llm_platform src/platform/linux.cpp)
else()
    set(_avenoir_llm_platform src/platform/unsupported.cpp)
endif()

avenoir_add_library(llm
    SOURCES
        src/anthropic.cpp
        src/claude_code.cpp
        src/discovery.cpp
        src/factory.cpp
        src/http.cpp
        src/models.cpp
        src/openai_compatible.cpp
        ${_avenoir_llm_platform}
    DEPS
        avenoir::core)

# Utilisées seulement dans les .cpp.
target_link_libraries(avenoir_llm PRIVATE
    cpr::cpr
    reproc++
    spdlog::spdlog)
if(WIN32)
    target_link_libraries(avenoir_llm PRIVATE ws2_32)
endif()

avenoir_add_tests(llm
    SOURCES
        tests/anthropic_test.cpp
        tests/claude_code_test.cpp
        tests/discovery_test.cpp
        tests/factory_test.cpp
        tests/integration_test.cpp
        tests/models_test.cpp
        tests/openai_compatible_test.cpp)
