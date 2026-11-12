# avenoir::ipc — transport local et JSON-RPC entre le démon et ses interfaces.
# Le code propre à chaque OS est isolé dans src/platform/ et choisi ici.

if(WIN32)
    set(_avenoir_ipc_platform src/platform/windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_avenoir_ipc_platform src/platform/linux.cpp)
else()
    set(_avenoir_ipc_platform src/platform/unsupported.cpp)
endif()

avenoir_add_library(ipc
    SOURCES
        src/peer.cpp
        ${_avenoir_ipc_platform}
    DEPS
        avenoir::core)

target_link_libraries(avenoir_ipc PRIVATE spdlog::spdlog)
if(WIN32)
    target_link_libraries(avenoir_ipc PRIVATE advapi32)
endif()

avenoir_add_tests(ipc
    SOURCES
        tests/ipc_test.cpp)
