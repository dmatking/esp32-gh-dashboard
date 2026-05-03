cmake_minimum_required(VERSION 3.16)
project(gh_dashboard_sim C)

include(screencap/cmake/screencap.cmake)

set(MAIN ../main)

# Board selection: cmake -DSIM_BOARD=cyd28  (default: p4)
set(SIM_BOARD "p4" CACHE STRING "Sim board target (p4, cyd28)")

screencap_add_sim(gh_dashboard_sim
    SOURCES
        main_sim.c
        board_sim.c
        ${MAIN}/dashboard.c
        ${MAIN}/font8x16.c
    INCLUDES
        ${MAIN}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(SIM_BOARD STREQUAL "cyd28")
    target_compile_definitions(gh_dashboard_sim PRIVATE SIM_LCD_W=320 SIM_LCD_H=240)
endif()
