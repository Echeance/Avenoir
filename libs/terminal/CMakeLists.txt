# avenoir::terminal — rendu dans le terminal : couleurs, cadres, Markdown léger ;
# dessins d'Avenoir (loup, titre) en données.

avenoir_add_library(terminal
    SOURCES
        src/markdown.cpp
        src/mascot.cpp
        src/style.cpp
    DEPS
        avenoir::core)

avenoir_add_tests(terminal
    SOURCES
        tests/markdown_test.cpp
        tests/style_test.cpp)
