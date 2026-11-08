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
        src/app_index.cpp
        src/apps.cpp
        src/register.cpp
        src/system.cpp
        src/system_control.cpp
        src/time.cpp
        src/validation.cpp
        ${_avenoir_platform}
    DEPS
        avenoir::core)

target_include_directories(avenoir_tools PRIVATE src)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(avenoir_tools PRIVATE reproc++)
elseif(WIN32)
    target_link_libraries(avenoir_tools PRIVATE shell32 shlwapi ole32 oleaut32 uuid powrprof wbemuuid windowsapp)
endif()

avenoir_add_tests(tools
    SOURCES
        tests/app_index_test.cpp
        tests/system_control_test.cpp
        tests/system_test.cpp
        tests/time_test.cpp
        tests/validation_test.cpp)

# Logique portable (index des applications, contrôle du système) testée par
# ses en-têtes internes.
if(TARGET avenoir_tools_tests)
    target_include_directories(avenoir_tools_tests PRIVATE src)
endif()
