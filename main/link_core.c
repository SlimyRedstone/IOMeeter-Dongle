#include "link_core.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "config.h"
#include "neopixel.h"
#include "now_connect.h"
#include "usb_proto.h"

#include "ui/vars.h"

static const char *TAG = "link_core";

#define LINK_TASK_STACK 3072
#define LINK_TASK_PRIO  3
#define LINK_POLL_MS    50

/* Longest of the small JSON events built below. */
#define EVENT_BUF_MAX 64

#ifdef CONFIG_NOW_CONNECT_ROLE_MASTER
#define ROLE_MASTER 1
#else
#define ROLE_MASTER 0
#endif

/* ------------------------------------------------------------ forwarding -- */

/* What the host was last told, so only movement is sent. */
static now_connect_report_t s_last_report;
static bool                 s_reported;

/*
 * The host hears about sliders one at a time, in the same shape it uses to
 * command them: {"set":{"slider":{"id":N,"value":V,"touch":T}}}.
 *
 * Only what actually moved goes out. A report carries all four sliders and
 * arrives whenever any one of them changes, so forwarding the set every time
 * would be three repeats out of four. Touch counts as movement in its own
 * right -- a finger landing on a slider that has not moved yet is exactly what
 * stops the host driving it.
 */
static void forward_report(const now_connect_report_t *report)
{
    for (int i = 0; i < NOW_CONNECT_SLIDER_COUNT; i++) {
        const uint16_t value   = now_connect_report_slider(report, i);
        const bool     touched = now_connect_report_touched(report, i);

        if (s_reported &&
            value == now_connect_report_slider(&s_last_report, i) &&
            touched == now_connect_report_touched(&s_last_report, i)) {
            continue;
        }
        usb_proto_send_slider(i, value, touched);
    }

    s_last_report = *report;
    s_reported    = true;
}

/* --------------------------------------------------------------- master -- */
#if ROLE_MASTER

/* "MAC: AA:BB:CC:DD:EE:FF\nBat: 100%" is 33 bytes; this leaves room. */
#define DEVICE_INFO_MAX 64

/*
 * Hands the screen what it shows about the paired device. Called again on
 * every battery report, so the percentage stays current rather than being
 * whatever it was at the moment the link came up.
 */
static void show_peer(void)
{
    uint8_t mac[NOW_CONNECT_MAC_LEN];
    char    mac_text[18] = "unknown";
    char    text[DEVICE_INFO_MAX];

    if (now_connect_peer_mac(mac)) {
        now_connect_mac_str(mac, mac_text, sizeof(mac_text));
    }

    const uint8_t battery = now_connect_peer_battery();
    if (battery <= 100) {
        snprintf(text, sizeof(text), "MAC:\n%s\nBat: %u%%", mac_text, (unsigned)battery);
    } else {
        /* NOW_CONNECT_BATTERY_UNKNOWN, until the peer's first battery frame
           arrives a moment after it connects, or forever if it has no gauge. */
        snprintf(text, sizeof(text), "MAC:\n%s\nBat: --%%", mac_text);
    }

    set_var_device_status_text(text);
    set_var_device_is_connected(true);
}

static void hide_peer(void)
{
    set_var_device_is_connected(false);
    set_var_device_status_text(NULL);
}

/*
 * Nothing is queued for a host that is not listening, and nothing is sent out
 * of a buffer snprintf had to truncate -- it returns the length it wanted, not
 * the one it wrote.
 */
static void send_event(const char *json, int len, size_t max)
{
    if (len <= 0 || (size_t)len >= max) {
        return;
    }
    if (usb_proto_vendor_mounted()) {
        usb_proto_send_event(json, (size_t)len);
    }
}

static void on_report(const now_connect_report_t *report)
{
    /* Nothing to tell a host that is not there, and dropping it also means the
       next host to arrive is sent every slider rather than only the movers. */
    if (!usb_proto_vendor_mounted()) {
        s_reported = false;
        return;
    }
    forward_report(report);
}

static void on_peer_battery(uint8_t level)
{
    if (now_connect_status() == NOW_CONNECT_STATUS_CONNECTED) {
        show_peer();   /* redraw with the level that just arrived */
    }

    char json[EVENT_BUF_MAX];
    int  len = snprintf(json, sizeof(json), "{\"pair\":{\"batt\":%u}}",
                        (unsigned)level);
    send_event(json, len, sizeof(json));
}

static void on_link_status(now_connect_status_t status)
{
    /* Connected is the only state with a device to describe; pairing, waiting
       and unpaired all leave the screen on its disconnected panel. */
    if (status == NOW_CONNECT_STATUS_CONNECTED) {
        show_peer();
    } else {
        hide_peer();
    }

    char json[EVENT_BUF_MAX];
    int  len = snprintf(json, sizeof(json), "{\"pair\":{\"status\":\"%s\"}}",
                        now_connect_status_name(status));
    send_event(json, len, sizeof(json));
}

