#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"

static const char *TAG = "config";

#define MOUNT_POINT     "/spiffs"
#define PARTITION_LABEL "storage"
#define CONFIG_JSON     MOUNT_POINT "/config.json"

/* Refuse to allocate for a file that cannot plausibly be our config. */
#define MAX_FILE_BYTES  4096

/* JSON key for each slot, indexed by config_led_state_t. */
static const char *const s_led_keys[LED_STATE_MAX] = {
    [LED_STATE_BOOT]      = "on_boot",
    [LED_STATE_CONNECTED] = "on_connected",
    [LED_STATE_RECEIVE]   = "on_receive",
    [LED_STATE_ABSENT]    = "on_absent",
};

static const uint32_t s_led_defaults[LED_STATE_MAX] = {
    [LED_STATE_BOOT]      = 0xFF0000,
    [LED_STATE_CONNECTED] = 0x00FF00,
    [LED_STATE_RECEIVE]   = 0xFF00FF,
    [LED_STATE_ABSENT]    = 0xFF6000,
};

/* Scales every colour above; see config_led_brightness(). */
#define LED_BRIGHTNESS_KEY      "brightness"
#define LED_BRIGHTNESS_DEFAULT  64

static uint32_t s_led_colors[LED_STATE_MAX];
static uint8_t  s_led_brightness = LED_BRIGHTNESS_DEFAULT;
static bool     s_mounted;

/*
 * Slider positions held in RAM only. They track what the host last sent so the
 * device can answer for them; persisting them would mean rewriting the file on
 * every drag.
 */
static int s_sliders[CONFIG_SLIDER_COUNT];

/* ------------------------------------------------------ ESP-NOW pairing -- */

#define PAIR_KEY            "pair"
#define PAIR_LED_KEY        "led"
#define PAIR_BATTERY_KEY    "battery_min"
#define PAIR_BATTERY_DEFAULT 1

static const char *const s_pair_mac_keys[PAIR_MAC_MAX] = {
    [PAIR_MAC_MASTER] = "master",
    [PAIR_MAC_SLAVE]  = "slave",
    [PAIR_MAC_THIS]   = "this",
};

static const char *const s_pair_led_keys[PAIR_LED_MAX] = {
    [PAIR_LED_CONNECTED]    = "connected",
    [PAIR_LED_DISCONNECTED] = "disconnected",
    [PAIR_LED_PAIRING]      = "pairing",
    [PAIR_LED_NO_CONFIG]    = "no_config",
};

static const uint32_t s_pair_led_defaults[PAIR_LED_MAX] = {
    [PAIR_LED_CONNECTED]    = 0x00FF00,
    [PAIR_LED_DISCONNECTED] = 0x3FFF00,
    [PAIR_LED_PAIRING]      = 0x003FFF,
    [PAIR_LED_NO_CONFIG]    = 0xFFFF00,
};

static uint8_t  s_pair_mac[PAIR_MAC_MAX][CONFIG_PAIR_MAC_LEN];
static bool     s_pair_mac_valid[PAIR_MAC_MAX];
static uint32_t s_pair_led_colors[PAIR_LED_MAX];
static uint8_t  s_pair_battery_min = PAIR_BATTERY_DEFAULT;

/* ------------------------------------------------------------- helpers -- */

/* Accepts "RRGGBB" and "#RRGGBB". Returns false on anything else. */
static bool parse_hex_color(const char *text, uint32_t *out_rgb)
{
    if (text == NULL) {
        return false;
    }
    if (*text == '#') {
        text++;
    }
    if (strlen(text) != 6) {
        return false;
    }

    uint32_t rgb = 0;
    for (int i = 0; i < 6; i++) {
        char c = text[i];
        int nib;

        if (c >= '0' && c <= '9')      nib = c - '0';
        else if (c >= 'a' && c <= 'f') nib = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nib = c - 'A' + 10;
        else return false;

        rgb = (rgb << 4) | (uint32_t)nib;
    }

    *out_rgb = rgb;
    return true;
}

/*
 * Accepts "AABBCCDDEEFF" with ':' or '-' between any pair of digits, which
 * covers both the form written back out and whatever a hand-edited file or the
 * desktop client happens to use. An empty string means "no address", and is
 * reported as a parse failure so the slot is left cleared.
 */
