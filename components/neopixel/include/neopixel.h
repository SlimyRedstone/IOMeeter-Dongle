/*
 * Addressable status LED, in either of the two wire protocols the hardware
 * might use: WS2812 on one pin through RMT, or APA102 on two pins through SPI.
 * The type is a Kconfig choice, and only the selected one is compiled.
 *
 * Kept separate from usb_proto so the USB component carries no LED knowledge
 * and can be reused on boards that have no addressable LED.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEOPIXEL_MODEL_WS2812 = 0,  /*!< One wire, RMT encoded, GRB order  */
    NEOPIXEL_MODEL_APA102,      /*!< Data and clock, SPI, BGR order    */
} neopixel_model_t;

typedef struct {
    neopixel_model_t model;
    int      gpio;      /*!< Data line */
    int      clk_gpio;  /*!< Clock line; APA102 only, ignored otherwise */
    int      spi_host;  /*!< SPI host number; APA102 only               */
    int      spi_hz;    /*!< Bus clock; APA102 only                     */
    uint32_t count;     /*!< Number of LEDs on the strip                */
} neopixel_config_t;

#ifdef CONFIG_NEOPIXEL_MODEL_APA102
#define NEOPIXEL_KCONFIG_MODEL NEOPIXEL_MODEL_APA102
#define NEOPIXEL_KCONFIG_CLK   CONFIG_NEOPIXEL_CLK_GPIO
#define NEOPIXEL_KCONFIG_HOST  CONFIG_NEOPIXEL_SPI_HOST
#define NEOPIXEL_KCONFIG_HZ    CONFIG_NEOPIXEL_SPI_HZ
#else
#define NEOPIXEL_KCONFIG_MODEL NEOPIXEL_MODEL_WS2812
#define NEOPIXEL_KCONFIG_CLK   (-1)
#define NEOPIXEL_KCONFIG_HOST  (-1)
#define NEOPIXEL_KCONFIG_HZ    0
#endif

/** Everything from Kconfig, which is where the board's wiring belongs. */
#define NEOPIXEL_DEFAULT_CONFIG() ((neopixel_config_t){ \
    .model    = NEOPIXEL_KCONFIG_MODEL,                 \
    .gpio     = CONFIG_NEOPIXEL_DATA_GPIO,              \
    .clk_gpio = NEOPIXEL_KCONFIG_CLK,                   \
    .spi_host = NEOPIXEL_KCONFIG_HOST,                  \
    .spi_hz   = NEOPIXEL_KCONFIG_HZ,                    \
    .count    = CONFIG_NEOPIXEL_COUNT,                  \
})

/** Global brightness applied when nothing sets one. A WS2812 at full scale is
 *  painfully bright, so the default is a small fraction of it. */
#define NEOPIXEL_DEFAULT_BRIGHTNESS CONFIG_NEOPIXEL_BRIGHTNESS

/**
 * @brief Bring up the strip and clear it.
 *
 * Returns ESP_ERR_NOT_SUPPORTED if @p config asks for the model this build was
 * not compiled for; the type is a Kconfig choice, not a runtime one.
 */
esp_err_t neopixel_init(const neopixel_config_t *config);

/**
 * @brief Scale every colour by @p level / 255 before it reaches the strip.
 *
 * Global: it applies to every later neopixel_set_rgb(), whoever calls it, so
 * colours can be written at full scale and dimmed in one place. Takes effect
 * immediately by re-sending the current colour. Survives neopixel_deinit().
 *
 * On an APA102 this drives the chip's own 5-bit current register rather than
 * scaling the colour bytes, which keeps a dim colour from collapsing into a
 * handful of steps. The API is the same 0 - 255 either way.
 */
esp_err_t neopixel_set_brightness(uint8_t level);

/**
 * @brief Current global brightness, 0 - 255.
 */
uint8_t neopixel_get_brightness(void);

/**
 * @brief Set every LED to one packed 0xRRGGBB colour.
 *
 * The signature matches usb_proto_led_cb_t and now_connect_led_cb_t, so it can
 * be handed straight to either component's callback field.
 */
esp_err_t neopixel_set_rgb(uint32_t rgb);

/**
 * @brief Last colour passed to neopixel_set_rgb(), packed 0xRRGGBB.
 *
 * Reports what was requested, not what the strip physically shows: brightness
 * scaling is not applied to it, and a failed refresh still updates it.
 */
uint32_t neopixel_get_rgb(void);

/**
 * @brief Turn every LED off.
 */
esp_err_t neopixel_clear(void);

/**
 * @brief Release the RMT channel or the SPI bus.
 */
esp_err_t neopixel_deinit(void);

#ifdef __cplusplus
}
#endif
