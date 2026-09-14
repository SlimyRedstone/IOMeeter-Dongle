/*
 * ESP-NOW link, pairing and the binary report format. See now_connect.h for
 * the wire layout and the rules the two roles follow.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "config.h"
#include "now_connect.h"

static const char *TAG = "now_connect";

/* Frames that do not open with these two bytes are not ours. */
#define PROTO_MAGIC   0x4E   /* 'N' */
#define PROTO_VERSION 1

#define RX_QUEUE_DEPTH 6
#define TASK_STACK     5120  /* pairing writes config.json from this task */
#define TASK_PRIO      4

/* Housekeeping period: also the resolution of the pairing button's hold and
   the step size of the LED fade. */
#define TICK_MS           50
#define PAIR_BROADCAST_MS 250

/*
 * The LED says three things. Pairing fades the window's colour in and out at
 * 0.5Hz -- one full breath every two seconds -- which is unmistakable across a
 * room and cannot be confused with a resting state. Otherwise the colour is
 * held steady, and a frame carrying real data blinks it off and on twice.
 */
#define LED_FADE_PERIOD_MS 2000
#define LED_BLINK_PHASE_MS 80
#define LED_BLINK_PHASES   4     /* off, on, off, on */

#define TWO_PI 6.283185307f

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t type;    /*!< now_connect_msg_t            */
    uint8_t role;    /*!< sender's now_connect_role_t  */
} frame_header_t;

/* One received frame, copied out of the Wi-Fi task and handed to ours. */
typedef struct {
    uint8_t mac[NOW_CONNECT_MAC_LEN];
    uint8_t role;
    uint8_t type;
    uint8_t len;
    uint8_t data[NOW_CONNECT_PAYLOAD_MAX];
} rx_frame_t;

static const uint8_t s_broadcast[NOW_CONNECT_MAC_LEN] =
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static now_connect_config_t s_cfg;
static QueueHandle_t        s_rx_queue;
static TaskHandle_t         s_task;
static volatile bool        s_running;

static uint8_t s_self_mac[NOW_CONNECT_MAC_LEN];
static uint8_t s_peer_mac[NOW_CONNECT_MAC_LEN];
static bool    s_paired;

static now_connect_status_t s_status = NOW_CONNECT_STATUS_MAX;

/* Set by the USB layer; read on every transmission. */
static volatile bool s_usb_active;

static bool    s_pairing;
static int64_t s_pair_deadline_ms;
static int64_t s_pair_next_tx_ms;

/* Milliseconds since boot, from esp_timer, so nothing here wraps. */
static int64_t s_last_rx_ms;
static int64_t s_last_tx_ms;    /*!< Last attempt, delivered or not */
static int64_t s_next_battery_ms;

static uint8_t s_peer_battery = NOW_CONNECT_BATTERY_UNKNOWN;

static int64_t s_button_down_ms;  /*!< 0 while the button is released */
static bool    s_button_latched;  /*!< Hold already opened a window   */

static uint32_t s_led_base;                /*!< Colour the current status calls for */
static uint32_t s_led_shown = UINT32_MAX;  /*!< Last colour written; never a real one */
static int64_t  s_blink_start_ms;           /*!< 0 when no blink is running */

/* ------------------------------------------------------------- helpers -- */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t status_color(now_connect_status_t status)
{
    switch (status) {
    case NOW_CONNECT_STATUS_CONNECTED:    return s_cfg.led_connected;
    case NOW_CONNECT_STATUS_PAIRING:      return s_cfg.led_pairing;
    case NOW_CONNECT_STATUS_DISCONNECTED: return s_cfg.led_disconnected;
    case NOW_CONNECT_STATUS_NO_CONFIG:
    default:                              return s_cfg.led_no_config;
    }
}