/*
 * The host moved a slider. Record it, then hand the whole set to the slave
 * with the commanded bit set on that one, which is what tells the slave to
 * drive its motor: a slider the user is already holding stays where it is.
 *
 * A slave that is on USB, or not there at all, simply does not receive it. The
 * host is still answered with {"ok":true}, because the position was recorded.
 */
static esp_err_t on_slider_set(int id, int value)
{
    esp_err_t err = config_set_slider(id, value);
    if (err != ESP_OK) {
        return err;
    }

    now_connect_report_t report;
    now_connect_report_clear(&report);

    for (int i = 0; i < NOW_CONNECT_SLIDER_COUNT; i++) {
        now_connect_report_set_slider(&report, i, (uint16_t)config_get_slider(i));
    }
    now_connect_report_set_commanded(&report, id, true);

    now_connect_send_report(&report);
    return ESP_OK;
}

/* ---------------------------------------------------------------- slave -- */
#else

/*
 * Fill @p report from this device's own inputs.
 *
 * The sliders, key matrix and encoders have no drivers yet, so nothing is
 * sampled and nothing is sent -- the link still runs on its heartbeat and the
 * LED still shows its state. Return true once there is something real in it,
 * and the loop below takes care of both routes out.
 */
static bool sample_inputs(now_connect_report_t *report)
{
    now_connect_report_clear(report);
    return false;
}

/*
 * The slave's one loop. It decides which way its state leaves the device, and
 * that decision is the whole of the USB priority rule: while the cable is
 * live, NowConnect is silenced and the host is written to directly; when it is
 * pulled, the radio resumes on the next tick.
 */
static void link_task(void *arg)
{
    (void)arg;

    now_connect_report_t current, last;
    now_connect_report_clear(&last);
    bool primed = false;

    for (;;) {
        const bool on_usb = usb_proto_vendor_mounted();
        now_connect_set_usb_active(on_usb);

        if (sample_inputs(&current)) {
            if (!primed || memcmp(&current, &last, sizeof(current)) != 0) {
                primed = true;
                last   = current;

                if (on_usb) {
                    forward_report(&current);
                } else {
                    now_connect_send_report(&current);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LINK_POLL_MS));
    }
}

#endif /* ROLE_MASTER */

/* ----------------------------------------------------------- start-up --- */

static void start_usb(void)
{
    usb_proto_config_t cfg = USB_PROTO_DEFAULT_CONFIG();

    cfg.on_led_command = neopixel_set_rgb;
    cfg.on_led_query   = neopixel_get_rgb;
    cfg.on_config_get  = config_to_json;
    cfg.on_config_set  = config_from_json;
#if ROLE_MASTER
    cfg.on_slider_set  = on_slider_set;
    cfg.product        = "IOMeeter Dongle";
#else
    cfg.on_slider_set  = config_set_slider;
    cfg.product        = "IOMeeter Surface";
#endif
    cfg.manufacturer = "SlimyRedstone";

    /* NowConnect owns the LED; see the note at the top. */
    cfg.status_led = false;

    esp_err_t err = usb_proto_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_proto_start: %s", esp_err_to_name(err));
    }
}

/* Type and pins are the Kconfig ones: APA102 on GPIO40/39 for this board. */
static void start_led(void)
{
    const neopixel_config_t led = NEOPIXEL_DEFAULT_CONFIG();

    esp_err_t err = neopixel_init(&led);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status LED unavailable: %s", esp_err_to_name(err));
        return;
    }
    neopixel_set_brightness(config_led_brightness());
}

static void start_now_connect(void)
{
    now_connect_config_t cfg = NOW_CONNECT_DEFAULT_CONFIG();

    now_connect_load_colors(&cfg);
    cfg.on_led = neopixel_set_rgb;

#if ROLE_MASTER
    cfg.on_report       = on_report;
    cfg.on_peer_battery = on_peer_battery;
    cfg.on_status       = on_link_status;
#else
    /* cfg.on_battery = read_battery; once the gauge has a driver. Until then
       NOW_CONNECT_BATTERY_UNKNOWN is what the master is told. */
#endif

    esp_err_t err = now_connect_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "now_connect_start: %s", esp_err_to_name(err));
    }
}

void link_start(void)
{
    start_led();
    start_usb();
    start_now_connect();

#if !ROLE_MASTER
    if (xTaskCreate(link_task, "link", LINK_TASK_STACK, NULL,
                    LINK_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot start the link task");
    }
#endif

#if CONFIG_NOW_CONNECT_PAIR_GPIO >= 0
    ESP_LOGI(TAG, "hold the button on GPIO%d for %dms to pair",
             CONFIG_NOW_CONNECT_PAIR_GPIO, CONFIG_NOW_CONNECT_PAIR_HOLD_MS);
#endif
}
