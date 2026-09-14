/*
 * ST7735 panel driver for esp_lcd.
 *
 * ESP-IDF ships ST7789 but not ST7735, and the registry drivers are written
 * against the 5.x panel config, whose colour fields v6 removed. This is the
 * same shape as the built-in ST7789 driver, with the ST7735's own power and
 * gamma sequence and its 16-bit COLMOD value.
 *
 * Internal to the display component; nothing outside it needs the panel type.
 */

#pragma once

#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a panel handle for an ST7735 on an already-created IO handle.
 *
 * Honours rgb_ele_order, bits_per_pixel (16 or 18) and reset_gpio_num from
 * @p panel_dev_config. The caller still has to reset, init and set the gap,
 * mirror and inversion the particular module needs.
 */
esp_err_t esp_lcd_new_panel_st7735(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
