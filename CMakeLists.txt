set(requires
    "esp_lcd"
    "esp_timer"
    )
if(CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD)
    list(APPEND requires "esp_hal_lcd")
endif()

idf_component_register(
    SRCS
        "dsi_panel_espressif_ek79007.c"
        "dsi_panel_espressif_ili9881c.c"
        "dsi_panel_espressif_lt8912b.c"
        "dsi_panel_nicolaielectronics_st7701.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ${requires}
    )

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
