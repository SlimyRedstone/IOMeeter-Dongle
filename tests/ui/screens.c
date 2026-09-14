#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;

static void event_handler_cb_main_brightness_selector(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_brightness_value(value);
        }
    }
}

static void event_handler_cb_main_hue_selector(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_arc_get_value(ta);
            set_var_hue_value(value);
        }
    }
}

static void event_handler_cb_main_led_indicator(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
}

static void event_handler_cb_main_volume_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_volume_value(value);
        }
    }
}

static void event_handler_cb_main_gain_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_gain_value(value);
        }
    }
}

static void event_handler_cb_main_dre_level_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_dre_level(value);
        }
    }
}

static void event_handler_cb_main_dre_max_gain_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_dre_gain(value);
        }
    }
}

static void event_handler_cb_main_agc_level_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_agc_level_value(value);
        }
    }
}

static void event_handler_cb_main_agc_max_gain_slider(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_slider_get_value(ta);
            set_var_agc_gain_value(value);
        }
    }
}

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 536, 240);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0e0e0e), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // Tabs
            lv_obj_t *obj = lv_tabview_create(parent_obj, LV_DIR_TOP, 42);
            objects.tabs = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 536, 240);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff0e0e0e), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // LED
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "LED");
                    objects.led = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // Brightness_selector
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.brightness_selector = obj;
                            lv_obj_set_pos(obj, 1, 78);
                            lv_obj_set_size(obj, 305, 21);
                            lv_slider_set_range(obj, 0, 255);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_brightness_selector, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                        }
                        {
                            // Hue_selector
                            lv_obj_t *obj = lv_arc_create(parent_obj);
                            objects.hue_selector = obj;
                            lv_obj_set_pos(obj, 339, 14);
                            lv_obj_set_size(obj, 150, 150);
                            lv_arc_set_range(obj, 0, 360);
                            lv_arc_set_bg_start_angle(obj, 90);
                            lv_arc_set_bg_end_angle(obj, 89);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_hue_selector, LV_EVENT_ALL, 0);
                        }
                        {
                            // LED_indicator
                            lv_obj_t *obj = lv_led_create(parent_obj);
                            objects.led_indicator = obj;
                            lv_obj_set_pos(obj, 392, 67);
                            lv_obj_set_size(obj, 45, 45);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_led_indicator, LV_EVENT_ALL, 0);
                            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
                        }
                    }
                }
                {
                    // VolumeTab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Volume");
                    objects.volume_tab = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // VolumeSlider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.volume_slider = obj;
                            lv_obj_set_pos(obj, 3, 42);
                            lv_obj_set_size(obj, 486, 30);
                            lv_slider_set_range(obj, -100, 27);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_volume_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                        }
                        {
                            // VolumeLabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.volume_label = obj;
                            lv_obj_set_pos(obj, 234, 114);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_38, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
                {
                    // GainTab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "Gain");
                    objects.gain_tab = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // GainSlider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.gain_slider = obj;
                            lv_obj_set_pos(obj, 3, 42);
                            lv_obj_set_size(obj, 486, 30);
                            lv_slider_set_range(obj, -11, 42);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_gain_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                        }
                        {
                            // GainLabel
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.gain_label = obj;
                            lv_obj_set_pos(obj, 219, 104);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_38, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
                {
                    // DRE_Tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "DRE");
                    objects.dre_tab = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // DRE_Title
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.dre_title = obj;
                            lv_obj_set_pos(obj, 77, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Dynamic Range Enhancer");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                        {
                            // DRE_Level_Text_Label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.dre_level_text_label = obj;
                            lv_obj_set_pos(obj, -8, 42);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Level");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                        {
                            // DRE_Level_Slider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.dre_level_slider = obj;
                            lv_obj_set_pos(obj, 87, 42);
                            lv_obj_set_size(obj, 400, 30);
                            lv_slider_set_range(obj, -66, -12);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_dre_level_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // DRE_Level_Label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.dre_level_label = obj;
                                    lv_obj_set_pos(obj, 173, 5);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // DRE_MaxGain_Slider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.dre_max_gain_slider = obj;
                            lv_obj_set_pos(obj, 87, 109);
                            lv_obj_set_size(obj, 400, 30);
                            lv_slider_set_range(obj, 2, 26);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_dre_max_gain_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // DRE_Gain_Label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.dre_gain_label = obj;
                                    lv_obj_set_pos(obj, 178, 5);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // DRE_MaxGain_Text_Label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.dre_max_gain_text_label = obj;
                            lv_obj_set_pos(obj, -4, 110);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Gain");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
                {
                    // AGC_Tab
                    lv_obj_t *obj = lv_tabview_add_tab(parent_obj, "AGC");
                    objects.agc_tab = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // AGC_Title
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.agc_title = obj;
                            lv_obj_set_pos(obj, 95, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Automatic Gain Control");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                        {
                            // AGC_Level_Text_Label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.agc_level_text_label = obj;
                            lv_obj_set_pos(obj, -8, 42);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Level");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                        {
                            // AGC_Level_Slider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.agc_level_slider = obj;
                            lv_obj_set_pos(obj, 87, 42);
                            lv_obj_set_size(obj, 400, 30);
                            lv_slider_set_range(obj, -36, -6);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_agc_level_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // AGC_Level_Label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.agc_level_label = obj;
                                    lv_obj_set_pos(obj, 173, 5);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // AGC_MaxGain_Slider
                            lv_obj_t *obj = lv_slider_create(parent_obj);
                            objects.agc_max_gain_slider = obj;
                            lv_obj_set_pos(obj, 87, 109);
                            lv_obj_set_size(obj, 400, 30);
                            lv_slider_set_range(obj, 3, 42);
                            lv_obj_add_event_cb(obj, event_handler_cb_main_agc_max_gain_slider, LV_EVENT_ALL, 0);
                            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                            lv_obj_set_style_radius(obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                            lv_obj_set_style_radius(obj, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
                            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff5c00a7), LV_PART_KNOB | LV_STATE_DEFAULT);
                            {
                                lv_obj_t *parent_obj = obj;
                                {
                                    // AGC_Gain_Label
                                    lv_obj_t *obj = lv_label_create(parent_obj);
                                    objects.agc_gain_label = obj;
                                    lv_obj_set_pos(obj, 176, 5);
                                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                                    lv_label_set_text(obj, "");
                                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                            }
                        }
                        {
                            // AGC_MaxGain_Text_Label
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.agc_max_gain_text_label = obj;
                            lv_obj_set_pos(obj, -4, 110);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_label_set_text(obj, "Gain");
                            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                        }
                    }
                }
            }
        }
    }
}