static uint32_t scale_rgb(uint32_t rgb, uint8_t level)
{
    const uint32_t r = (((rgb >> 16) & 0xFF) * level + 127) / 255;
    const uint32_t g = (((rgb >> 8) & 0xFF) * level + 127) / 255;
    const uint32_t b = ((rgb & 0xFF) * level + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

/*
 * Works out what the LED should be showing this instant and writes it only if
 * that differs from what it is showing already, so a steady colour costs one
 * call and a fade costs one per tick.
 */
static void service_led(void)
{
    if (s_cfg.on_led == NULL) {
        return;
    }

    const int64_t now = now_ms();
    uint32_t rgb = s_led_base;

    if (s_status == NOW_CONNECT_STATUS_PAIRING) {
        /* A cosine rather than a triangle: the turn at each end is smooth, so
           it reads as breathing instead of as a sawtooth. */
        const float phase = (float)(now % LED_FADE_PERIOD_MS) / LED_FADE_PERIOD_MS;
        const float level = (1.0f - cosf(TWO_PI * phase)) * 0.5f;
        rgb = scale_rgb(s_led_base, (uint8_t)(level * 255.0f));
    } else if (s_blink_start_ms != 0) {
        const int64_t elapsed = now - s_blink_start_ms;

        if (elapsed < (int64_t)LED_BLINK_PHASE_MS * LED_BLINK_PHASES) {
            /* Phases 0 to 3 are off, on, off, on: the LED drops out twice and
               ends lit, so it runs back into the resting colour without a step. */
            const int phase = (int)(elapsed / LED_BLINK_PHASE_MS);
            rgb = (phase % 2 == 0) ? 0 : s_led_base;
        } else {
            s_blink_start_ms = 0;
        }
    }

    if (rgb != s_led_shown) {
        s_led_shown = rgb;
        s_cfg.on_led(rgb);
    }
}

/* One blink at a time: a burst of frames must not leave the LED stuttering. */
static void led_blink(void)
{
    const int64_t now = now_ms();

    if (s_status == NOW_CONNECT_STATUS_PAIRING) {
        return;
    }
    if (s_blink_start_ms != 0 &&
        now < s_blink_start_ms + (int64_t)LED_BLINK_PHASE_MS * LED_BLINK_PHASES) {
        return;
    }
    s_blink_start_ms = now;
}

static void set_status(now_connect_status_t status)
{
    if (status == s_status) {
        return;
    }
    s_status = status;

    /* Answer the "how full is it" question as soon as there is someone to
       answer it to, rather than up to a whole interval later. */
    if (status == NOW_CONNECT_STATUS_CONNECTED) {
        s_next_battery_ms = now_ms();
    }

    ESP_LOGI(TAG, "%s", now_connect_status_name(status));

    s_led_base       = status_color(status);
    s_led_shown      = UINT32_MAX;   /* whatever it was, redraw it */
    s_blink_start_ms = 0;
    service_led();

    if (s_cfg.on_status != NULL) {
        s_cfg.on_status(status);
    }
}

static uint32_t battery_interval_ms(void)
{
    uint8_t minutes = s_cfg.battery_minutes ? s_cfg.battery_minutes
                                            : config_pair_battery_minutes();
    if (minutes == 0) {
        minutes = 1;
    }
    return (uint32_t)minutes * 60U * 1000U;
}

static esp_err_t peer_add(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    esp_now_peer_info_t peer = {
        .channel = s_cfg.channel,
        .ifidx   = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac, NOW_CONNECT_MAC_LEN);
    return esp_now_add_peer(&peer);
}

/*
 * Every transmission goes through here. Two rules live in one place as a
 * result: a slave that is on USB stays silent apart from pairing, which the
 * user may well be doing with the cable plugged in, and the heartbeat clock is
 * pushed back by any frame at all, so a device that is already reporting never
 * adds a heartbeat on top.
 */
static esp_err_t tx(const uint8_t *mac, uint8_t type, const void *data, size_t len)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > NOW_CONNECT_PAYLOAD_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_last_tx_ms = now_ms();

    const bool is_pairing_frame = (type == NOW_CONNECT_MSG_PAIR_REQ ||
                                   type == NOW_CONNECT_MSG_PAIR_ACK);
    if (s_usb_active && s_cfg.role == NOW_CONNECT_ROLE_SLAVE && !is_pairing_frame) {
        return ESP_ERR_INVALID_STATE;
    }

    if (type >= NOW_CONNECT_MSG_REPORT) {
        led_blink();
    }

    uint8_t frame[sizeof(frame_header_t) + NOW_CONNECT_PAYLOAD_MAX];
    frame_header_t *header = (frame_header_t *)frame;

    header->magic   = PROTO_MAGIC;
    header->version = PROTO_VERSION;
    header->type    = type;
    header->role    = (uint8_t)s_cfg.role;

    if (len > 0 && data != NULL) {
        memcpy(frame + sizeof(frame_header_t), data, len);
    }

    return esp_now_send(mac, frame, sizeof(frame_header_t) + len);
}

static esp_err_t tx_peer(uint8_t type, const void *data, size_t len)
{
    if (!s_paired) {
        return ESP_ERR_INVALID_STATE;
    }
    return tx(s_peer_mac, type, data, len);
}

/* ------------------------------------------------------------- pairing -- */

static void pairing_open(void)
{
    s_pairing          = true;
    s_pair_deadline_ms = now_ms() + s_cfg.pair_window_ms;
    s_pair_next_tx_ms  = 0;
    set_status(NOW_CONNECT_STATUS_PAIRING);
}

static void pairing_close(void)
{
    s_pairing = false;
}

/*
 * Write both addresses out. Which slot each one belongs in follows from the
 * roles, and this device's own address is stored either way so that the
 * desktop client can read it back without asking the firmware.
 */
static void pairing_store(void)
{
    const bool self_is_master = (s_cfg.role == NOW_CONNECT_ROLE_MASTER);

    config_set_pair_mac(PAIR_MAC_THIS, s_self_mac);
    config_set_pair_mac(PAIR_MAC_MASTER, self_is_master ? s_self_mac : s_peer_mac);
    config_set_pair_mac(PAIR_MAC_SLAVE,  self_is_master ? s_peer_mac : s_self_mac);

    esp_err_t err = config_save();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot persist the pairing: %s", esp_err_to_name(err));
    }
}

