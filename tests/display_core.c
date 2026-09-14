#include "display_core.h"

#include "esp_mac.h"




/* int32_t get_var_bar_value() {
    return lv_bar_get_value(objects.bar);
}
void set_var_bar_value(int32_t value) {
    int32_t values[2] = {
        lv_bar_get_min_value(objects.bar),
        lv_bar_get_max_value(objects.bar)
    };
    if (value < values[0]) value = values[0];
    if (value > values[1]) value = values[1];
    
    lv_bar_set_value(objects.bar, value, LV_ANIM_ON);
}
const char *get_var_text_value() {
    return lv_label_get_text(objects.bar_text_value);
}
void set_var_text_value(const char *value) {
    if (value == NULL) {
        value = "";
    }
    const char *current_value = lv_label_get_text(objects.bar_text_value);
    if (strcmp(value, current_value) != 0) {
        lv_label_set_text(objects.bar_text_value, value);
    }
} */


void set_bar_text_value(uint8_t value) {
    char text[16];
    // snprintf(text, sizeof(text), "%li", value);
	snprintf(text, sizeof(text), "%u/255", value);
    // set_var_text_value(text);
    // set_var_bar_value(value);
}

bool lvgl_lock(int timeout_ms) {
	const TickType_t timeout_ticks
			= (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	return xQueueTakeMutexRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_unlock(void) { xQueueGiveMutexRecursive(lvgl_mux); }

void increase_lvgl_tick(void *arg) {
	/* Tell LVGL how many milliseconds has elapsed */
	lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void custom_lvgl_flush_cb( lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
#if DISPLAY_FULLRESH
	uint32_t w = (area->x2 - area->x1 + 1);
	uint32_t h = (area->y2 - area->y1 + 1);
	display_push_colors(area->x1, area->y1, w, h, (uint16_t *)color_map);
	lv_disp_flush_ready(drv);
#else
	int offsetx1 = area->x1;
	int offsetx2 = area->x2;
	int offsety1 = area->y1;
	int offsety2 = area->y2;
	display_push_colors(
			offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, (uint16_t *)color_map);
#endif
}

void display_task(void *args) {
	gpio_reset_pin(BOARD_BUTTON1_PIN);
	gpio_set_direction(BOARD_BUTTON1_PIN, GPIO_MODE_INPUT);

	gpio_reset_pin(BOARD_BUTTON2_PIN);
	gpio_set_direction(BOARD_BUTTON2_PIN, GPIO_MODE_INPUT);

	gpio_glitch_filter_handle_t gpio_filter_handler;
	gpio_pin_glitch_filter_config_t gpio_glitch_cfg = {
		.clk_src	= SOC_MOD_CLK_APB,
		.gpio_num = (gpio_num_t)0,
	};

	if (gpio_new_pin_glitch_filter(&gpio_glitch_cfg, &gpio_filter_handler)
			== ESP_OK) {
		gpio_glitch_filter_enable(gpio_filter_handler);
	}
	#ifdef BOARD_HAS_TOUCH
		touch_begin(&fingers);
	#endif

	display_init();

	lv_init();

#if CONFIG_SPIRAM
	lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
			DISPLAY_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
	assert(buf1);

	lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
			DISPLAY_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
	assert(buf2);
	lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISPLAY_BUFFER_SIZE);
#else
	lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
			AMOLED_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
	assert(buf1);
	lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
			AMOLED_HEIGHT * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
	assert(buf2);
	lv_disp_draw_buf_init(&disp_buf, buf1, buf2, AMOLED_HEIGHT * 20);
#endif

	ESP_LOGI(DISPLAY_TAG, "Register display driver to LVGL");
	lv_disp_drv_init(&disp_drv);
	disp_drv.hor_res			= AMOLED_HEIGHT;
	disp_drv.ver_res			= AMOLED_WIDTH;
	disp_drv.flush_cb			= custom_lvgl_flush_cb;
	disp_drv.draw_buf			= &disp_buf;
	disp_drv.full_refresh = false;
	lv_disp_drv_register(&disp_drv);

#if BOARD_HAS_TOUCH

	static lv_indev_drv_t indev_drv;	// Input device driver (Touch)
	lv_indev_drv_init(&indev_drv);
	indev_drv.type		= LV_INDEV_TYPE_POINTER;
	indev_drv.disp		= NULL;
	indev_drv.read_cb = lvgl_touch_cb;
	lv_indev_drv_register(&indev_drv);
#endif


	ESP_LOGI(DISPLAY_TAG, "Install LVGL tick timer");
	const esp_timer_create_args_t lvgl_tick_timer_args
			= { .callback							 = &increase_lvgl_tick,
					.arg									 = NULL,
					.dispatch_method			 = ESP_TIMER_TASK,
					.name									 = "lvgl_tick",
					.skip_unhandled_events = true, };
	esp_timer_handle_t lvgl_tick_timer = NULL;
	ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(
			lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));
	// ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 500));

	lvgl_mux = xSemaphoreCreateRecursiveMutex();
	assert(lvgl_mux);

	init_external_ui(&ui_vars);

	ui_init();

	/* if (lvgl_lock(-1)) {
			lv_demo_benchmark();
			lvgl_unlock();
	} */
	amoled_set_brightness(AMOLED_BRIGHTNESS_MAX);	 // Set brightness to maximum
	// set_bar_text_value(AMOLED_BRIGHTNESS_MAX);
    lv_disp_t *current_display = lv_disp_get_default();
    lv_disp_set_rotation(current_display, LV_DISP_ROT_180);
	
	uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;

	while (1) {
		// Lock the mutex due to the LVGL APIs are not thread-safe
		if (lvgl_lock(10)) {
			task_delay_ms = lv_timer_handler();


			ui_tick();
			// tick_screen_main();
			// Release the mutex
			lvgl_unlock();
		}
		if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
			task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
		} else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
			task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
		}
		vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
	}

	gpio_glitch_filter_disable(gpio_filter_handler);
	gpio_del_glitch_filter(gpio_filter_handler);

	vTaskDelete(NULL);
}