static bool parse_mac(const char *text, uint8_t out[CONFIG_PAIR_MAC_LEN])
{
    if (text == NULL) {
        return false;
    }

    int nibbles = 0;
    uint8_t mac[CONFIG_PAIR_MAC_LEN] = {0};

    for (const char *c = text; *c != '\0'; c++) {
        if (*c == ':' || *c == '-') {
            continue;
        }

        int nib;
        if (*c >= '0' && *c <= '9')      nib = *c - '0';
        else if (*c >= 'a' && *c <= 'f') nib = *c - 'a' + 10;
        else if (*c >= 'A' && *c <= 'F') nib = *c - 'A' + 10;
        else return false;

        if (nibbles >= CONFIG_PAIR_MAC_LEN * 2) {
            return false;
        }
        mac[nibbles / 2] = (uint8_t)((mac[nibbles / 2] << 4) | nib);
        nibbles++;
    }

    if (nibbles != CONFIG_PAIR_MAC_LEN * 2) {
        return false;
    }
    memcpy(out, mac, CONFIG_PAIR_MAC_LEN);
    return true;
}

static void format_mac(const uint8_t mac[CONFIG_PAIR_MAC_LEN],
                       char out[CONFIG_PAIR_MAC_STR_LEN])
{
    snprintf(out, CONFIG_PAIR_MAC_STR_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void load_defaults(void)
{
    memcpy(s_led_colors, s_led_defaults, sizeof(s_led_colors));
    s_led_brightness = LED_BRIGHTNESS_DEFAULT;

    memcpy(s_pair_led_colors, s_pair_led_defaults, sizeof(s_pair_led_colors));
    memset(s_pair_mac, 0, sizeof(s_pair_mac));
    memset(s_pair_mac_valid, 0, sizeof(s_pair_mac_valid));
    s_pair_battery_min = PAIR_BATTERY_DEFAULT;
}

/*
 * Shared by the file loader and the host update path. Every key is optional,
 * so a file that predates the pairing feature simply keeps the defaults, and a
 * partial object from the host updates only what it names.
 *
 * @return number of values applied.
 */
static int apply_pair(const cJSON *pair)
{
    int applied = 0;

    for (int i = 0; i < PAIR_MAC_MAX; i++) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(pair, s_pair_mac_keys[i]);
        if (!cJSON_IsString(item)) {
            continue;
        }

        uint8_t mac[CONFIG_PAIR_MAC_LEN];
        if (parse_mac(item->valuestring, mac)) {
            memcpy(s_pair_mac[i], mac, sizeof(mac));
            s_pair_mac_valid[i] = true;
        } else if (item->valuestring[0] == '\0') {
            s_pair_mac_valid[i] = false;   /* deliberately cleared */
        } else {
            ESP_LOGW(TAG, PAIR_KEY ".%s is not a MAC address: \"%s\"",
                     s_pair_mac_keys[i], item->valuestring);
            continue;
        }
        applied++;
    }

    const cJSON *led = cJSON_GetObjectItemCaseSensitive(pair, PAIR_LED_KEY);
    if (cJSON_IsObject(led)) {
        for (int i = 0; i < PAIR_LED_MAX; i++) {
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(led, s_pair_led_keys[i]);
            if (!cJSON_IsString(item)) {
                continue;
            }

            uint32_t rgb;
            if (!parse_hex_color(item->valuestring, &rgb)) {
                ESP_LOGW(TAG, PAIR_KEY "." PAIR_LED_KEY ".%s is not RRGGBB hex: \"%s\"",
                         s_pair_led_keys[i], item->valuestring);
                continue;
            }
            s_pair_led_colors[i] = rgb;
            applied++;
        }
    }

    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(pair, PAIR_BATTERY_KEY);
    if (cJSON_IsNumber(minutes)) {
        if (minutes->valueint >= 1 && minutes->valueint <= 255) {
            s_pair_battery_min = (uint8_t)minutes->valueint;
            applied++;
        } else {
            ESP_LOGW(TAG, PAIR_KEY "." PAIR_BATTERY_KEY " out of range: %d",
                     minutes->valueint);
        }
    }

    return applied;
}

/* Shared by the file loader and the host update path. */
static bool apply_brightness(const cJSON *led)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(led, LED_BRIGHTNESS_KEY);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    if (item->valueint < 0 || item->valueint > 255) {
        ESP_LOGW(TAG, "led." LED_BRIGHTNESS_KEY " out of range: %d", item->valueint);
        return false;
    }
    s_led_brightness = (uint8_t)item->valueint;
    return true;
}