void tick_screen_main() {
    {
        int32_t new_val = get_var_brightness_value();
        int32_t cur_val = lv_slider_get_value(objects.brightness_selector);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.brightness_selector;
            lv_slider_set_value(objects.brightness_selector, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_hue_value();
        int32_t cur_val = lv_arc_get_value(objects.hue_selector);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.hue_selector;
            lv_arc_set_value(objects.hue_selector, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_rgb_value();
        uint32_t cur_val = lv_color_to32(((lv_led_t *)objects.led_indicator)->color);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.led_indicator;
            lv_led_set_color(objects.led_indicator, lv_color_hex(new_val));
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_brightness_value();
        if (new_val < 0) new_val = 0;
        else if (new_val > 255) new_val = 255;
        int32_t cur_val = lv_led_get_brightness(objects.led_indicator);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.led_indicator;
            lv_led_set_brightness(objects.led_indicator, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_volume_value();
        int32_t cur_val = lv_slider_get_value(objects.volume_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.volume_slider;
            lv_slider_set_value(objects.volume_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_volume_label();
        const char *cur_val = lv_label_get_text(objects.volume_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.volume_label;
            lv_label_set_text(objects.volume_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_gain_value();
        int32_t cur_val = lv_slider_get_value(objects.gain_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.gain_slider;
            lv_slider_set_value(objects.gain_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_gain_label();
        const char *cur_val = lv_label_get_text(objects.gain_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.gain_label;
            lv_label_set_text(objects.gain_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_dre_level();
        int32_t cur_val = lv_slider_get_value(objects.dre_level_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.dre_level_slider;
            lv_slider_set_value(objects.dre_level_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_dre_level_label();
        const char *cur_val = lv_label_get_text(objects.dre_level_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.dre_level_label;
            lv_label_set_text(objects.dre_level_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_dre_gain();
        int32_t cur_val = lv_slider_get_value(objects.dre_max_gain_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.dre_max_gain_slider;
            lv_slider_set_value(objects.dre_max_gain_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_dre_gain_label();
        const char *cur_val = lv_label_get_text(objects.dre_gain_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.dre_gain_label;
            lv_label_set_text(objects.dre_gain_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_agc_level_value();
        int32_t cur_val = lv_slider_get_value(objects.agc_level_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.agc_level_slider;
            lv_slider_set_value(objects.agc_level_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_agc_level_label();
        const char *cur_val = lv_label_get_text(objects.agc_level_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.agc_level_label;
            lv_label_set_text(objects.agc_level_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_agc_gain_value();
        int32_t cur_val = lv_slider_get_value(objects.agc_max_gain_slider);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.agc_max_gain_slider;
            lv_slider_set_value(objects.agc_max_gain_slider, new_val, LV_ANIM_ON);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_agc_gain_label();
        const char *cur_val = lv_label_get_text(objects.agc_gain_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.agc_gain_label;
            lv_label_set_text(objects.agc_gain_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
}


void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_main();
}

typedef void (*tick_screen_func_t)();

tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};

void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
