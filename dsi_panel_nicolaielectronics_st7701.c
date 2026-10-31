/*
 * SPDX-FileCopyrightText: 2026 Nicolai Electronics
 *
 * SPDX-License-Identifier: MIT
 */

#include "dsi_panel_nicolaielectronics_st7701.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD
#include "hal/mipi_dsi_hal.h"
#endif

static const char* TAG = "ST7701 panel";

// FPS = 30000000/(40+40+30+480)/(16+16+16+800) = 60Hz
#define PANEL_MIPI_DSI_DPI_CLK_MHZ 30
#define PANEL_MIPI_DSI_LCD_H_RES   480
#define PANEL_MIPI_DSI_LCD_V_RES   800
#define PANEL_MIPI_DSI_LCD_HSYNC   40
#define PANEL_MIPI_DSI_LCD_HBP     40
#define PANEL_MIPI_DSI_LCD_HFP     30
#define PANEL_MIPI_DSI_LCD_VSYNC   16
#define PANEL_MIPI_DSI_LCD_VBP     16
#define PANEL_MIPI_DSI_LCD_VFP     16

#define PANEL_MIPI_DSI_LANE_NUM          2
#define PANEL_MIPI_DSI_LANE_BITRATE_MBPS 500

#define LCD_CMD_ST7701_VCOMS 0xB1  // VCOMS (Command 2 BK1)

static const st7701_lcd_init_cmd_t tanmatsu_display_init_sequence[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},  // Regular command function
    {LCD_CMD_NORON, (uint8_t[]){0x00}, 0, 0},                 // Turn on normal display mode
    {0xEF, (uint8_t[]){0x08}, 1, 0},                          //

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},  // Command 2 BK0 function
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},                    // LNESET (Display Line Setting): (0x63+1)*8 = 800 lines
    {0xC1, (uint8_t[]){0x10, 0x10}, 2, 0},                    // PORCTRL (Porch Control): VBP = 16, VFP = 16
    {0xC2, (uint8_t[]){0x37, 0x04}, 2, 0},  // INVSET (Inversion sel. & frame rate control): PCLK=512+(4*16) = 576
    {0xCC, (uint8_t[]){0x38}, 1, 0},        //
    {0xB0, (uint8_t[]){0x40, 0xC9, 0x90, 0x0D, 0x0F, 0x04, 0x00, 0x07, 0x07, 0x1C, 0x04, 0x52, 0x0F, 0xDF, 0x26, 0xCF},
     16, 0},  // PVGAMCTRL
    {0xB1, (uint8_t[]){0x40, 0xC9, 0xCF, 0x0C, 0x90, 0x04, 0x00, 0x07, 0x08, 0x1B, 0x06, 0x55, 0x13, 0x62, 0xE7, 0xCF},
     16, 0},  // NVGAMCTRL

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},  // Command 2 BK1 function
    {0xB0, (uint8_t[]){0x5D}, 1, 0},                          // VRHS
    {LCD_CMD_ST7701_VCOMS, (uint8_t[]){0x2D}, 1, 0},          // VCOMS
    {0xB2, (uint8_t[]){0x07}, 1, 0},                          // VGH
    {0xB3, (uint8_t[]){0x80}, 1, 0},                          // TESTCMD
    {0xB5, (uint8_t[]){0x08}, 1, 0},                          // VGLS
    {0xB7, (uint8_t[]){0x85}, 1, 0},                          // PWCTRL1
    {0xB8, (uint8_t[]){0x20}, 1, 0},                          // PWCTRL2
    {0xB9, (uint8_t[]){0x10}, 1, 0},                          // DGMLUTR
    {0xC1, (uint8_t[]){0x78}, 1, 0},                          // SPD1
    {0xC2, (uint8_t[]){0x78}, 1, 0},                          // SPD2
    {0xD0, (uint8_t[]){0x88}, 1, 100},                        // MIPISET1
    {0xE0, (uint8_t[]){0x00, 0x19, 0x02}, 3, 0},              //
    {0xE1, (uint8_t[]){0x05, 0xA0, 0x07, 0xA0, 0x04, 0xA0, 0x06, 0xA0, 0x00, 0x44, 0x44}, 11, 0},        //
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 12, 0},  //
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},                                                   //
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},                                                               //
    {0xE5, (uint8_t[]){0x0D, 0x31, 0xC8, 0xAF, 0x0F, 0x33, 0xC8, 0xAF, 0x09, 0x2D, 0xC8, 0xAF, 0x0B, 0x2F, 0xC8, 0xAF},
     16, 0},                                            //
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},  //
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},              //
    {0xE8, (uint8_t[]){0x0C, 0x30, 0xC8, 0xAF, 0x0E, 0x32, 0xC8, 0xAF, 0x08, 0x2C, 0xC8, 0xAF, 0x0A, 0x2E, 0xC8, 0xAF},
     16, 0},                                                              //
    {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x44, 0x00, 0x40}, 7, 0},  //
    {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},                                //
    {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x45, 0x67, 0x98, 0xBA},
     16, 0},  //

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},  // Regular command function
    {LCD_CMD_SLPOUT, (uint8_t[]){0x00}, 0, 120},              // Exit sleep mode
    {LCD_CMD_DISPON, (uint8_t[]){0x00}, 0, 0},                // Display on (enable frame buffer output)
};