static void pairing_complete(const uint8_t *mac)
{
    if (s_paired && memcmp(mac, s_peer_mac, NOW_CONNECT_MAC_LEN) != 0) {
        esp_now_del_peer(s_peer_mac);
    }

    memcpy(s_peer_mac, mac, NOW_CONNECT_MAC_LEN);
    s_paired = true;

    esp_err_t err = peer_add(s_peer_mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer: %s", esp_err_to_name(err));
    }

    pairing_store();
    pairing_close();

    char text[18];
    now_connect_mac_str(s_peer_mac, text, sizeof(text));
    ESP_LOGI(TAG, "paired with %s", text);

    s_last_rx_ms = now_ms();
    set_status(NOW_CONNECT_STATUS_CONNECTED);
}

static void service_pairing(void)
{
    if (!s_pairing) {
        return;
    }

    int64_t now = now_ms();
    if (now >= s_pair_deadline_ms) {
        ESP_LOGW(TAG, "pairing window closed without an answer");
        pairing_close();
        return;
    }

    if (now >= s_pair_next_tx_ms) {
        s_pair_next_tx_ms = now + PAIR_BROADCAST_MS;
        tx(s_broadcast, NOW_CONNECT_MSG_PAIR_REQ, NULL, 0);
    }
}

/* ---------------------------------------------------------------- link -- */

