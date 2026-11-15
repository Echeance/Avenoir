# Hôte des fenêtres (décision 68) : exécutable `avenoir-ui`, placé à côté de
# `avenoir` et `avenoird`, avec la page construite dans `ui/`.
#
# Le relais et les fichiers servis (portables, testés partout) forment
# `avenoir_ui_lib`. La fenêtre demande WebKitGTK 6 et gtk4-layer-shell du
# système sous Linux (liés dynamiquement, jamais livrés), WebView2 sous
# Windows (chargeur lié en statique, Edge déjà présent dans Windows) ; sans
# eux, seul `avenoir_ui_lib` est compilé.

add_library(avenoir_ui_lib STATIC src/assets.cpp src/relay.cpp)
target_include_directories(avenoir_ui_lib PUBLIC src)
target_link_libraries(avenoir_ui_lib PUBLIC
    avenoir::core nlohmann_json::nlohmann_json spdlog::spdlog
    PRIVATE avenoir::options)

if(AVENOIR_BUILD_TESTS)
    add_executable(avenoir_ui_tests tests/assets_test.cpp tests/relay_test.cpp)
    target_link_libraries(avenoir_ui_tests PRIVATE
        avenoir_ui_lib avenoir::options Catch2::Catch2WithMain)
    avenoir_utf8_manifest(avenoir_ui_tests)
    catch_discover_tests(avenoir_ui_tests TEST_PREFIX "ui: ")
endif()

if(NOT AVENOIR_WITH_UI)
    return()
endif()
if(WIN32)
    find_package(unofficial-webview2 CONFIG)
    if(NOT unofficial-webview2_FOUND)
        message(STATUS "Interface graphique non compilée : WebView2 introuvable (vcpkg)")
        return()
    endif()
    # Application fenêtrée (pas de console), point d'entrée main().
    add_executable(avenoir_ui WIN32 src/main.cpp src/platform/windows.cpp)
    target_link_options(avenoir_ui PRIVATE /ENTRY:mainCRTStartup)
    target_link_libraries(avenoir_ui PRIVATE unofficial::webview2::webview2 dwmapi ole32 shlwapi user32
                                             gdi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(AVENOIR_WEBVIEW IMPORTED_TARGET webkitgtk-6.0 gtk4)
        pkg_check_modules(AVENOIR_LAYER_SHELL IMPORTED_TARGET gtk4-layer-shell-0)
    endif()
    if(NOT AVENOIR_WEBVIEW_FOUND)
        message(STATUS "Interface graphique non compilée : webkitgtk-6.0 ou gtk4 introuvable "
                       "(pkg-config)")
        return()
    endif()
    add_executable(avenoir_ui src/main.cpp src/platform/linux.cpp)
    target_link_libraries(avenoir_ui PRIVATE PkgConfig::AVENOIR_WEBVIEW)
    # layer-shell (widget en haut de l'écran sous Wayland) : sans lui, fenêtre
    # ordinaire.
    if(AVENOIR_LAYER_SHELL_FOUND)
        target_link_libraries(avenoir_ui PRIVATE PkgConfig::AVENOIR_LAYER_SHELL)
        target_compile_definitions(avenoir_ui PRIVATE AVENOIR_LAYER_SHELL)
    else()
        message(STATUS "Interface graphique sans gtk4-layer-shell : widget en fenêtre ordinaire")
    endif()
else()
    message(STATUS "Interface graphique : pas sous ce système")
    return()
endif()

set_target_properties(avenoir_ui PROPERTIES
    OUTPUT_NAME avenoir-ui
    RUNTIME_OUTPUT_DIRECTORY "${AVENOIR_BIN_DIR}")
target_link_libraries(avenoir_ui PRIVATE
    avenoir_ui_lib avenoir_cli_lib avenoir::ipc avenoir::options)
avenoir_utf8_manifest(avenoir_ui)
add_dependencies(avenoir_ui avenoird)

# La page (ui/) construite par Vite à côté de l'exécutable, si Node.js est là.
# Sous Windows, npm.cmd : « npm » seul y est un script pour bash.
find_program(AVENOIR_NPM NAMES npm.cmd npm)
if(AVENOIR_NPM)
    set(_ui_root "${CMAKE_SOURCE_DIR}/ui")
    set(_ui_out "${AVENOIR_BIN_DIR}/ui")
    file(GLOB_RECURSE _ui_sources CONFIGURE_DEPENDS
        "${_ui_root}/src/*" "${_ui_root}/index.html" "${_ui_root}/package-lock.json"
        "${_ui_root}/vite.config.ts" "${_ui_root}/svelte.config.js")
    add_custom_command(
        OUTPUT "${_ui_out}/index.html"
        COMMAND "${CMAKE_COMMAND}" "-DNPM=${AVENOIR_NPM}" "-DUI_ROOT=${_ui_root}"
                "-DOUT_DIR=${_ui_out}" -P "${CMAKE_CURRENT_SOURCE_DIR}/build_page.cmake"
        DEPENDS ${_ui_sources} "${CMAKE_CURRENT_SOURCE_DIR}/build_page.cmake"
        COMMENT "Construction de la page de l'interface (ui/)"
        VERBATIM)
    add_custom_target(avenoir_ui_page ALL DEPENDS "${_ui_out}/index.html")
    add_dependencies(avenoir_ui avenoir_ui_page)
else()
    message(STATUS "Page de l'interface non construite : npm introuvable")
endif()
