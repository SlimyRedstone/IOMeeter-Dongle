/*
 * Two LED protocols behind one API. Only the model chosen in Kconfig is
 * compiled, so a board with one type carries no code for the other.
 */

#include "neopixel.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#ifdef CONFIG_NEOPIXEL_MODEL_APA102
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#else
#include "led_strip.h"
#endif

static const char *TAG = "neopixel";

static uint32_t s_count;
static uint32_t s_color;
static uint8_t  s_brightness = NEOPIXEL_DEFAULT_BRIGHTNESS;

/* ============================================================== WS2812 === */
#ifndef CONFIG_NEOPIXEL_MODEL_APA102

static led_strip_handle_t s_strip;

/* Rounds rather than truncates, so a dim setting keeps the hue of small
 * components instead of flooring them to zero. */
static inline uint8_t scale(uint8_t component)
{
    return (uint8_t)(((uint32_t)component * s_brightness + 127) / 255);
}

static esp_err_t driver_init(const neopixel_config_t *config)
{
    if (s_strip != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const led_strip_config_t strip_cfg = {
        .strip_gpio_num         = config->gpio,
        .max_leds               = config->count,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out       = false,
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,  /* 10 MHz */
        .mem_block_symbols = 64,
        .flags.with_dma    = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WS2812 on GPIO%d, %u LED(s)", config->gpio, (unsigned)config->count);
    return ESP_OK;
}

static bool driver_ready(void)
{
    return s_strip != NULL;
}

static esp_err_t driver_write(uint32_t rgb)
{
    /* led_strip takes plain r/g/b and handles the GRB wire order itself. */
    const uint8_t r = scale((rgb >> 16) & 0xFF);
    const uint8_t g = scale((rgb >> 8) & 0xFF);
    const uint8_t b = scale(rgb & 0xFF);

    for (uint32_t i = 0; i < s_count; i++) {
        esp_err_t err = led_strip_set_pixel(s_strip, i, r, g, b);
        if (err != ESP_OK) {
            return err;
        }
    }
    return led_strip_refresh(s_strip);
}

static esp_err_t driver_clear(void)
{
    return led_strip_clear(s_strip);
}

static esp_err_t driver_deinit(void)
{
    esp_err_t err = led_strip_del(s_strip);
    s_strip = NULL;
    return err;
}

/* ============================================================== APA102 === */
#else

/*
 * An APA102 frame is a 32-bit start field of zeros, one 32-bit field per LED,
 * and an end field long enough to clock the last one through the chain. Each
 * LED field is three current bits, a five-bit current level, then blue, green
 * and red -- the order the datasheet calls BGR, which is why the colours are
 * reversed on the way out.
 *
 * That five-bit field is a real current limit rather than a duty cycle, so
 * brightness lives there instead of being multiplied into the colour bytes:
 * dimming this way keeps all eight bits of each colour.
 */
#define APA102_START_BYTES 4
#define APA102_END_BYTES   4
#define APA102_LED_BYTES   4

static spi_device_handle_t s_spi;
static spi_host_device_t   s_host;

/*
 * Kconfig names the peripheral the way the schematic does, 2 or 3, but the
 * driver's enum counts from SPI1 at zero -- SPI2_HOST is 1 and SPI3_HOST is 2.
 * Handing the number straight to spi_bus_initialize() lands on SPI_HOST_MAX
 * and it refuses the bus, which is silent unless the return value is read.
 */
static spi_host_device_t spi_host_from_number(int number)
{
    return (number >= 3) ? SPI3_HOST : SPI2_HOST;
}
static uint8_t            *s_frame;      /* DMA-capable, built in place */
static size_t              s_frame_len;

/* 0 stays off; anything else keeps at least the lowest visible step. */
static inline uint8_t current_level(void)
{
    if (s_brightness == 0) {
        return 0;
    }
    uint8_t level = (uint8_t)(((uint32_t)s_brightness * 31 + 127) / 255);
    return level == 0 ? 1 : level;
}

static esp_err_t driver_init(const neopixel_config_t *config)
{
    if (s_spi != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_frame_len = APA102_START_BYTES + config->count * APA102_LED_BYTES + APA102_END_BYTES;
    s_frame = heap_caps_malloc(s_frame_len, MALLOC_CAP_DMA);
    if (s_frame == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_frame, 0x00, APA102_START_BYTES);
    memset(s_frame + s_frame_len - APA102_END_BYTES, 0xFF, APA102_END_BYTES);

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num     = config->gpio,
        .sclk_io_num     = config->clk_gpio,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = (int)s_frame_len,
    };

    s_host = spi_host_from_number(config->spi_host);
    esp_err_t err = spi_bus_initialize(s_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize(host %d): %s", config->spi_host, esp_err_to_name(err));
        goto fail;
    }

    /* No chip select: the LED listens to every clock edge on the bus, which is
       also why the bus cannot be shared with anything else. */
    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = config->spi_hz,
        .mode           = 0,
        .spics_io_num   = -1,
        .queue_size     = 1,
    };
    err = spi_bus_add_device(s_host, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        spi_bus_free(s_host);
        goto fail;
    }

    ESP_LOGI(TAG, "APA102 on GPIO%d/CLK%d, host %d, %u LED(s)",
             config->gpio, config->clk_gpio, config->spi_host, (unsigned)config->count);
    return ESP_OK;

fail:
    free(s_frame);
    s_frame = NULL;
    return err;
}

static bool driver_ready(void)
{
    return s_spi != NULL;
}

static esp_err_t driver_write(uint32_t rgb)
{
    const uint8_t header = (uint8_t)(0xE0 | current_level());
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8) & 0xFF;
    const uint8_t b = rgb & 0xFF;

    uint8_t *led = s_frame + APA102_START_BYTES;
    for (uint32_t i = 0; i < s_count; i++, led += APA102_LED_BYTES) {
        led[0] = header;
        led[1] = b;
        led[2] = g;
        led[3] = r;
    }

    spi_transaction_t trans = {
        .length    = s_frame_len * 8,
        .tx_buffer = s_frame,
    };
    return spi_device_transmit(s_spi, &trans);
}