static void service_link(void)
{
    if (s_pairing) {
        return;
    }
    if (!s_paired) {
        set_status(NOW_CONNECT_STATUS_NO_CONFIG);
        return;
    }

    int64_t now = now_ms();
    bool up = (now - s_last_rx_ms) < CONFIG_NOW_CONNECT_LINK_TIMEOUT_MS;
    set_status(up ? NOW_CONNECT_STATUS_CONNECTED : NOW_CONNECT_STATUS_DISCONNECTED);

    if (now - s_last_tx_ms >= CONFIG_NOW_CONNECT_HEARTBEAT_MS) {
        tx_peer(NOW_CONNECT_MSG_HEARTBEAT, NULL, 0);
    }
}

static void service_battery(void)
{
    if (!s_paired || s_pairing || now_ms() < s_next_battery_ms) {
        return;
    }
    s_next_battery_ms = now_ms() + battery_interval_ms();
    now_connect_send_battery();
}

static void poll_button(void)
{
    if (s_cfg.pair_gpio < 0) {
        return;
    }

    int  level = gpio_get_level(s_cfg.pair_gpio);
    bool down  = s_cfg.pair_active_high ? (level != 0) : (level == 0);

    if (!down) {
        s_button_down_ms = 0;
        s_button_latched = false;
        return;
    }

    int64_t now = now_ms();
    if (s_button_down_ms == 0) {
        s_button_down_ms = now;   /* the hold time is its own debounce */
        return;
    }
    if (!s_button_latched && (now - s_button_down_ms) >= (int64_t)s_cfg.pair_hold_ms) {
        s_button_latched = true;
        ESP_LOGI(TAG, "pairing button held, opening the window");
        pairing_open();
    }
}

/* ------------------------------------------------------------ receive --- */

static void handle_frame(const rx_frame_t *frame)
{
    /* While the window is open, a device of the opposite role is exactly what
       is being looked for, whoever it turns out to be. */
    if (s_pairing && (frame->type == NOW_CONNECT_MSG_PAIR_REQ ||
                      frame->type == NOW_CONNECT_MSG_PAIR_ACK)) {
        if (frame->role == (uint8_t)s_cfg.role) {
            return;   /* two masters or two slaves have nothing to say */
        }

        bool answer = (frame->type == NOW_CONNECT_MSG_PAIR_REQ);
        pairing_complete(frame->mac);
        if (answer) {
            tx(s_peer_mac, NOW_CONNECT_MSG_PAIR_ACK, NULL, 0);
        }
        return;
    }

    /* Outside the window, only the paired peer is listened to. */
    if (!s_paired || memcmp(frame->mac, s_peer_mac, NOW_CONNECT_MAC_LEN) != 0) {
        return;
    }

    s_last_rx_ms = now_ms();

    switch (frame->type) {
    case NOW_CONNECT_MSG_PAIR_REQ:
        /* It has forgotten us and is looking again; answer so it settles. */
        tx(s_peer_mac, NOW_CONNECT_MSG_PAIR_ACK, NULL, 0);
        return;

    case NOW_CONNECT_MSG_PAIR_ACK:
    case NOW_CONNECT_MSG_HEARTBEAT:
        return;

    case NOW_CONNECT_MSG_REPORT:
        if ((size_t)frame->len < sizeof(now_connect_report_t)) {
            ESP_LOGW(TAG, "short report: %u bytes", (unsigned)frame->len);
            return;
        }
        if (s_cfg.on_report != NULL) {
            now_connect_report_t report;
            memcpy(&report, frame->data, sizeof(report));
            s_cfg.on_report(&report);
        }
        break;

    case NOW_CONNECT_MSG_BATTERY:
        if (frame->len < 1) {
            return;
        }
        s_peer_battery = frame->data[0];
        if (s_cfg.on_peer_battery != NULL) {
            s_cfg.on_peer_battery(s_peer_battery);
        }
        break;

    default:
        break;
    }

    led_blink();

    if (s_cfg.on_packet != NULL) {
        s_cfg.on_packet(frame->type, frame->data, frame->len);
    }
}

/*
 * Runs on the Wi-Fi task, so it does nothing beyond validating the header and
 * handing the frame on. Anything rejected here never reaches the queue.
 */
