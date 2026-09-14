/*
 * The 0.96" 160x80 ST7735 panel: SPI bus, controller, orientation, backlight.
 *
 * Panel only. It knows nothing about LVGL or about what is drawn -- it hands
 * back the esp_lcd handles and lets the caller decide. On this project that
 * caller is main/display_core.c, which owns LVGL and the UI.
 *
 * Wiring and geometry are Kconfig, under "Display (ST7735 + LVGL)". The
 * defaults are the T-Dongle S3 Plus: SCLK 5, MOSI 3, CS 4, DC 2, RST 1,
 * backlight 38, axes swapped and colour inverted, which is how it is fitted.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DISPLAY_ENABLED
#define DISPLAY_WIDTH        CONFIG_DISPLAY_H_RES
#define DISPLAY_HEIGHT       CONFIG_DISPLAY_V_RES
#define DISPLAY_BUFFER_LINES CONFIG_DISPLAY_LVGL_BUFFER_LINES
#else
#define DISPLAY_WIDTH        0
#define DISPLAY_HEIGHT       0
#define DISPLAY_BUFFER_LINES 0
#endif

/** Bytes one draw buffer needs at 16 bits per pixel. */
#define DISPLAY_BUFFER_BYTES (DISPLAY_WIDTH * DISPLAY_BUFFER_LINES * 2)

/**
 * @brief Bring up the bus, the controller and the backlight pin.
 *
 * Leaves the backlight off so that nothing shows until something has been
 * drawn. Returns ESP_ERR_NOT_SUPPORTED when the display is switched off in
 * Kconfig, which callers can treat as "no screen on this board".
 */
esp_err_t display_init(void);

/**
 * @brief Tear it all down again.
 */
esp_err_t display_deinit(void);

/**
 * @brief Turn the backlight on or off.
 */
esp_err_t display_backlight(bool on);

/**
 * @brief The panel, for drawing. NULL before display_init() succeeds.
 */
esp_lcd_panel_handle_t display_panel(void);

/**
 * @brief The panel's IO channel, which is where transfer-complete callbacks
 *        are registered. NULL before display_init() succeeds.
 */
esp_lcd_panel_io_handle_t display_io(void);

#ifdef __cplusplus
}
#endif
