# MIPI DSI abstraction

[![Component Registry](https://components.espressif.com/components/nicolaielectronics/mipi_dsi_abstraction/badge.svg)](https://components.espressif.com/components/nicolaielectronics/mipi_dsi_abstraction)

Abstraction layer for quickly experimenting with MIPI DSI displays.

| LCD controller | Component name                    | Note                                                                                            |
| :------------: | :-------------------------------: | :---------------------------------------------------------------------------------------------: |
|     EK79007    | esp_lcd_ek79007                   | Included in the ESP32-P4-Function-EV-Board with v0.1 chip kit                                   |
|     ILI9981C   | esp_lcd_ili9981c                  | Included in the ESP32-P4 devkit with v0.0 chip                                                  |
|     ST7701(S)  | esp_lcd_st7701                    | Configured for use with the display on [Tanmatsu](https://nicolaielectronics.nl/docs/tanmatsu/) |
|     LT8912B    | esp_lcd_lt8912b                   | MIPI DSI to HDMI adapter chip                                                                   |

## Add to project

Packages from this repository are uploaded to [Espressif's component service](https://components.espressif.com/).
You can add them to your project via `idf.py add-dependancy`, e.g.

```
    idf.py add-dependency "nicolaielectronics/mipi-dsi-abstraction"
```

Alternatively, you can create `idf_component.yml`. More is in [Espressif's documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-component-manager.html).

## Note

This component was made to facilitate easier testing for our firmwares on both the devkits and our target hardware and thus
the configurations we use are the only supported display configurations.

## ST7701 Color Format Configuration (Tanmatsu)

The ST7701 driver for Tanmatsu devices **defaults to RGB888 (24-bit color)** mode, providing full 16.7 million colors with 8-bit precision per RGB channel.

### Switching to RGB565 Mode

If you need to use RGB565 (16-bit color) mode for bandwidth savings or compatibility, you must configure it **before** the display is initialized.

**Important:** On Tanmatsu devices, `st7701_initialize()` is called internally by `bsp_device_initialize()`. You must call `st7701_set_color_format()` **before** `bsp_device_initialize()` to ensure the display controller is configured correctly from the start.

#### Setup

First, add the component dependency to your `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES
        badge-bsp
        esp32-component-mipi-dsi-abstraction  # Add this line
    INCLUDE_DIRS "."
)
```

#### Usage Example:

```c
#include "esp_lcd_mipi_dsi.h"
#include "bsp/device.h"

// Declare the ST7701 color format function
extern esp_err_t st7701_set_color_format(lcd_color_rgb_pixel_format_t format);

void app_main(void) {
    // Configure for RGB565 mode BEFORE BSP initialization
    st7701_set_color_format(LCD_COLOR_PIXEL_FORMAT_RGB565);

    // Initialize BSP (this calls st7701_initialize() internally)
    bsp_device_initialize();

    // Query the configured format
    size_t h_res, v_res;
    lcd_color_rgb_pixel_format_t color_fmt;
    st7701_get_parameters(&h_res, &v_res, &color_fmt);
    // color_fmt will be LCD_COLOR_PIXEL_FORMAT_RGB565
}
```

**Notes:**
- The `extern` declaration is necessary because `st7701_set_color_format()` is not exposed through the BSP headers.
- Adding `esp32-component-mipi-dsi-abstraction` to `PRIV_REQUIRES` is required for the linker to find the function.

### Color Format Comparison

| Format | Bits/Pixel | Colors | Memory (480×800) | Bandwidth |
|--------|-----------|--------|-----------------|-----------|
| RGB888 | 24 | 16.7M | 1,152,000 bytes | Higher |
| RGB565 | 16 | 65,536 | 768,000 bytes | Lower |

**Default:** RGB888 (24-bit) for maximum color quality.

## Example

```
/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
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
#include "dsi_panel_espressif_ek79007.h"
#include "dsi_panel_espressif_ili9881c.h"
#include "dsi_panel_nicolaielectronics_st7701.h"

static const char *TAG = "example";

#define PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define PANEL_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL           1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL          !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT                26
#define EXAMPLE_PIN_NUM_LCD_RST                 27

void example_bsp_enable_dsi_phy_power(void) {
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = PANEL_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}


static void example_bsp_init_lcd_backlight(void)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif
}

static void example_bsp_set_lcd_backlight(uint32_t level)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level);
#endif
}

typedef enum _display_type {
    DISPLAY_TYPE_EK79007 = 0,
    DISPLAY_TYPE_ILI9881C,
    DISPLAY_TYPE_ST7701,
} display_type_t;

void app_main(void)
{
    example_bsp_enable_dsi_phy_power();
    example_bsp_init_lcd_backlight();
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    size_t h_res = 0;
    size_t v_res = 0;
    lcd_color_rgb_pixel_format_t color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;

    display_type_t display_type = DISPLAY_TYPE_ST7701;

    switch (display_type) {
        case DISPLAY_TYPE_EK79007:
            ek79007_initialize(EXAMPLE_PIN_NUM_LCD_RST);
            mipi_dpi_panel = ek79007_get_panel();
            ek79007_get_parameters(&h_res, &v_res, &color_fmt);
            break;
        case DISPLAY_TYPE_ILI9881C:
            ili9881c_initialize(EXAMPLE_PIN_NUM_LCD_RST);
            mipi_dpi_panel = ili9881c_get_panel();
            ili9881c_get_parameters(&h_res, &v_res, &color_fmt);
            break;
        case DISPLAY_TYPE_ST7701:
            st7701_initialize(EXAMPLE_PIN_NUM_LCD_RST);
            mipi_dpi_panel = st7701_get_panel();
            st7701_get_parameters(&h_res, &v_res, &color_fmt);
            break;
        default:
            ESP_LOGE(TAG, "Invalid display type");
            return;
    }

    uint8_t bytes_per_pixel = 0;
    switch (color_fmt) {
        case LCD_COLOR_PIXEL_FORMAT_RGB565:
            bytes_per_pixel = 2;
            break;
        case LCD_COLOR_PIXEL_FORMAT_RGB666:
            bytes_per_pixel = 3;
            break;
        case LCD_COLOR_PIXEL_FORMAT_RGB888:
            bytes_per_pixel = 3;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported color format");
            return;
    }


    size_t draw_buffer_sz = h_res * v_res * bytes_per_pixel;
    uint8_t* buf1 = (uint8_t*) heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(buf1);

    memset(buf1, 0xFF, draw_buffer_sz);

    if (bytes_per_pixel == 2) {
        for (size_t i = 0; i < draw_buffer_sz; i+=bytes_per_pixel) {
            buf1[i+0] = 0x07;
            buf1[i+1] = 0xe0;
        }
    } else if (bytes_per_pixel == 3) {
        for (size_t i = 0; i < draw_buffer_sz; i+=bytes_per_pixel) {
            buf1[i+0] = 0;
            buf1[i+1] = 0;
            buf1[i+2] = 0xff;
        }
    }

    esp_lcd_panel_draw_bitmap(mipi_dpi_panel, 0, 0, h_res - 1, v_res - 1, buf1);

    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
}
```
