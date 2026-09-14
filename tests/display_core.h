#include <stdio.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"


#include "ui/actions.h"
#include "ui/fonts.h"    
#include "ui/images.h"
#include "ui/screens.h"
#include "ui/structs.h"
#include "ui/styles.h"
#include "ui/ui.h"
#include "ui/vars.h"


#define EXAMPLE_LVGL_TICK_PERIOD_MS 1
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 50
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 10
#define EXAMPLE_LVGL_TASK_PRIORITY 0

#define AMOLED_BRIGHTNESS_MAX 0xFF
#define AMOLED_BRIGHTNESS_MIN 0x0F


static const char *DISPLAY_TAG  = "LCD";
static SemaphoreHandle_t lvgl_mux = NULL;
static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;      // contains callback functions

void custom_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
void increase_lvgl_tick(void *arg);

bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);

void display_task(void * args);


