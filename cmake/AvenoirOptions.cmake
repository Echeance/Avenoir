# Options de compilation communes à toutes les cibles du projet, portées par
# la cible d'interface `avenoir::options` (à lier en PRIVATE).

add_library(avenoir_options INTERFACE)
add_library(avenoir::options ALIAS avenoir_options)

if(MSVC)
    target_compile_options(avenoir_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    if(AVENOIR_WARNINGS_AS_ERRORS)
        target_compile_options(avenoir_options INTERFACE /WX)
    endif()
    # Pas de macros min/max ni d'en-têtes Windows superflus.
    target_compile_definitions(avenoir_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(avenoir_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough)
    if(AVENOIR_WARNINGS_AS_ERRORS)
        target_compile_options(avenoir_options INTERFACE -Werror)
    endif()
endif()

if(AVENOIR_SANITIZE)
    if(MSVC)
        message(WARNING "AVENOIR_SANITIZE n'est pris en charge qu'avec GCC et Clang : ignoré.")
    else()
        set(_avenoir_san -fsanitize=address,undefined -fno-omit-frame-pointer
                         -fno-sanitize-recover=undefined)
        target_compile_options(avenoir_options INTERFACE ${_avenoir_san})
        target_link_options(avenoir_options INTERFACE ${_avenoir_san})
    endif()
endif()

# Windows : intègre le manifeste qui fait d'UTF-8 la page de codes active
# (arguments, chemins, environnement). Sans effet ailleurs.
#
#   avenoir_utf8_manifest(<cible exécutable>)
function(avenoir_utf8_manifest target)
    if(WIN32)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/utf8.manifest")
    endif()
endfunction()

# Déclare une bibliothèque interne `avenoir::<nom>` selon la structure
# libs/<nom>/{include,src,tests}.
#
#   avenoir_add_library(core SOURCES src/a.cpp DEPS avenoir::autre)
function(avenoir_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    set(target avenoir_${name})
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(avenoir::${name} ALIAS ${target})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${target} PUBLIC ${ARG_DEPS} PRIVATE avenoir::options)
    target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

# Déclare l'exécutable de tests d'une bibliothèque (Catch2, découvert par ctest).
#
#   avenoir_add_tests(core SOURCES tests/a_test.cpp)
function(avenoir_add_tests name)
    if(NOT AVENOIR_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    set(target avenoir_${name}_tests)
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE avenoir::${name} avenoir::options
                                            Catch2::Catch2WithMain)
    # Sans lui, les noms de tests accentués passés par ctest arrivent abîmés.
    avenoir_utf8_manifest(${target})
    catch_discover_tests(${target} TEST_PREFIX "${name}: ")
endfunction()