/* Caller frees. Returns NULL if the file is absent, empty or implausibly big. */
static char *read_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return NULL;
    }
    if (st.st_size <= 0 || st.st_size > MAX_FILE_BYTES) {
        ESP_LOGW(TAG, "%s has an implausible size (%ld bytes)", path, (long)st.st_size);
        return NULL;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    char *buf = malloc((size_t)st.st_size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* Missing or malformed entries keep whatever is already in s_led_colors. */
static void parse_json(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        ESP_LOGW(TAG, "%s is not valid JSON, using defaults", CONFIG_JSON);
        return;
    }

    const cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (cJSON_IsObject(led)) {
        for (int i = 0; i < LED_STATE_MAX; i++) {
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(led, s_led_keys[i]);
            if (!cJSON_IsString(item)) {
                continue;
            }

            uint32_t rgb;
            if (parse_hex_color(item->valuestring, &rgb)) {
                s_led_colors[i] = rgb;
            } else {
                ESP_LOGW(TAG, "led.%s is not RRGGBB hex: \"%s\"",
                         s_led_keys[i], item->valuestring);
            }
        }
        apply_brightness(led);
    } else {
        ESP_LOGW(TAG, "no \"led\" object, using defaults");
    }

    const cJSON *pair = cJSON_GetObjectItemCaseSensitive(root, PAIR_KEY);
    if (cJSON_IsObject(pair)) {
        apply_pair(pair);
    }

    cJSON_Delete(root);
}

/* ----------------------------------------------------------- public API -- */

char *config_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON *led = cJSON_AddObjectToObject(root, "led");
    if (led == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < LED_STATE_MAX; i++) {
        char hex[7];
        snprintf(hex, sizeof(hex), "%06X", (unsigned)(s_led_colors[i] & 0xFFFFFF));
        cJSON_AddStringToObject(led, s_led_keys[i], hex);
    }
    cJSON_AddNumberToObject(led, LED_BRIGHTNESS_KEY, s_led_brightness);

    cJSON *pair = cJSON_AddObjectToObject(root, PAIR_KEY);
    if (pair == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < PAIR_MAC_MAX; i++) {
        char mac[CONFIG_PAIR_MAC_STR_LEN] = "";
        if (s_pair_mac_valid[i]) {
            format_mac(s_pair_mac[i], mac);
        }
        cJSON_AddStringToObject(pair, s_pair_mac_keys[i], mac);
    }

    cJSON *pair_led = cJSON_AddObjectToObject(pair, PAIR_LED_KEY);
    if (pair_led == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < PAIR_LED_MAX; i++) {
        char hex[7];
        snprintf(hex, sizeof(hex), "%06X", (unsigned)(s_pair_led_colors[i] & 0xFFFFFF));
        cJSON_AddStringToObject(pair_led, s_pair_led_keys[i], hex);
    }
    cJSON_AddNumberToObject(pair, PAIR_BATTERY_KEY, s_pair_battery_min);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

esp_err_t config_from_json(const char *json)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int applied = 0;
    const cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");

    if (cJSON_IsObject(led)) {
        for (int i = 0; i < LED_STATE_MAX; i++) {
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(led, s_led_keys[i]);
            if (!cJSON_IsString(item)) {
                continue;
            }

            uint32_t rgb;
            if (!parse_hex_color(item->valuestring, &rgb)) {
                ESP_LOGW(TAG, "led.%s is not RRGGBB hex: \"%s\"",
                         s_led_keys[i], item->valuestring);
                continue;
            }
            s_led_colors[i] = rgb;
            applied++;
        }
        if (apply_brightness(led)) {
            applied++;
        }
    }

    const cJSON *pair = cJSON_GetObjectItemCaseSensitive(root, PAIR_KEY);
    if (cJSON_IsObject(pair)) {
        applied += apply_pair(pair);
    }

    cJSON_Delete(root);

    if (applied == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return config_save();
}

esp_err_t config_save(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char *text = config_to_json();
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(CONFIG_JSON, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s for writing", CONFIG_JSON);
        cJSON_free(text);
        return ESP_FAIL;
    }

    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    fclose(f);
    cJSON_free(text);

    if (written != len) {
        ESP_LOGE(TAG, "short write to %s (%u of %u bytes)",
                 CONFIG_JSON, (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved %s", CONFIG_JSON);
    return ESP_OK;
}

esp_err_t config_reload(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    load_defaults();

    char *text = read_file(CONFIG_JSON);
    if (text == NULL) {
        ESP_LOGW(TAG, "%s missing, writing defaults", CONFIG_JSON);
        return config_save();
    }

    parse_json(text);
    free(text);

    ESP_LOGI(TAG, "boot=#%06X connected=#%06X receive=#%06X absent=#%06X",
             (unsigned)s_led_colors[LED_STATE_BOOT],
             (unsigned)s_led_colors[LED_STATE_CONNECTED],
             (unsigned)s_led_colors[LED_STATE_RECEIVE],
             (unsigned)s_led_colors[LED_STATE_ABSENT]);
    return ESP_OK;
}

esp_err_t config_init(void)
{
    if (s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    load_defaults();

    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path              = MOUNT_POINT,
        .partition_label        = PARTITION_LABEL,
        .max_files              = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&spiffs_cfg);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "no '%s' partition -- is partitions.csv selected?",
                     PARTITION_LABEL);
        } else {
            ESP_LOGE(TAG, "esp_vfs_spiffs_register: %s", esp_err_to_name(err));
        }
        return err;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "spiffs mounted at %s, %u/%u bytes used",
                 MOUNT_POINT, (unsigned)used, (unsigned)total);
    }

    s_mounted = true;
    return config_reload();
}