static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || s_rx_queue == NULL) {
        return;
    }
    if (len < (int)sizeof(frame_header_t) ||
        len > (int)(sizeof(frame_header_t) + NOW_CONNECT_PAYLOAD_MAX)) {
        return;
    }

    const frame_header_t *header = (const frame_header_t *)data;
    if (header->magic != PROTO_MAGIC || header->version != PROTO_VERSION) {
        return;
    }

    rx_frame_t frame;
    memcpy(frame.mac, info->src_addr, NOW_CONNECT_MAC_LEN);
    frame.role = header->role;
    frame.type = header->type;
    frame.len  = (uint8_t)(len - sizeof(frame_header_t));
    if (frame.len > 0) {
        memcpy(frame.data, data + sizeof(frame_header_t), frame.len);
    }

    if (xQueueSend(s_rx_queue, &frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "rx queue full, dropped a type %02X frame", (unsigned)frame.type);
    }
}

static void now_connect_task(void *arg)
{
    (void)arg;

    /* Too large for the stack, and only this task touches it. */
    static rx_frame_t frame;

    while (s_running) {
        /*
         * Blocking on the queue keeps received frames prompt; the housekeeping
         * below runs afterwards either way, and each part of it is held to its
         * own deadline, so running it more often than TICK_MS costs nothing.
         */
        if (xQueueReceive(s_rx_queue, &frame, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            handle_frame(&frame);
        }

        poll_button();
        service_pairing();
        service_link();
        service_battery();
        service_led();
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------- radio -- */

static esp_err_t radio_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        esp_err_t erase = nvs_flash_erase();
        if (erase != ESP_OK) {
            return erase;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    /* Both are shared with whatever else the application brought up. */
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* Nothing here joins an access point, so none of it belongs in flash. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    /* With no access point to follow, the channel is simply asserted, and has
       to be the same one the other device asserts. */
    err = esp_wifi_set_channel(s_cfg.channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        return err;
    }

    return esp_wifi_get_mac(WIFI_IF_STA, s_self_mac);
}

static void radio_stop(void)
{
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* ---------------------------------------------------------- public API -- */

esp_err_t now_connect_start(const now_connect_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg = *config;
    if (s_cfg.channel == 0) {
        s_cfg.channel = 1;
    }
    if (s_cfg.pair_hold_ms == 0) {
        s_cfg.pair_hold_ms = CONFIG_NOW_CONNECT_PAIR_HOLD_MS;
    }
    if (s_cfg.pair_window_ms == 0) {
        s_cfg.pair_window_ms = CONFIG_NOW_CONNECT_PAIR_WINDOW_S * 1000U;
    }

    s_status         = NOW_CONNECT_STATUS_MAX;   /* forces the first LED write */
    s_pairing        = false;
    s_usb_active     = false;
    s_peer_battery   = NOW_CONNECT_BATTERY_UNKNOWN;
    s_button_down_ms = 0;
    s_button_latched = false;
    s_last_tx_ms     = 0;

    /* Far enough back that the link reads as down until the peer is actually
       heard from: a plain 0 would look recent for the first few seconds. */
    s_last_rx_ms = -(int64_t)CONFIG_NOW_CONNECT_LINK_TIMEOUT_MS - 1;

    /* The peer is whichever slot this device is not. A device that finds none
       has nothing to talk to and says so on the LED until it is paired. */
    const config_pair_mac_t peer_slot = (s_cfg.role == NOW_CONNECT_ROLE_MASTER)
                                      ? PAIR_MAC_SLAVE : PAIR_MAC_MASTER;
    s_paired = config_pair_mac(peer_slot, s_peer_mac);

    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_frame_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = radio_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "radio: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_now_init();
    if (err != ESP_OK) {
        goto fail_radio;
    }

    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        goto fail_radio;
    }

    /* Broadcast is a peer like any other as far as esp_now_send is concerned,
       and it is the only address pairing has to work with. */
    err = peer_add(s_broadcast);
    if (err != ESP_OK) {
        goto fail_radio;
    }

    if (s_paired) {
        err = peer_add(s_peer_mac);
        if (err != ESP_OK) {
            goto fail_radio;
        }
    }

    if (s_cfg.pair_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_cfg.pair_gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = s_cfg.pair_active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = s_cfg.pair_active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&io);
        if (err != ESP_OK) {
            goto fail_radio;
        }
    }

    /* Keep the file's idea of this device's own address current, but only
       write flash when it has actually changed. */
    uint8_t stored[NOW_CONNECT_MAC_LEN];
    if (!config_pair_mac(PAIR_MAC_THIS, stored) ||
        memcmp(stored, s_self_mac, NOW_CONNECT_MAC_LEN) != 0) {
        config_set_pair_mac(PAIR_MAC_THIS, s_self_mac);
        config_save();
    }

    set_status(s_paired ? NOW_CONNECT_STATUS_DISCONNECTED
                        : NOW_CONNECT_STATUS_NO_CONFIG);

    s_running = true;
    if (xTaskCreate(now_connect_task, "now_connect", TASK_STACK, NULL,
                    TASK_PRIO, &s_task) != pdPASS) {
        s_running = false;
        err = ESP_ERR_NO_MEM;
        goto fail_radio;
    }

    char self[18], peer[18];
    now_connect_mac_str(s_self_mac, self, sizeof(self));
    now_connect_mac_str(s_peer_mac, peer, sizeof(peer));
    ESP_LOGI(TAG, "%s %s on channel %u, peer %s",
             s_cfg.role == NOW_CONNECT_ROLE_MASTER ? "master" : "slave",
             self, (unsigned)s_cfg.channel, s_paired ? peer : "unknown");
    return ESP_OK;

fail_radio:
    radio_stop();
fail:
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
    return err;
}

esp_err_t now_connect_stop(void)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Let the task observe the flag and delete itself. */
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(TICK_MS * 3));
    s_task = NULL;

    radio_stop();

    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;

    s_pairing = false;
    s_status  = NOW_CONNECT_STATUS_MAX;
    return ESP_OK;
}

