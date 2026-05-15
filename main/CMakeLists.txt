set(EXTRA_REQUIRES "")
set(EXTRA_SRCS "")
set(EXTRA_DEFINES "")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra")
    include(${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../board_config.cmake")
message(STATUS "COMPONENT DEBUG: BOARD='${BOARD}'")

if(BOARD STREQUAL "cyd28")
    set(BOARD_SRC "board_cyd28_ili9341_touch.c")
    list(APPEND EXTRA_REQUIRES esp_lcd)
    list(APPEND EXTRA_DEFINES BOARD_NATIVE_WIFI)
else()
    set(BOARD_SRC "board_waveshare_wvshr_p4_720_touch.c")
    list(APPEND EXTRA_REQUIRES esp_driver_ppa esp_mm esp_lcd_st7703 espressif__esp_hosted)
endif()

idf_component_register(
    SRCS "main.c"
         ${BOARD_SRC}
         "board_defaults.c"
         "github_api.c"
         "traffic_csv.c"
         "dashboard.c"
         "layout_cyd.c"
         "font8x16.c"
         ${EXTRA_SRCS}
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp-tls nvs_flash json esp_timer
             esp_wifi esp_netif lwip mbedtls wifi_prov ${EXTRA_REQUIRES}
)

if(EXTRA_DEFINES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${EXTRA_DEFINES})
endif()
