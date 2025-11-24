#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"

typedef struct {
    gpio_num_t reset_pin;
    size_t num_fbs;  // Amount of framebuffers to allocate (0 and 1 both allocate one buffer)
} ek79007_configuration_t;

esp_lcd_panel_handle_t ek79007_get_panel(void);
esp_err_t ek79007_initialize(const ek79007_configuration_t* config);
esp_err_t ek79007_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