// Init sequence for the SW39480800PN03-01-1 display variant, identified by display ID ID0=0x42 ID1=0x00 ID2=0x01
static const st7701_lcd_init_cmd_t tanmatsu_display_2_init_sequence[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {LCD_CMD_SLPOUT, (uint8_t[]){0x00}, 0, 120},  // Exit sleep mode

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},  // Command 2 page 0x13 function
    {0xEF, (uint8_t[]){0x08}, 1, 0},                          //

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},  // Command 2 BK0 function
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},                    // LNESET (Display Line Setting): (0x63+1)*8 = 800 lines
    {0xC1, (uint8_t[]){0x0C, 0x02}, 2, 0},                    // PORCTRL (Porch Control)
    {0xC2, (uint8_t[]){0x01, 0x02}, 2, 0},                    // INVSET (Inversion sel. & frame rate control)
    {0xC7, (uint8_t[]){0x00}, 1, 0},                          //
    {0xCC, (uint8_t[]){0x10}, 1, 0},                          //
    {0xB0, (uint8_t[]){0x15, 0x16, 0x1C, 0x0C, 0x0E, 0x04, 0x08, 0x06, 0x06, 0x24, 0x01, 0x0D, 0x0E, 0x21, 0x24, 0x0B},
     16, 0},  // PVGAMCTRL
    {0xB1, (uint8_t[]){0x15, 0x0C, 0x16, 0x09, 0x0B, 0x04, 0x08, 0x0A, 0x0A, 0x24, 0x09, 0x14, 0x13, 0x28, 0x2D, 0x15},
     16, 0},  // NVGAMCTRL

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},  // Command 2 BK1 function
    {0xB0, (uint8_t[]){0x5D}, 1, 0},                          // VRHS
    {LCD_CMD_ST7701_VCOMS, (uint8_t[]){0x2E}, 1, 0},          // VCOMS
    {0xB2, (uint8_t[]){0x82}, 1, 0},                          // VGH
    {0xB3, (uint8_t[]){0x80}, 1, 0},                          // TESTCMD
    {0xB5, (uint8_t[]){0x45}, 1, 0},                          // VGLS
    {0xB7, (uint8_t[]){0x8D}, 1, 0},                          // PWCTRL1
    {0xB8, (uint8_t[]){0x33}, 1, 0},                          // PWCTRL2
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},                    // DGMLUTR
    {0xBB, (uint8_t[]){0x03}, 1, 0},                          //
    {0xBC, (uint8_t[]){0x3E}, 1, 0},                          //
    {0xC1, (uint8_t[]){0x78}, 1, 0},                          // SPD1
    {0xC2, (uint8_t[]){0x78}, 1, 0},                          // SPD2
    {0xD0, (uint8_t[]){0x88}, 1, 10},                         // MIPISET1
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},              //
    {0xE1, (uint8_t[]){0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},        //
    {0xE2, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 12, 0},  //
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x00}, 4, 0},                                                   //
    {0xE4, (uint8_t[]){0x22, 0x00}, 2, 0},                                                               //
    {0xE5, (uint8_t[]){0x04, 0x34, 0x9A, 0xA0, 0x06, 0x34, 0x9A, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     16, 0},                                            //
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x00}, 4, 0},  //
    {0xE7, (uint8_t[]){0x22, 0x00}, 2, 0},              //
    {0xE8, (uint8_t[]){0x05, 0x34, 0x9A, 0xA0, 0x07, 0x34, 0x9A, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     16, 0},                                                              //
    {0xEB, (uint8_t[]){0x02, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},  //
    {0xEC, (uint8_t[]){0x00, 0x00}, 2, 0},                                //
    {0xED, (uint8_t[]){0xFA, 0x45, 0x0B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xB0, 0x54, 0xAF},
     16, 0},                                                        //
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},  //

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},  // Command 2 page 0x13 function
    {0xE8, (uint8_t[]){0x00, 0x0E}, 2, 0},                    //
    {LCD_CMD_SLPOUT, (uint8_t[]){0x00}, 0, 120},              // Exit sleep mode
    {0xE8, (uint8_t[]){0x00, 0x0C}, 2, 10},                   //
    {0xE8, (uint8_t[]){0x00, 0x00}, 2, 0},                    //

    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},  // Command 2 BK0 function
    {LCD_CMD_MADCTL, (uint8_t[]){0x00}, 1, 0},                // Memory data access control

    {LCD_CMD_DISPON, (uint8_t[]){0x00}, 0, 20},  // Display on (enable frame buffer output)
};

