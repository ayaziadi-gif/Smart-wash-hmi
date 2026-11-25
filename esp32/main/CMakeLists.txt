# Le composant "main" compile le demarrage ESP32 (main.c) ET le code portable
# de l'interface, partage avec le simulateur PC (../../src).
set(APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../src)

idf_component_register(
    SRCS "main.c" "${APP_DIR}/smart_wash.c" "${APP_DIR}/machine.c"
    INCLUDE_DIRS "." "${APP_DIR}")
