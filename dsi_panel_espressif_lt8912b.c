// SPDX-FileCopyrightText: 2025 Nicolai Electronics
// SPDX-License-Identifier: MIT

#include "include/dsi_panel_espressif_lt8912b.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_lt8912b.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define PANEL_MIPI_DSI_LANE_NUM          2
#define PANEL_MIPI_DSI_LANE_BITRATE_MBPS 500  // 1Gbps
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS   1

static const char* TAG = "LT8912B panel";

static const esp_lcd_dpi_panel_config_t dpi_configs[] = {
    LT8912B_800x600_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
    LT8912B_1024x768_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
    LT8912B_1280x720_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
    LT8912B_1280x800_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
    LT8912B_1920x1080_PANEL_30HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS)};

static const esp_lcd_panel_lt8912b_video_timing_t video_timings[] = {
    ESP_LCD_LT8912B_VIDEO_TIMING_800x600_60Hz(), ESP_LCD_LT8912B_VIDEO_TIMING_1024x768_60Hz(),
    ESP_LCD_LT8912B_VIDEO_TIMING_1280x720_60Hz(), ESP_LCD_LT8912B_VIDEO_TIMING_1280x800_60Hz(),
    ESP_LCD_LT8912B_VIDEO_TIMING_1920x1080_30Hz()};

static esp_lcd_panel_handle_t mipi_panel = NULL;
static esp_lcd_panel_io_handle_t io = NULL;
static esp_lcd_panel_io_handle_t io_cec_dsi = NULL;
static esp_lcd_panel_io_handle_t io_avi = NULL;
static lt8912b_resolution_t panel_resolution = LT8912B_RESOLUTION_800X600;

esp_lcd_panel_handle_t lt8912b_get_panel(void) {
    return mipi_panel;
}

esp_err_t lt8912b_get_parameters(size_t* h_res, size_t* v_res, lcd_color_rgb_pixel_format_t* color_fmt) {
    if (h_res) {
        switch (panel_resolution) {
            case LT8912B_RESOLUTION_800X600:
                *h_res = 800;
                break;
            case LT8912B_RESOLUTION_1024X768:
                *h_res = 1024;
                break;
            case LT8912B_RESOLUTION_1280X720:
                *h_res = 1280;
                break;
            case LT8912B_RESOLUTION_1280X800:
                *h_res = 1280;
                break;
            case LT8912B_RESOLUTION_1920X1080:
                *h_res = 1920;
                break;
            default:
                break;
        }
    }
    if (v_res) {
        switch (panel_resolution) {
            case LT8912B_RESOLUTION_800X600:
                *v_res = 600;
                break;
            case LT8912B_RESOLUTION_1024X768:
                *v_res = 768;
                break;
            case LT8912B_RESOLUTION_1280X720:
                *v_res = 720;
                break;
            case LT8912B_RESOLUTION_1280X800:
                *v_res = 800;
                break;
            case LT8912B_RESOLUTION_1920X1080:
                *v_res = 1080;
                break;
            default:
                break;
        }
    }
    if (color_fmt) {
        *color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB888;
    }
    return ESP_OK;
}

esp_err_t lt8912b_initialize(gpio_num_t reset_pin, i2c_master_bus_handle_t i2c_handle,
                             lt8912b_resolution_t resolution) {
    if (resolution >= LT8912B_RESOLUTION_MAX) {
        ESP_LOGE(TAG, "Invalid resolution");
        return ESP_ERR_INVALID_ARG;
    }

    panel_resolution = resolution;

    // I2C configuration

    uint32_t i2c_speed = 100000;

    esp_lcd_panel_io_i2c_config_t io_config = LT8912B_IO_CFG(i2c_speed, LT8912B_IO_I2C_MAIN_ADDRESS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config, &io));

    esp_lcd_panel_io_i2c_config_t io_config_cec = LT8912B_IO_CFG(i2c_speed, LT8912B_IO_I2C_CEC_ADDRESS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config_cec, &io_cec_dsi));

    esp_lcd_panel_io_i2c_config_t io_config_avi = LT8912B_IO_CFG(i2c_speed, LT8912B_IO_I2C_AVI_ADDRESS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config_avi, &io_avi));

    // MIPI DSI bus
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = PANEL_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = PANEL_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    // LT8912B MIPI DSI data
    lt8912b_vendor_config_t vendor_config = {
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_configs[panel_resolution],
                .lane_num = 2,
            },
    };

    memcpy(&vendor_config.video_timing, &video_timings[panel_resolution], sizeof(esp_lcd_panel_lt8912b_video_timing_t));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = reset_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };

    const esp_lcd_panel_lt8912b_io_t io_all = {
        .main = io,
        .cec_dsi = io_cec_dsi,
        .avi = io_avi,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_lt8912b(&io_all, &panel_config, &mipi_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_panel));

    return ESP_OK;
}
