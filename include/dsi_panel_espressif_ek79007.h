#pragma once

#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"

esp_lcd_panel_handle_t ek79007_get_panel(void);
void ek79007_initialize(gpio_num_t reset_pin);
void ek79007_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
