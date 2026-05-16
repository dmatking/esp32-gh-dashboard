cmake_minimum_required(VERSION 3.16)
project(gh_dashboard_sim C)

include(screencap/cmake/screencap.cmake)

set(MAIN ../main)

# Board selection: cmake -DSIM_BOARD=cyd28  (default: p4)
set(SIM_BOARD "p4" CACHE STRING "Sim board target (p4, cyd28, cyd28s3, cyd35)")

screencap_add_sim(gh_dashboard_sim
    SOURCES
        main_sim.c
        board_sim.c
        layout_editor.c
        ${MAIN}/board_defaults.c
        ${MAIN}/dashboard.c
        ${MAIN}/layout_cyd.c
        ${MAIN}/font8x16.c
    INCLUDES
        ${MAIN}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(SIM_BOARD STREQUAL "cyd28" OR SIM_BOARD STREQUAL "cyd28s3")
    # Both 2.8" CYDs are 320x240 in landscape; the S3 just uses a different
    # chip and a slightly different pinout — same visual layout.
    target_compile_definitions(gh_dashboard_sim PRIVATE SIM_LCD_W=320 SIM_LCD_H=240)
elseif(SIM_BOARD STREQUAL "cyd35")
    target_compile_definitions(gh_dashboard_sim PRIVATE SIM_LCD_W=480 SIM_LCD_H=320)
endif()
