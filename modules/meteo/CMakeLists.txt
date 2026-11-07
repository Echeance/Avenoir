# Module météo (fourni avec Avenoir) : programme séparé, lancé par l'hôte de
# modules. Il est placé, avec son manifeste, dans le dossier `modules/meteo/`
# à côté de l'exécutable `avenoir` (dossier de build, puis archive de release).

set(_meteo_dir "${AVENOIR_MODULES_OUTPUT_DIR}/meteo")

# Logique sans réseau, partagée par le programme et les tests.
add_library(avenoir_meteo_core STATIC
    src/city_store.cpp
    src/weather.cpp)
target_include_directories(avenoir_meteo_core PUBLIC src)
target_link_libraries(avenoir_meteo_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE avenoir::options)
target_compile_features(avenoir_meteo_core PUBLIC cxx_std_23)

add_executable(avenoir_meteo
    src/main.cpp
    src/open_meteo.cpp)
set_target_properties(avenoir_meteo PROPERTIES
    OUTPUT_NAME avenoir-meteo
    RUNTIME_OUTPUT_DIRECTORY "${_meteo_dir}")
avenoir_utf8_manifest(avenoir_meteo)
target_link_libraries(avenoir_meteo PRIVATE avenoir_meteo_core cpr::cpr avenoir::options)

# Le manifeste est recopié à chaque modification.
configure_file(module.toml "${_meteo_dir}/module.toml" COPYONLY)

if(AVENOIR_BUILD_TESTS)
    add_executable(avenoir_meteo_tests tests/meteo_test.cpp)
    target_link_libraries(avenoir_meteo_tests PRIVATE
        avenoir_meteo_core avenoir::modules avenoir::options Catch2::Catch2WithMain)
    target_compile_definitions(avenoir_meteo_tests PRIVATE
        AVENOIR_METEO_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/module.toml")
    avenoir_utf8_manifest(avenoir_meteo_tests)
    catch_discover_tests(avenoir_meteo_tests TEST_PREFIX "meteo: ")
endif()