void now_connect_load_colors(now_connect_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->led_connected    = config_pair_led_color(PAIR_LED_CONNECTED);
    config->led_disconnected = config_pair_led_color(PAIR_LED_DISCONNECTED);
    config->led_pairing      = config_pair_led_color(PAIR_LED_PAIRING);
    config->led_no_config    = config_pair_led_color(PAIR_LED_NO_CONFIG);
}

esp_err_t now_connect_start_pairing(void)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pairing) {
        return ESP_OK;
    }
    pairing_open();
    return ESP_OK;
}

esp_err_t now_connect_stop_pairing(void)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    pairing_close();
    return ESP_OK;
}

esp_err_t now_connect_forget(void)
{
    if (s_paired) {
        esp_now_del_peer(s_peer_mac);
    }

    s_paired = false;
    memset(s_peer_mac, 0, sizeof(s_peer_mac));

    config_set_pair_mac(PAIR_MAC_MASTER, NULL);
    config_set_pair_mac(PAIR_MAC_SLAVE, NULL);
    esp_err_t err = config_save();

    set_status(NOW_CONNECT_STATUS_NO_CONFIG);
    return err;
}

now_connect_status_t now_connect_status(void)
{
    return s_status == NOW_CONNECT_STATUS_MAX ? NOW_CONNECT_STATUS_NO_CONFIG : s_status;
}

now_connect_role_t now_connect_role(void)
{
    return s_cfg.role;
}

const char *now_connect_status_name(now_connect_status_t status)
{
    switch (status) {
    case NOW_CONNECT_STATUS_NO_CONFIG:    return "no_config";
    case NOW_CONNECT_STATUS_PAIRING:      return "pairing";
    case NOW_CONNECT_STATUS_DISCONNECTED: return "disconnected";
    case NOW_CONNECT_STATUS_CONNECTED:    return "connected";
    default:                              return "unknown";
    }
}

