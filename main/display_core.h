/*
 * Everything the screen needs at run time: the panel, LVGL, its tick, and the
 * EEZ Studio UI under main/ui.
 *
 * display_task() is the whole of it. Start it with xTaskCreatePinnedToCore()
 * and it brings the panel up, registers LVGL against it, loads the UI and then
 * runs the LVGL loop forever. Nothing is drawn anywhere else.
 *
 * LVGL is not thread safe, so anything touching an lv_* object from another
 * task has to hold the lock:
 *
 *     if (lvgl_lock(100)) {
 *         lv_label_set_text(objects.some_label, "hello");
 *         lvgl_unlock();
 *     }
 */

#pragma once

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pinned to core 1 so the LVGL loop does not fight USB and Wi-Fi, both of
   which live on core 0 by default. */
#define DISPLAY_TASK_STACK 6144
#define DISPLAY_TASK_PRIO  3
#define DISPLAY_TASK_CORE  1

/**
 * @brief The display's own task. Never returns.
 */
void display_task(void *args);

/**
 * @brief Take the LVGL lock. @p timeout_ms of -1 waits forever.
 *
 * @return false if the timeout passed first.
 */
bool lvgl_lock(int timeout_ms);

/**
 * @brief Release the LVGL lock.
 */
void lvgl_unlock(void);

#ifdef __cplusplus
}
#endif
