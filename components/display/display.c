/*
 * Bring-up in the order the panel needs it: bus, IO, panel, orientation,
 * backlight, then LVGL on top. Nothing here draws.
 */

#include "display.h"

#include "esp_log.h"

#ifdef CONFIG_DISPLAY_ENABLED

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"

#include "esp_lcd_st7735.h"

static const char *TAG = "display";

/*
 * Kconfig names the peripheral the way the schematic does, 2 or 3, but the
 * driver's enum counts from SPI1 at zero -- SPI2_HOST is 1 and SPI3_HOST is 2.
 * Passing the number through unmapped quietly picks the wrong peripheral, or
 * SPI_HOST_MAX if it is 3.
 */
#define SPI_HOST_NUM (CONFIG_DISPLAY_SPI_HOST >= 3 ? SPI3_HOST : SPI2_HOST)

/* Kconfig bools are defined-or-absent, not 1-or-0, so they cannot be passed
   to a function as they stand. */
#ifdef CONFIG_DISPLAY_SWAP_XY
#define PANEL_SWAP_XY true
#else
#define PANEL_SWAP_XY false
#endif
#ifdef CONFIG_DISPLAY_MIRROR_X
#define PANEL_MIRROR_X true
#else
#define PANEL_MIRROR_X false
#endif
#ifdef CONFIG_DISPLAY_MIRROR_Y
#define PANEL_MIRROR_Y true
#else
#define PANEL_MIRROR_Y false
#endif
#ifdef CONFIG_DISPLAY_INVERT_COLOR
#define PANEL_INVERT true
#else
#define PANEL_INVERT false
#endif

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t    s_panel;

esp_err_t display_backlight(bool on)
{
#if CONFIG_DISPLAY_PIN_BACKLIGHT >= 0
#ifdef CONFIG_DISPLAY_BACKLIGHT_ACTIVE_HIGH
    const int level = on ? 1 : 0;
#else
    const int level = on ? 0 : 1;
#endif
    return gpio_set_level(CONFIG_DISPLAY_PIN_BACKLIGHT, level);
#else
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t backlight_init(void)
{
#if CONFIG_DISPLAY_PIN_BACKLIGHT >= 0
    const gpio_config_t io = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_DISPLAY_PIN_BACKLIGHT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    /* An output register left at its reset value is 0, which on an active-low
       line is "on" -- the panel would show noise before anything is drawn. */
    return display_backlight(false);
#else
    return ESP_OK;
#endif
}

/*
 * The panel's own orientation, applied once here rather than through LVGL's
 * rotation: doing it in the controller costs nothing per frame, and it means
 * the gap below is expressed in the same coordinates LVGL draws in.
 */
static esp_err_t panel_orient(void)
{
    esp_err_t err = esp_lcd_panel_swap_xy(s_panel, PANEL_SWAP_XY);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_mirror(s_panel, PANEL_MIRROR_X, PANEL_MIRROR_Y);
    if (err != ESP_OK) {
        return err;
    }

    /* The visible window sits inside a larger controller memory. */
    err = esp_lcd_panel_set_gap(s_panel, CONFIG_DISPLAY_X_GAP, CONFIG_DISPLAY_Y_GAP);
    if (err != ESP_OK) {
        return err;
    }

    return esp_lcd_panel_invert_color(s_panel, PANEL_INVERT);
}

static esp_err_t panel_start(void)
{
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num     = CONFIG_DISPLAY_PIN_SCLK,
        .mosi_io_num     = CONFIG_DISPLAY_PIN_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISPLAY_BUFFER_BYTES,
    };

    esp_err_t err = spi_bus_initialize(SPI_HOST_NUM, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize(host %d): %s",
                 CONFIG_DISPLAY_SPI_HOST, esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = CONFIG_DISPLAY_PIN_CS,
        .dc_gpio_num       = CONFIG_DISPLAY_PIN_DC,
        .spi_mode          = 0,
        .pclk_hz           = CONFIG_DISPLAY_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_NUM, &io_cfg, &s_io);
    if (err != ESP_OK) {
        goto fail_bus;
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_DISPLAY_PIN_RST,
#ifdef CONFIG_DISPLAY_BGR_ORDER
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7735(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        goto fail_io;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        goto fail_panel;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        goto fail_panel;
    }
    err = panel_orient();
    if (err != ESP_OK) {
        goto fail_panel;
    }
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        goto fail_panel;
    }

    return ESP_OK;

fail_panel:
    esp_lcd_panel_del(s_panel);
    s_panel = NULL;
fail_io:
    esp_lcd_panel_io_del(s_io);
    s_io = NULL;
fail_bus:
    spi_bus_free(SPI_HOST_NUM);
    return err;
}

esp_err_t display_init(void)
{
    if (s_panel != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = backlight_init();
    if (err != ESP_OK) {
        return err;
    }

    err = panel_start();
    if (err != ESP_OK) {
        return err;
    }

    /* Backlight stays off: whoever draws turns it on, so the first thing seen
       is a finished frame rather than whatever the panel powered up holding. */
    ESP_LOGI(TAG, "ST7735 %dx%d on host %d",
             CONFIG_DISPLAY_H_RES, CONFIG_DISPLAY_V_RES, CONFIG_DISPLAY_SPI_HOST);
    return ESP_OK;
}

esp_err_t display_deinit(void)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    display_backlight(false);

    esp_lcd_panel_del(s_panel);
    s_panel = NULL;
    esp_lcd_panel_io_del(s_io);
    s_io = NULL;

    return spi_bus_free(SPI_HOST_NUM);
}

esp_lcd_panel_handle_t display_panel(void)
{
    return s_panel;
}

esp_lcd_panel_io_handle_t display_io(void)
{
    return s_io;
}

/* ============================================== display switched off ===== */
#else

esp_err_t display_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t display_deinit(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t display_backlight(bool on)
{
    (void)on;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_lcd_panel_handle_t display_panel(void)
{
    return NULL;
}

esp_lcd_panel_io_handle_t display_io(void)
{
    return NULL;
}

#endif /* CONFIG_DISPLAY_ENABLED */
