#pragma once

#include "driver/gpio.h"
#include "esp_lcd_mipi_dsi.h"

#define LCD_CMD_TEOFF 0x34
#define LCD_CMD_TEON  0x35

esp_lcd_panel_handle_t st7701_get_panel(void);
esp_lcd_panel_io_handle_t st7701_get_panel_io(void);
void st7701_initialize(gpio_num_t reset_pin);
void st7701_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt);