esp_err_t config_deinit(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mounted = false;
    return esp_vfs_spiffs_unregister(PARTITION_LABEL);
}

uint32_t config_led_color(config_led_state_t state)
{
    if (state < 0 || state >= LED_STATE_MAX) {
        return 0;
    }
    return s_mounted ? s_led_colors[state] : s_led_defaults[state];
}

esp_err_t config_set_led_color(config_led_state_t state, uint32_t rgb)
{
    if (state < 0 || state >= LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_led_colors[state] = rgb & 0xFFFFFF;
    return ESP_OK;
}

uint8_t config_led_brightness(void)
{
    return s_mounted ? s_led_brightness : LED_BRIGHTNESS_DEFAULT;
}

esp_err_t config_set_led_brightness(uint8_t level)
{
    s_led_brightness = level;
    return ESP_OK;
}

esp_err_t config_set_slider(int id, int value)
{
    if (id < 0 || id >= CONFIG_SLIDER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_sliders[id] = value;
    ESP_LOGI(TAG, "slider %d = %d", id, value);
    return ESP_OK;
}

int config_get_slider(int id)
{
    if (id < 0 || id >= CONFIG_SLIDER_COUNT) {
        return 0;
    }
    return s_sliders[id];
}

bool config_pair_mac(config_pair_mac_t slot, uint8_t out[CONFIG_PAIR_MAC_LEN])
{
    if (slot < 0 || slot >= PAIR_MAC_MAX || out == NULL || !s_pair_mac_valid[slot]) {
        return false;
    }
    memcpy(out, s_pair_mac[slot], CONFIG_PAIR_MAC_LEN);
    return true;
}

esp_err_t config_set_pair_mac(config_pair_mac_t slot,
                              const uint8_t mac[CONFIG_PAIR_MAC_LEN])
{
    if (slot < 0 || slot >= PAIR_MAC_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mac == NULL) {
        s_pair_mac_valid[slot] = false;
        memset(s_pair_mac[slot], 0, CONFIG_PAIR_MAC_LEN);
        return ESP_OK;
    }

    memcpy(s_pair_mac[slot], mac, CONFIG_PAIR_MAC_LEN);
    s_pair_mac_valid[slot] = true;
    return ESP_OK;
}

uint32_t config_pair_led_color(config_pair_led_t slot)
{
    if (slot < 0 || slot >= PAIR_LED_MAX) {
        return 0;
    }
    return s_mounted ? s_pair_led_colors[slot] : s_pair_led_defaults[slot];
}

esp_err_t config_set_pair_led_color(config_pair_led_t slot, uint32_t rgb)
{
    if (slot < 0 || slot >= PAIR_LED_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pair_led_colors[slot] = rgb & 0xFFFFFF;
    return ESP_OK;
}

uint8_t config_pair_battery_minutes(void)
{
    return s_mounted ? s_pair_battery_min : PAIR_BATTERY_DEFAULT;
}

esp_err_t config_set_pair_battery_minutes(uint8_t minutes)
{
    if (minutes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pair_battery_min = minutes;
    return ESP_OK;
}

const char *config_file_path(void)
{
    return CONFIG_JSON;
}
