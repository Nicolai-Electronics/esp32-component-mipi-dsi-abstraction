#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"

typedef enum {
    LT8912B_RESOLUTION_800X600 = 0,
    LT8912B_RESOLUTION_1024X768,
    LT8912B_RESOLUTION_1280X720,
    LT8912B_RESOLUTION_1280X800,
    LT8912B_RESOLUTION_1920X1080,
    LT8912B_RESOLUTION_MAX
} lt8912b_resolution_t;

esp_lcd_panel_handle_t lt8912b_get_panel(void);
esp_err_t lt8912b_initialize(gpio_num_t reset_pin, i2c_master_bus_handle_t i2c_handle, lt8912b_resolution_t resolution);
esp_err_t lt8912b_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
