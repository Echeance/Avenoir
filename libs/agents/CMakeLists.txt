# avenoir::agents — orchestrateur multi-agents (docs/PROJECT.md § 2.11) : plan
# de tâches, exécution parallèle avec dépendances, outil de délégation.

avenoir_add_library(agents
    SOURCES
        src/delegate.cpp
        src/orchestrator.cpp
        src/plan.cpp
    DEPS
        avenoir::core)

target_link_libraries(avenoir_agents PRIVATE spdlog::spdlog)

avenoir_add_tests(agents
    SOURCES
        tests/agents_test.cpp)
