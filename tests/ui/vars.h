#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations



// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_DRE_LEVEL = 0,
    FLOW_GLOBAL_VARIABLE_DRE_LEVEL_LABEL = 1,
    FLOW_GLOBAL_VARIABLE_DRE_GAIN = 2,
    FLOW_GLOBAL_VARIABLE_DRE_GAIN_LABEL = 3,
    FLOW_GLOBAL_VARIABLE_AGC_LEVEL_VALUE = 4,
    FLOW_GLOBAL_VARIABLE_AGC_LEVEL_LABEL = 5,
    FLOW_GLOBAL_VARIABLE_AGC_GAIN_VALUE = 6,
    FLOW_GLOBAL_VARIABLE_AGC_GAIN_LABEL = 7,
    FLOW_GLOBAL_VARIABLE_RGB_VALUE = 8
};

// Native global variables

extern float get_var_agc_level();
extern void set_var_agc_level(float value);
extern float get_var_agc_max_gain();
extern void set_var_agc_max_gain(float value);
extern int32_t get_var_hue_value();
extern void set_var_hue_value(int32_t value);
extern int32_t get_var_brightness_value();
extern void set_var_brightness_value(int32_t value);
extern int32_t get_var_volume_value();
extern void set_var_volume_value(int32_t value);
extern const char *get_var_volume_label();
extern void set_var_volume_label(const char *value);
extern int32_t get_var_gain_value();
extern void set_var_gain_value(int32_t value);
extern const char *get_var_gain_label();
extern void set_var_gain_label(const char *value);
extern int32_t get_var_dre_level();
extern void set_var_dre_level(int32_t value);
extern const char *get_var_dre_level_label();
extern void set_var_dre_level_label(const char *value);
extern int32_t get_var_dre_gain();
extern void set_var_dre_gain(int32_t value);
extern const char *get_var_dre_gain_label();
extern void set_var_dre_gain_label(const char *value);
extern int32_t get_var_agc_level_value();
extern void set_var_agc_level_value(int32_t value);
extern const char *get_var_agc_level_label();
extern void set_var_agc_level_label(const char *value);
extern int32_t get_var_agc_gain_value();
extern void set_var_agc_gain_value(int32_t value);
extern const char *get_var_agc_gain_label();
extern void set_var_agc_gain_label(const char *value);
extern int32_t get_var_rgb_value();
extern void set_var_rgb_value(int32_t value);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/