#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"

typedef struct {
    gpio_num_t reset_pin;
    size_t num_fbs;  // Amount of framebuffers to allocate (0 and 1 both allocate one buffer)
} ili9881c_configuration_t;

esp_lcd_panel_handle_t ili9881c_get_panel(void);
esp_err_t ili9881c_initialize(const ili9881c_configuration_t* config);
esp_err_t ili9881c_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
