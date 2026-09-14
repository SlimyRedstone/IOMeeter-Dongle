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
    FLOW_GLOBAL_VARIABLE_DEVICE_STATUS_TEXT = 0,
    FLOW_GLOBAL_VARIABLE_DEVICE_IS_CONNECTED = 1,
    FLOW_GLOBAL_VARIABLE_DEVICE_IS_DISCONNECTED = 2
};

// Native global variables

extern const char *get_var_device_status_text();
extern void set_var_device_status_text(const char *value);
extern bool get_var_device_is_connected();
extern void set_var_device_is_connected(bool value);
extern bool get_var_device_is_disconnected();
extern void set_var_device_is_disconnected(bool value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/