bool now_connect_is_paired(void)
{
    return s_paired;
}

bool now_connect_peer_mac(uint8_t out[NOW_CONNECT_MAC_LEN])
{
    if (out == NULL || !s_paired) {
        return false;
    }
    memcpy(out, s_peer_mac, NOW_CONNECT_MAC_LEN);
    return true;
}

bool now_connect_self_mac(uint8_t out[NOW_CONNECT_MAC_LEN])
{
    if (out == NULL || !s_running) {
        return false;
    }
    memcpy(out, s_self_mac, NOW_CONNECT_MAC_LEN);
    return true;
}

void now_connect_mac_str(const uint8_t mac[NOW_CONNECT_MAC_LEN], char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    if (mac == NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, max, "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
}

esp_err_t now_connect_set_usb_active(bool active)
{
    if (s_usb_active == active) {
        return ESP_OK;
    }
    s_usb_active = active;

    if (active) {
        ESP_LOGI(TAG, "USB has the link; radio traffic suspended");
        return ESP_OK;
    }

    /* Due a heartbeat immediately, so the master sees the link come back
       within one housekeeping tick rather than one heartbeat interval. */
    s_last_tx_ms = 0;
    ESP_LOGI(TAG, "USB released the link; resuming");
    return ESP_OK;
}

bool now_connect_usb_active(void)
{
    return s_usb_active;
}

esp_err_t now_connect_send_report(const now_connect_report_t *report)
{
    if (report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return tx_peer(NOW_CONNECT_MSG_REPORT, report, sizeof(*report));
}

esp_err_t now_connect_send_battery(void)
{
    uint8_t level = NOW_CONNECT_BATTERY_UNKNOWN;

    if (s_cfg.on_battery != NULL) {
        level = s_cfg.on_battery();
        if (level > 100) {
            level = NOW_CONNECT_BATTERY_UNKNOWN;
        }
    }
    return tx_peer(NOW_CONNECT_MSG_BATTERY, &level, sizeof(level));
}

esp_err_t now_connect_send(uint8_t type, const void *data, size_t len)
{
    if (len > 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return tx_peer(type, data, len);
}

void now_connect_set_battery_cb(now_connect_battery_cb_t cb)
{
    s_cfg.on_battery = cb;
}

uint8_t now_connect_peer_battery(void)
{
    return s_peer_battery;
}

/* ---------------------------------------------------- report accessors --- */

void now_connect_report_clear(now_connect_report_t *report)
{
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }
}

/*
 * Two sliders share three bytes: the first takes bits 23-12 and the second
 * bits 11-0, so a pair is one 24-bit word with the lower-numbered slider on
 * top. Sliders 0 and 1 occupy the first three bytes, 2 and 3 the next.
 */
uint16_t now_connect_report_slider(const now_connect_report_t *report, int id)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return 0;
    }

    const uint8_t *pair = &report->sliders[(id / 2) * 3];
    if ((id & 1) == 0) {
        return (uint16_t)((pair[0] << 4) | (pair[1] >> 4));
    }
    return (uint16_t)(((pair[1] & 0x0F) << 8) | pair[2]);
}

void now_connect_report_set_slider(now_connect_report_t *report, int id, uint16_t value)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return;
    }
    if (value > NOW_CONNECT_SLIDER_MAX) {
        value = NOW_CONNECT_SLIDER_MAX;
    }

    uint8_t *pair = &report->sliders[(id / 2) * 3];
    if ((id & 1) == 0) {
        pair[0] = (uint8_t)(value >> 4);
        pair[1] = (uint8_t)((pair[1] & 0x0F) | ((value & 0x0F) << 4));
    } else {
        pair[1] = (uint8_t)((pair[1] & 0xF0) | ((value >> 8) & 0x0F));
        pair[2] = (uint8_t)(value & 0xFF);
    }
}

bool now_connect_report_touched(const now_connect_report_t *report, int id)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return false;
    }
    return (report->slider_flags & (1u << id)) != 0;
}

