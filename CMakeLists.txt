# Smart Wash HMI - simulateur PC (LVGL v9 + SDL2)
cmake_minimum_required(VERSION 3.18)
project(smart_wash_hmi C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- LVGL v9 : dossier lib/lvgl s'il existe (sous-module), sinon telechargement ---
# Le code inclut "lvgl/lvgl.h" : on ajoute donc le dossier PARENT de lvgl/.
if(EXISTS ${CMAKE_SOURCE_DIR}/lib/lvgl/lvgl.h)
  set(LVGL_PARENT ${CMAKE_SOURCE_DIR}/lib)
else()
  include(FetchContent)
  set(LVGL_PARENT ${CMAKE_BINARY_DIR}/_deps)
  FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${LVGL_PARENT}/lvgl
    SOURCE_SUBDIR  _aucun_)   # telecharge seulement : LVGL est compile ci-dessous
  FetchContent_MakeAvailable(lvgl)
endif()
set(LVGL_DIR ${LVGL_PARENT}/lvgl)

find_package(SDL2 REQUIRED)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${LVGL_PARENT} ${LVGL_DIR} ${CMAKE_SOURCE_DIR}/include ${SDL2_INCLUDE_DIRS})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# --- Application ---
add_executable(smart_wash
  src/smart_wash.c
  src/machine.c
  platform/pc_sdl/main.c
  platform/pc_sdl/hal/hal.c)
target_include_directories(smart_wash PRIVATE src platform/pc_sdl)
target_link_libraries(smart_wash PRIVATE lvgl ${SDL2_LIBRARIES} m)
if(NOT MSVC)
  target_compile_options(smart_wash PRIVATE -Wall -Wextra)
endif()