static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;

static st7701_configuration_t st7701_config = {0};

// Current VCOMS value, matches the value set in tanmatsu_display_init_sequence above
static uint8_t st7701_vcom = 0x2D;

// VCOMS value read back from the panel's NVM/OTP at reset, before the init sequence overwrites it
static uint8_t st7701_vcom_otp = 0x00;
static bool st7701_vcom_otp_valid = false;

// Display ID0/ID1/ID2 read back from the panel via RDDID (0x04) at reset, used to distinguish between
// different display variants that otherwise share the same ST7701 controller.
static uint8_t st7701_id[3] = {0x00, 0x00, 0x00};
static bool st7701_id_valid = false;

// Set once the display ID has been read, when it matches the second display variant
// (ID0=0x42 ID1=0x00 ID2=0x01) and tanmatsu_display_2_init_sequence is in use.
static bool st7701_is_display_2 = false;

static esp_err_t st7701_enter_cmd2_bk1(void) {
    return esp_lcd_panel_io_tx_param(mipi_dbi_io, 0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5);
}

static esp_err_t st7701_exit_cmd2_bk1(void) {
    return esp_lcd_panel_io_tx_param(mipi_dbi_io, 0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5);
}

esp_lcd_panel_handle_t st7701_get_panel(void) {
    return mipi_dpi_panel;
}

esp_lcd_panel_io_handle_t st7701_get_panel_io(void) {
    return mipi_dbi_io;
}

esp_err_t st7701_get_parameters(size_t* h_res, size_t* v_res, lcd_color_format_t* color_fmt) {
    if (h_res) {
        *h_res = PANEL_MIPI_DSI_LCD_H_RES;
    }
    if (v_res) {
        *v_res = PANEL_MIPI_DSI_LCD_V_RES;
    }
    if (color_fmt) {
        *color_fmt = st7701_config.use_24_bit_color ? LCD_COLOR_FMT_RGB888 : LCD_COLOR_FMT_RGB565;
    }

    return ESP_OK;
}

esp_err_t st7701_get_vcom(uint8_t* vcom) {
    if (!vcom) {
        return ESP_ERR_INVALID_ARG;
    }
    *vcom = st7701_vcom;
    return ESP_OK;
}

esp_err_t st7701_set_vcom(uint8_t vcom) {
    if (!mipi_dbi_io) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(st7701_enter_cmd2_bk1(), TAG, "failed to switch to command 2 BK1");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(mipi_dbi_io, LCD_CMD_ST7701_VCOMS, (uint8_t[]){vcom}, 1), TAG,
                        "failed to set VCOMS");
    ESP_RETURN_ON_ERROR(st7701_exit_cmd2_bk1(), TAG, "failed to switch back to regular command set");

    st7701_vcom = vcom;
    return ESP_OK;
}

esp_err_t st7701_get_vcom_otp(uint8_t* vcom) {
    if (!vcom) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!st7701_vcom_otp_valid) {
        return ESP_ERR_INVALID_STATE;
    }
    *vcom = st7701_vcom_otp;
    return ESP_OK;
}

esp_err_t st7701_get_id(uint8_t id[3]) {
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!st7701_id_valid) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(id, st7701_id, sizeof(st7701_id));
    return ESP_OK;
}

bool st7701_is_display_variant_2(void) {
    return st7701_is_display_2;
}

esp_err_t st7701_initialize(const st7701_configuration_t* config) {
    memcpy(&st7701_config, config, sizeof(st7701_configuration_t));
    // create MIPI DSI bus first, it will initialize the DSI PHY as well
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = PANEL_MIPI_DSI_LANE_NUM,
        .lane_bit_rate_mbps = PANEL_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "failed to create MIPI DSI bus");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control IO");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,    // according to the LCD spec
        .lcd_param_bits = 8,  // according to the LCD spec
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io), TAG,
                        "failed to install MIPI DSI DBI IO");

    esp_err_t id_res = esp_lcd_panel_io_rx_param(mipi_dbi_io, LCD_CMD_RDDID, st7701_id, 3);