void now_connect_report_set_touched(now_connect_report_t *report, int id, bool touched)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return;
    }
    if (touched) {
        report->slider_flags |= (uint8_t)(1u << id);
    } else {
        report->slider_flags &= (uint8_t)~(1u << id);
    }
}

bool now_connect_report_commanded(const now_connect_report_t *report, int id)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return false;
    }
    return (report->slider_flags & (1u << (id + NOW_CONNECT_SLIDER_COUNT))) != 0;
}

void now_connect_report_set_commanded(now_connect_report_t *report, int id, bool commanded)
{
    if (report == NULL || id < 0 || id >= NOW_CONNECT_SLIDER_COUNT) {
        return;
    }
    uint8_t bit = (uint8_t)(1u << (id + NOW_CONNECT_SLIDER_COUNT));
    if (commanded) {
        report->slider_flags |= bit;
    } else {
        report->slider_flags &= (uint8_t)~bit;
    }
}

uint32_t now_connect_report_keys(const now_connect_report_t *report)
{
    if (report == NULL) {
        return 0;
    }
    uint32_t keys = (uint32_t)report->keys[0]
                  | ((uint32_t)report->keys[1] << 8)
                  | ((uint32_t)report->keys[2] << 16)
                  | ((uint32_t)report->keys[3] << 24);
    return keys & 0x3FFFFFFFu;   /* 30 keys, the top two bits are spare */
}

void now_connect_report_set_keys(now_connect_report_t *report, uint32_t keys)
{
    if (report == NULL) {
        return;
    }
    keys &= 0x3FFFFFFFu;
    report->keys[0] = (uint8_t)(keys);
    report->keys[1] = (uint8_t)(keys >> 8);
    report->keys[2] = (uint8_t)(keys >> 16);
    report->keys[3] = (uint8_t)(keys >> 24);
}

bool now_connect_report_key(const now_connect_report_t *report, int index)
{
    if (index < 0 || index >= NOW_CONNECT_KEY_COUNT) {
        return false;
    }
    return (now_connect_report_keys(report) & (1u << index)) != 0;
}

void now_connect_report_set_key(now_connect_report_t *report, int index, bool down)
{
    if (report == NULL || index < 0 || index >= NOW_CONNECT_KEY_COUNT) {
        return;
    }

    uint32_t keys = now_connect_report_keys(report);
    if (down) {
        keys |= (1u << index);
    } else {
        keys &= ~(1u << index);
    }
    now_connect_report_set_keys(report, keys);
}

/* One nibble per encoder: bit 0 turned, bit 1 pressed, bits 2-3 direction. */
void now_connect_report_encoder(const now_connect_report_t *report, int id,
                                now_connect_encoder_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (now_connect_encoder_t){0};

    if (report == NULL || id < 0 || id >= NOW_CONNECT_ENCODER_COUNT) {
        return;
    }

    uint8_t nibble = (report->encoders[id / 2] >> ((id & 1) ? 4 : 0)) & 0x0F;
    out->turned    = (nibble & 0x01) != 0;
    out->pressed   = (nibble & 0x02) != 0;
    out->direction = (now_connect_turn_t)((nibble >> 2) & 0x03);
}

void now_connect_report_set_encoder(now_connect_report_t *report, int id,
                                    const now_connect_encoder_t *encoder)
{
    if (report == NULL || encoder == NULL || id < 0 || id >= NOW_CONNECT_ENCODER_COUNT) {
        return;
    }

    uint8_t nibble = (uint8_t)((encoder->turned ? 0x01 : 0)
                             | (encoder->pressed ? 0x02 : 0)
                             | ((encoder->direction & 0x03) << 2));

    uint8_t *byte  = &report->encoders[id / 2];
    uint8_t  shift = (id & 1) ? 4 : 0;
    *byte = (uint8_t)((*byte & ~(0x0F << shift)) | (nibble << shift));
}
