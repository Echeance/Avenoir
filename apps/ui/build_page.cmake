# Construit la page de l'interface (ui/) dans OUT_DIR : dépendances
# installées d'après le fichier de verrou si elles manquent, puis Vite.
# Appelé par la cible avenoir_ui_page : cmake -DNPM=… -DUI_ROOT=… -DOUT_DIR=… -P build_page.cmake
if(NOT EXISTS "${UI_ROOT}/node_modules")
    execute_process(COMMAND "${NPM}" ci --no-fund --no-audit
                    WORKING_DIRECTORY "${UI_ROOT}" RESULT_VARIABLE installed)
    if(NOT installed EQUAL 0)
        message(FATAL_ERROR "npm ci a échoué dans ${UI_ROOT}")
    endif()
endif()
execute_process(COMMAND "${NPM}" run build -- --outDir "${OUT_DIR}" --emptyOutDir
                WORKING_DIRECTORY "${UI_ROOT}" RESULT_VARIABLE built)
if(NOT built EQUAL 0)
    message(FATAL_ERROR "construction de la page (npm run build) échouée")
endif()
