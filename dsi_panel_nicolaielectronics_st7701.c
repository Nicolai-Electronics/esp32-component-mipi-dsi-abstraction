/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_st7701.h"

static const char *TAG = "ST7701 panel";

// FPS = 30000000/(40+40+30+480)/(16+16+16+800) = 60Hz
#define PANEL_MIPI_DSI_DPI_CLK_MHZ  30
#define PANEL_MIPI_DSI_LCD_H_RES    480
#define PANEL_MIPI_DSI_LCD_V_RES    800
#define PANEL_MIPI_DSI_LCD_HSYNC    40
#define PANEL_MIPI_DSI_LCD_HBP      40
#define PANEL_MIPI_DSI_LCD_HFP      30
#define PANEL_MIPI_DSI_LCD_VSYNC    16
#define PANEL_MIPI_DSI_LCD_VBP      16
#define PANEL_MIPI_DSI_LCD_VFP      16
#define PANEL_MIPI_DSI_LANE_NUM          2    // 2 data lanes
#define PANEL_MIPI_DSI_LANE_BITRATE_MBPS 250  // 250 Mbps

static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;

esp_lcd_panel_handle_t st7701_get_panel(void) {
    return mipi_dpi_panel;
}

void st7701_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt) {
    if (h_res) {
        *h_res = PANEL_MIPI_DSI_LCD_H_RES;
    }
    if (v_res) {
        *v_res = PANEL_MIPI_DSI_LCD_V_RES;
    }
    if (color_fmt) {
        *color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;
    }
}

void st7701_initialize(gpio_num_t reset_pin) {
    // create MIPI DSI bus first, it will initialize the DSI PHY as well
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = PANEL_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = PANEL_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install MIPI DSI LCD control IO");
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD spec
        .lcd_param_bits = 8, // according to the LCD spec
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANEL_MIPI_DSI_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size = PANEL_MIPI_DSI_LCD_H_RES,
            .v_size = PANEL_MIPI_DSI_LCD_V_RES,
            .hsync_back_porch = PANEL_MIPI_DSI_LCD_HBP,
            .hsync_pulse_width = PANEL_MIPI_DSI_LCD_HSYNC,
            .hsync_front_porch = PANEL_MIPI_DSI_LCD_HFP,
            .vsync_back_porch = PANEL_MIPI_DSI_LCD_VBP,
            .vsync_pulse_width = PANEL_MIPI_DSI_LCD_VSYNC,
            .vsync_front_porch = PANEL_MIPI_DSI_LCD_VFP,
        },
        .flags.use_dma2d = true,
    };

    st7701_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = reset_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(mipi_dbi_io, &lcd_dev_config, &mipi_dpi_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));

    /*ESP_LOGI(TAG, "Register DPI panel event callback for flush ready notification");
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        //.on_color_trans_done = ...,
        //.on_refresh_done = ...,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(mipi_dpi_panel, &cbs, display));*/
}
