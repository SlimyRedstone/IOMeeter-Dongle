#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *agc_gain_label;
    lv_obj_t *agc_level_label;
    lv_obj_t *agc_level_slider;
    lv_obj_t *agc_level_text_label;
    lv_obj_t *agc_max_gain_slider;
    lv_obj_t *agc_max_gain_text_label;
    lv_obj_t *agc_tab;
    lv_obj_t *agc_title;
    lv_obj_t *brightness_selector;
    lv_obj_t *dre_gain_label;
    lv_obj_t *dre_level_label;
    lv_obj_t *dre_level_slider;
    lv_obj_t *dre_level_text_label;
    lv_obj_t *dre_max_gain_slider;
    lv_obj_t *dre_max_gain_text_label;
    lv_obj_t *dre_tab;
    lv_obj_t *dre_title;
    lv_obj_t *gain_label;
    lv_obj_t *gain_slider;
    lv_obj_t *gain_tab;
    lv_obj_t *hue_selector;
    lv_obj_t *led;
    lv_obj_t *led_indicator;
    lv_obj_t *tabs;
    lv_obj_t *volume_label;
    lv_obj_t *volume_slider;
    lv_obj_t *volume_tab;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
};

void create_screen_main();
void tick_screen_main();

void create_screens();
void tick_screen(int screen_index);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/