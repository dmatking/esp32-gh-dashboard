cmake_minimum_required(VERSION 3.16)
project(gh_dashboard_sim C)

include(screencap/cmake/screencap.cmake)

set(MAIN ../main)

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
