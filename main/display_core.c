#include "display_core.h"

#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "display.h"

#include "ui/ui.h"

static const char *TAG = "display_core";

#define LVGL_TICK_PERIOD_MS  1
#define LVGL_TASK_MAX_DELAY_MS 50
#define LVGL_TASK_MIN_DELAY_MS 5

static SemaphoreHandle_t s_lvgl_mux;
static lv_display_t     *s_disp;

/*
 * The variables the generated UI reads. tick_screen_main() polls all three on
 * every LVGL tick and only touches a widget when the value has changed, so the
 * text getter has to hand back the same buffer each time -- it is compared
 * against what the label already shows, not consumed.
 *
 * The tick reads these with the LVGL lock held. The setters are called from
 * the link task instead, so they take the same lock; being recursive, it also
 * allows a setter called from inside an LVGL callback. Before display_task()
 * has created the mutex there is no reader to race with, and the write goes
 * ahead regardless.
 */
static bool is_device_connected = false;
static char device_infos[256] = {0};

const char *get_var_device_status_text() {
    return device_infos;
}
void set_var_device_status_text(const char *value) {
    const bool locked = lvgl_lock(100);

    if (value == NULL) {
        device_infos[0] = '\0';
    } else {
        snprintf(device_infos, sizeof(device_infos), "%s", value);
    }

    if (locked) {
        lvgl_unlock();
    }
}
bool get_var_device_is_connected() {
    return is_device_connected;
}
void set_var_device_is_connected(bool value) {
    is_device_connected = value;
}
bool get_var_device_is_disconnected() {
    return !is_device_connected;
}
void set_var_device_is_disconnected(bool value) {
    is_device_connected = !value;
}

bool lvgl_lock(int timeout_ms)
{
    if (s_lvgl_mux == NULL) {
        return false;
    }
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mux, ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mux);
}

/* LVGL counts in milliseconds and has no clock of its own. */
static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/*
 * Runs from the SPI interrupt once the last transfer of a flush has left.
 * Telling LVGL here rather than at the end of the flush is what lets it render
 * the next area while this one is still going out.
 */
static bool color_trans_done(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

/*
 * LVGL builds RGB565 in the CPU's byte order and esp_lcd puts bytes on the
 * wire as they are, so the pair has to be turned round before it goes. The
 * area is inclusive on both edges; esp_lcd wants the far edge exclusive.
 */
static void lvgl_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);

    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    lv_draw_sw_rgb565_swap(px_map, (uint32_t)(w * h));
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    /* lv_display_flush_ready() comes from color_trans_done(). */
}

/*
 * Brings LVGL up against the panel the display component already started:
 * two draw buffers in DMA-capable memory, the flush callback, the tick, then
 * the generated UI.
 */
static esp_err_t lvgl_start(void)
{
    lv_init();

    void *buf1 = heap_caps_malloc(DISPLAY_BUFFER_BYTES, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(DISPLAY_BUFFER_BYTES, MALLOC_CAP_DMA);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "no DMA memory for two %d byte draw buffers", DISPLAY_BUFFER_BYTES);
        free(buf1);
        free(buf2);
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (s_disp == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_user_data(s_disp, display_panel());
    lv_display_set_flush_cb(s_disp, lvgl_flush);
    lv_display_set_buffers(s_disp, buf1, buf2, DISPLAY_BUFFER_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = color_trans_done,
    };
    esp_err_t err = esp_lcd_panel_io_register_event_callbacks(display_io(), &io_cbs, s_disp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_event_callbacks: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t tick_args = {
        .callback              = lvgl_tick,
        .name                  = "lvgl_tick",
        .dispatch_method       = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t tick_timer = NULL;
    err = esp_timer_create(&tick_args, &tick_timer);
    if (err != ESP_OK) {
        return err;
    }
    return esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000);
}

void display_task(void *args)
{
    (void)args;

    esp_err_t err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display_init: %s -- no screen on this build",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mux == NULL) {
        ESP_LOGE(TAG, "cannot create the LVGL mutex");
        vTaskDelete(NULL);
        return;
    }

    err = lvgl_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* The UI generated by EEZ Studio into main/ui. */
    ui_init();

    /* First frame before the backlight, so nothing shows half-drawn. */
    if (lvgl_lock(-1)) {
        lv_timer_handler();
        lvgl_unlock();
    }
    display_backlight(true);

    ESP_LOGI(TAG, "running on core %d", esp_cpu_get_core_id());

    while (1) {
        uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;

        if (lvgl_lock(10)) {
            delay_ms = lv_timer_handler();
            ui_tick();
            lvgl_unlock();
        }

        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