static esp_err_t driver_clear(void)
{
    return driver_write(0);
}

static esp_err_t driver_deinit(void)
{
    esp_err_t err = spi_bus_remove_device(s_spi);
    s_spi = NULL;
    spi_bus_free(s_host);

    free(s_frame);
    s_frame = NULL;
    s_frame_len = 0;
    return err;
}

#endif /* CONFIG_NEOPIXEL_MODEL_APA102 */

/* ========================================================== public API === */

esp_err_t neopixel_init(const neopixel_config_t *config)
{
    if (config == NULL || config->count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->model != NEOPIXEL_KCONFIG_MODEL) {
        ESP_LOGE(TAG, "this build drives the other LED type; change it in menuconfig");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = driver_init(config);
    if (err != ESP_OK) {
        return err;
    }

    s_count = config->count;
    s_color = 0;
    return driver_clear();
}

esp_err_t neopixel_set_rgb(uint32_t rgb)
{
    if (!driver_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_color = rgb & 0xFFFFFF;
    return driver_write(s_color);
}

esp_err_t neopixel_set_brightness(uint8_t level)
{
    s_brightness = level;

    if (!driver_ready()) {
        return ESP_OK;
    }
    /* Re-send the current colour so the new level is visible immediately. */
    return neopixel_set_rgb(s_color);
}

uint8_t neopixel_get_brightness(void)
{
    return s_brightness;
}

uint32_t neopixel_get_rgb(void)
{
    return s_color;
}

esp_err_t neopixel_clear(void)
{
    if (!driver_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_color = 0;
    return driver_clear();
}

esp_err_t neopixel_deinit(void)
{
    if (!driver_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = driver_deinit();
    s_count = 0;
    return err;
}
