# avenoir::terminal — rendu dans le terminal : couleurs, cadres, Markdown léger.

avenoir_add_library(terminal
    SOURCES
        src/markdown.cpp
        src/style.cpp
    DEPS
        avenoir::core)

avenoir_add_tests(terminal
    SOURCES
        tests/markdown_test.cpp
        tests/style_test.cpp)
