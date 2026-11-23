# Desktop simulator: renders Orbit's real UI code into image files, so screens
# can be reviewed without flashing the watch. Run it with tools/sim.ps1.
cmake_minimum_required(VERSION 3.20)
project(orbit_sim C)

set(CMAKE_C_STANDARD 11)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL ${ROOT}/managed_components/lvgl__lvgl)

file(GLOB_RECURSE LVGL_SRC ${LVGL}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LVGL})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -O2 -w)

file(GLOB_RECURSE ORBIT_SRC
    ${ROOT}/main/core/*.c
    ${ROOT}/main/ui/*.c
    ${ROOT}/main/apps/*.c
    ${ROOT}/main/assets/gen/*.c)
add_executable(orbit_sim sim_main.c svc_sim.c ${ORBIT_SRC})
target_compile_definitions(orbit_sim PRIVATE ORBIT_SIM=1)
target_compile_options(orbit_sim PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(orbit_sim PRIVATE lvgl m)
