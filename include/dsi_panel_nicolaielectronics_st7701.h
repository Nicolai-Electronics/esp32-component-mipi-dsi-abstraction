#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"

#define LCD_CMD_TEOFF 0x34
#define LCD_CMD_TEON  0x35

typedef struct {
    gpio_num_t reset_pin;
    bool use_24_bit_color;  // 16-bit RGB565 or 24-bit RGB888 mode
    size_t num_fbs;         // Amount of framebuffers to allocate (0 and 1 both allocate one buffer)
} st7701_configuration_t;

esp_lcd_panel_handle_t st7701_get_panel(void);
esp_lcd_panel_io_handle_t st7701_get_panel_io(void);
esp_err_t st7701_initialize(const st7701_configuration_t* config);
esp_err_t st7701_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
