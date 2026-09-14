/*
 * IOMeeter firmware entry point.
 *
 * Three things in order and nothing else: the configuration the rest reads,
 * the screen on its own task, and the link that carries device state off the
 * board. The work is in display_core.c and link_core.c.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "config.h"
#include "display_core.h"
#include "link_core.h"

void app_main(void)
{
    /* First: the other two read their colours and their pairing out of it. Not
       fatal if it fails, since every getter falls back to a built-in default. */
    esp_err_t err = config_init();
    if (err != ESP_OK) {
        ESP_LOGE("main", "config_init: %s -- running on defaults", esp_err_to_name(err));
    }

    xTaskCreatePinnedToCore(display_task, "display", DISPLAY_TASK_STACK, NULL,
                            DISPLAY_TASK_PRIO, NULL, DISPLAY_TASK_CORE);

    link_start();
}
