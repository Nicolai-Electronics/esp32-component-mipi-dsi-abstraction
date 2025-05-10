/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "ILI9981C panel";

// FPS = 80000000/(40+140+40+800)/(4+16+16+1280) = 60Hz
#define PANEL_MIPI_DSI_DPI_CLK_MHZ       80
#define PANEL_MIPI_DSI_LCD_H_RES         800
#define PANEL_MIPI_DSI_LCD_V_RES         1280
#define PANEL_MIPI_DSI_LCD_HSYNC         40
#define PANEL_MIPI_DSI_LCD_HBP           140
#define PANEL_MIPI_DSI_LCD_HFP           40
#define PANEL_MIPI_DSI_LCD_VSYNC         4
#define PANEL_MIPI_DSI_LCD_VBP           16
#define PANEL_MIPI_DSI_LCD_VFP           16
#define PANEL_MIPI_DSI_LANE_NUM          2     // 2 data lanes
#define PANEL_MIPI_DSI_LANE_BITRATE_MBPS 1000  // 1Gbps

static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;

esp_lcd_panel_handle_t ili9881c_get_panel(void) {
    return mipi_dpi_panel;
}

esp_err_t ili9881c_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt) {
    if (h_res) {
        *h_res = PANEL_MIPI_DSI_LCD_H_RES;
    }
    if (v_res) {
        *v_res = PANEL_MIPI_DSI_LCD_V_RES;
    }
    if (color_fmt) {
        *color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB888;
    }

    return ESP_OK;
}

esp_err_t ili9881c_initialize(gpio_num_t reset_pin) {
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
        .lcd_cmd_bits = 8,    // according to the LCD spec
        .lcd_param_bits = 8,  // according to the LCD spec
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
    mipi_dpi_panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANEL_MIPI_DSI_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
        .video_timing =
            {
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

    ili9881c_vendor_config_t vendor_config = {
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = PANEL_MIPI_DSI_LANE_NUM,
            },
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = reset_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9881c(mipi_dbi_io, &lcd_dev_config, &mipi_dpi_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));

    /*ESP_LOGI(TAG, "Register DPI panel event callback for flush ready notification");
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        //.on_color_trans_done = ...,
        //.on_refresh_done = ...,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(mipi_dpi_panel, &cbs, display));*/

    return ESP_OK;
}
