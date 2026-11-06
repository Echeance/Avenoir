# avenoir::tools — outils natifs. Le code propre à chaque OS est isolé dans
# src/platform/ et choisi ici.

if(WIN32)
    set(_avenoir_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_platform src/platform/linux.cpp)
else()
    set(_avenoir_platform src/platform/unsupported.cpp)
endif()

avenoir_add_library(tools
    SOURCES
        src/apps.cpp
        src/register.cpp
        src/system.cpp
        src/time.cpp
        src/validation.cpp
        ${_avenoir_platform}
    DEPS
        avenoir::core)

target_include_directories(avenoir_tools PRIVATE src)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(avenoir_tools PRIVATE reproc++)
elseif(WIN32)
    target_link_libraries(avenoir_tools PRIVATE shell32)
endif()

avenoir_add_tests(tools
    SOURCES
        tests/system_test.cpp
        tests/time_test.cpp
        tests/validation_test.cpp)