#if CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD
    if (id_res == ESP_OK && mipi_dsi_hal_host_gen_read_timed_out()) {
        ESP_LOGE(TAG, "No response from panel while reading display ID, is a display connected?");
        return ESP_ERR_NOT_FOUND;
    }
#endif
    if (id_res == ESP_OK) {
        st7701_id_valid = true;
        st7701_is_display_2 = (st7701_id[0] == 0x42 && st7701_id[1] == 0x00 && st7701_id[2] == 0x01);
        ESP_LOGI(TAG, "Display ID: ID0=0x%02X ID1=0x%02X ID2=0x%02X", st7701_id[0], st7701_id[1], st7701_id[2]);
    } else {
        ESP_LOGW(TAG, "Failed to read display ID: %s", esp_err_to_name(id_res));
    }

    ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANEL_MIPI_DSI_DPI_CLK_MHZ,
        .in_color_format = config->use_24_bit_color ? LCD_COLOR_FMT_RGB888 : LCD_COLOR_FMT_RGB565,
        .num_fbs = config->num_fbs,
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
    };

    st7701_vendor_config_t vendor_config = {
        .init_cmds = st7701_is_display_2 ? tanmatsu_display_2_init_sequence : tanmatsu_display_init_sequence,
        .init_cmds_size = st7701_is_display_2 ? sizeof(tanmatsu_display_2_init_sequence) / sizeof(st7701_lcd_init_cmd_t)
                                              : sizeof(tanmatsu_display_init_sequence) / sizeof(st7701_lcd_init_cmd_t),
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
        .flags = {
            .use_mipi_interface = true,
        }};

    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = config->reset_pin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = config->use_24_bit_color ? 24 : 16,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(mipi_dbi_io, &lcd_dev_config, &mipi_dpi_panel), TAG,
                        "failed to install ST7701 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(mipi_dpi_panel), TAG, "failed to enable DMA2D for ST7701 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(mipi_dpi_panel), TAG, "failed to reset ST7701 panel");
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read back the VCOM value programmed into the panel's NVM/OTP before the init sequence below
    // overwrites the VCOMS register with the firmware default.
    //
    // This is also our only way to detect that no panel is attached: a DSI generic/DCS
    // read requires the peer to answer a bus-turnaround (BTA) request, and the DSI host
    // hardware has no "nobody answered" status bit. Upstream ESP-IDF busy-waits on the
    // read-complete flags with no timeout, which hangs the whole firmware forever if the
    // panel is missing. With CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD, the
    // vendored esp_hal_lcd component in this project bounds that wait and exposes
    // mipi_dsi_hal_host_gen_read_timed_out() so we can detect it here instead of hanging.
    // Without it, the normal esp_hal_lcd component is used and this detection is skipped,
    // so a missing panel will hang the read below instead.
    esp_err_t otp_res = st7701_enter_cmd2_bk1();
    if (otp_res == ESP_OK) {
        otp_res = esp_lcd_panel_io_rx_param(mipi_dbi_io, LCD_CMD_ST7701_VCOMS, &st7701_vcom_otp, 1);
    }
#if CONFIG_MIPI_DSI_ABSTRACTION_USE_VENDORED_ESP_HAL_LCD
    if (otp_res == ESP_OK && mipi_dsi_hal_host_gen_read_timed_out()) {
        ESP_LOGE(TAG, "No response from panel while reading VCOM OTP value, is a display connected?");
        esp_lcd_dpi_panel_disable_dma2d(mipi_dpi_panel);
        esp_lcd_panel_del(mipi_dpi_panel);
        mipi_dpi_panel = NULL;
        return ESP_ERR_NOT_FOUND;
    }
#endif
    if (otp_res == ESP_OK) {
        otp_res = st7701_exit_cmd2_bk1();
    }
    if (otp_res == ESP_OK) {
        st7701_vcom_otp_valid = true;
        ESP_LOGI(TAG, "VCOM value programmed into NVM/OTP: 0x%02X", st7701_vcom_otp);
    } else {
        ESP_LOGW(TAG, "Failed to read VCOM value from NVM/OTP: %s", esp_err_to_name(otp_res));
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(mipi_dpi_panel), TAG, "failed to initialize ST7701 panel");

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(mipi_dbi_io, LCD_CMD_COLMOD, (uint8_t[]){config->use_24_bit_color ? 0x77 : 0x55}, 1),
        TAG, "failed to set color mode");

    /*ESP_LOGI(TAG, "Register DPI panel event callback for flush ready notification");
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        //.on_color_trans_done = ...,
        //.on_refresh_done = ...,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(mipi_dpi_panel, &cbs, display), TAG, "failed to
    register flush event callback");*/

    return ESP_OK;
}
