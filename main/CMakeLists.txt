set(EXTRA_REQUIRES "")
set(EXTRA_SRCS "")
set(EXTRA_DEFINES "")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra")
    include(${CMAKE_CURRENT_LIST_DIR}/main.cmake.extra)
endif()

idf_component_register(
    SRCS "main.c"
         "board_waveshare_wvshr_p4_720_touch.c"
         "board_defaults.c"
         "wifi.c"
         "github_api.c"
         "dashboard.c"
         "font8x16.c"
         ${EXTRA_SRCS}
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp-tls nvs_flash json esp_timer
             esp_wifi esp_driver_ppa esp_mm mbedtls esp_lcd_st7703
             esp_netif lwip ${EXTRA_REQUIRES}
)

if(EXTRA_DEFINES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${EXTRA_DEFINES})
endif()
