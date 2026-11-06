# Test de bout en bout de la CLI, sans LLM ni réseau : une requête traitée
# par la voie rapide, la liste des outils, puis la sortie. Lancé par ctest sur
# chaque système de la CI (Linux et Windows).
#
# Variables : AVENOIR (exécutable), WORK_DIR (dossier de travail).

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/data")

# Fournisseur jamais contacté : la voie rapide n'appelle pas le LLM.
file(WRITE "${WORK_DIR}/config.toml" "[llm]\nprovider = \"openai_compatible\"\nbase_url = \"http://127.0.0.1:9\"\nmodel = \"aucun\"\n")
file(WRITE "${WORK_DIR}/input.txt" "quelle heure est-il ?\n/outils\n/quitter\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "XDG_DATA_HOME=${WORK_DIR}/data" "LOCALAPPDATA=${WORK_DIR}/data" "NO_COLOR=1"
            "${AVENOIR}" "${WORK_DIR}/config.toml"
    INPUT_FILE "${WORK_DIR}/input.txt"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE code
    TIMEOUT 60)

if(NOT code EQUAL 0)
    message(FATAL_ERROR "avenoir a échoué (code ${code})\n--- sortie ---\n${output}\n--- erreurs ---\n${errors}")
endif()

foreach(expected "Il est " "current_time" "launch_app" "À bientôt")
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "« ${expected} » absent de la sortie\n--- sortie ---\n${output}\n--- erreurs ---\n${errors}")
    endif()
endforeach()

# Aucune séquence d'échappement hors d'un terminal.
string(ASCII 27 escape)
string(FIND "${output}" "${escape}" position)
if(NOT position EQUAL -1)
    message(FATAL_ERROR "séquence d'échappement dans une sortie non interactive\n${output}")
endif()

if(NOT EXISTS "${WORK_DIR}/data/avenoir/avenoir.db")
    message(FATAL_ERROR "base de conversations non créée dans ${WORK_DIR}/data/avenoir")
endif()
