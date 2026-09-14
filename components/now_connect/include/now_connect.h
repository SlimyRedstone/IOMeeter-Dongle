/*
 * NowConnect -- the ESP-NOW link between the dongle and the control surface.
 *
 * One master (the device plugged into the computer) and one slave (the device
 * carrying the sliders, keys and encoders) pair once, remember each other's MAC
 * address in config.json, and then exchange fixed-layout binary reports. The
 * role is a build-time choice, CONFIG_NOW_CONNECT_ROLE_MASTER or _SLAVE.
 *
 * Pairing
 *   Holding the pairing button (GPIO0 by default) for three seconds on both
 *   devices opens a pairing window on each. While it is open they broadcast a
 *   request carrying their role; a request from the opposite role is answered,
 *   both addresses are written to config.json, and the window closes. The same
 *   thing happens without a button through now_connect_start_pairing().
 *
 * Wire format
 *   Every frame is a four-byte header followed by a payload. ESP-NOW caps a
 *   frame at 250 bytes, so nothing here is JSON: a full device report is 14
 *   bytes, and turning it into whatever the host wants to read is the caller's
 *   job on the way out over USB.
 *
 *     sliders[6]     four 12-bit positions, packed in pairs. Each pair is three
 *                    bytes, the first slider in the top 12 bits and the second
 *                    in the bottom 12, sliders 0 and 1 first.
 *     slider_flags   bits 0-3, slider is being touched;
 *                    bits 4-7, slider is being commanded by the computer, so
 *                    that an untouched slider is the only kind that moves.
 *     keys[4]        the key matrix, one bit per key, 0 to 29.
 *     profile        active profile id, 0 to 255.
 *     encoders[2]    four nibbles, one per encoder: bit 0 turned, bit 1
 *                    pressed, bits 2-3 direction (now_connect_turn_t).
 *
 *   The battery level is a message of its own rather than a field of the
 *   report, because it goes out once every pair.battery_min minutes instead of
 *   on every change. now_connect_set_battery_cb() supplies the value;
 *   NOW_CONNECT_BATTERY_UNKNOWN goes out when nothing does.
 *
 * USB takes priority
 *   A slave that is plugged in talks to the computer directly, so it must not
 *   also report over the radio -- the master would forward everything twice.
 *   now_connect_set_usb_active(true) silences every transmission from that
 *   moment; passing false resumes immediately, without re-pairing or
 *   re-initialising the radio, so a cable being pulled out costs one heartbeat.
 *
 * Status
 *   NO_CONFIG, PAIRING, DISCONNECTED and CONNECTED each have a colour in
 *   config.json under pair.led, and the on_led callback shows it --
 *   neopixel_set_rgb() can be handed to it directly. How it shows it says as
 *   much as the colour does:
 *
 *     pairing    the colour fades in and out at 0.5Hz, one breath every two
 *                seconds, so an open window is obvious from across a room
 *     otherwise  the colour is held steady, and every frame carrying real data
 *                -- reports, battery, anything of the caller's own, but not
 *                heartbeats -- blinks it off and on twice
 *
 *   The callback is only invoked when the colour actually changes, so a
 *   resting LED costs one call and a fade costs one per 50ms tick.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- layout --- */

#define NOW_CONNECT_MAC_LEN       6
#define NOW_CONNECT_SLIDER_COUNT  4
#define NOW_CONNECT_ENCODER_COUNT 4
#define NOW_CONNECT_KEY_COUNT     30

/** Largest slider position: they are 12 bits on the wire. */
#define NOW_CONNECT_SLIDER_MAX 4095

/** Reported when no battery callback is registered. */
#define NOW_CONNECT_BATTERY_UNKNOWN 254

/** Largest payload one frame carries, after the header. */
#define NOW_CONNECT_PAYLOAD_MAX 200

typedef enum {
    NOW_CONNECT_ROLE_MASTER = 0,  /*!< Plugged into the computer */
    NOW_CONNECT_ROLE_SLAVE  = 1,  /*!< Reports to the master     */
} now_connect_role_t;

typedef enum {
    NOW_CONNECT_STATUS_NO_CONFIG = 0, /*!< No peer stored; needs pairing  */
    NOW_CONNECT_STATUS_PAIRING,       /*!< Pairing window is open         */
    NOW_CONNECT_STATUS_DISCONNECTED,  /*!< Paired, but the peer is quiet  */
    NOW_CONNECT_STATUS_CONNECTED,     /*!< Peer heard from recently       */
    NOW_CONNECT_STATUS_MAX,
} now_connect_status_t;

/** Frame types. 0x40 and above are free for callers of now_connect_send(). */
typedef enum {
    NOW_CONNECT_MSG_PAIR_REQ  = 0x01, /*!< Broadcast while pairing        */
    NOW_CONNECT_MSG_PAIR_ACK  = 0x02, /*!< Answer to a request            */
    NOW_CONNECT_MSG_HEARTBEAT = 0x03, /*!< Keeps the link marked up       */
    NOW_CONNECT_MSG_REPORT    = 0x10, /*!< now_connect_report_t           */
    NOW_CONNECT_MSG_BATTERY   = 0x11, /*!< One byte, 0-100 or 254         */
    NOW_CONNECT_MSG_USER      = 0x40, /*!< First type reserved to callers */
} now_connect_msg_t;

/** Which way an encoder moved, in the two direction bits of its nibble. */
typedef enum {
    NOW_CONNECT_TURN_NONE = 0,
    NOW_CONNECT_TURN_CW   = 1,
    NOW_CONNECT_TURN_CCW  = 2,
} now_connect_turn_t;

/** One encoder, unpacked. */
typedef struct {
    bool               turned;   /*!< Moved since the last report */
    bool               pressed;  /*!< Shaft switch is down        */
    now_connect_turn_t direction;
} now_connect_encoder_t;

/**
 * @brief The device's whole state in 14 bytes. See the field notes at the top.
 *
 * Packed and made of bytes only, so the two ends agree on the layout whatever
 * the compiler does. Use the accessors below rather than touching the members.
 */
typedef struct __attribute__((packed)) {
    uint8_t sliders[6];
    uint8_t slider_flags;
    uint8_t keys[4];
    uint8_t profile;
    uint8_t encoders[2];
} now_connect_report_t;

_Static_assert(sizeof(now_connect_report_t) == 14, "report must stay 14 bytes");

/* ---------------------------------------------------------- callbacks --- */

/**
 * @brief A frame arrived from the paired peer.
 *
 * Runs on the component's own task. Frames from anything other than the paired
 * peer never reach it, and neither do the pairing and heartbeat frames the
 * component handles itself.
 */
typedef void (*now_connect_packet_cb_t)(uint8_t type, const uint8_t *data, size_t len);

/** @brief A device report arrived. Called before on_packet. */
typedef void (*now_connect_report_cb_t)(const now_connect_report_t *report);

/** @brief The peer's battery level arrived, 0-100 or NOW_CONNECT_BATTERY_UNKNOWN. */
typedef void (*now_connect_peer_battery_cb_t)(uint8_t level);

/** @brief Supplies this device's battery level, 0-100. */
typedef uint8_t (*now_connect_battery_cb_t)(void);

/** @brief Shows a link state. Matches neopixel_set_rgb(). @p rgb is 0xRRGGBB. */
typedef esp_err_t (*now_connect_led_cb_t)(uint32_t rgb);

/** @brief The link state changed. */
typedef void (*now_connect_status_cb_t)(now_connect_status_t status);

typedef struct {
    now_connect_role_t role;  /*!< Defaults to the Kconfig choice */

    /* Pairing button. A gpio of -1 leaves it out, and pairing is then only
       opened by now_connect_start_pairing(). */
    int      pair_gpio;
    bool     pair_active_high;
    uint32_t pair_hold_ms;    /*!< How long it has to be held     */
    uint32_t pair_window_ms;  /*!< How long the window stays open */

    uint8_t channel;          /*!< Must match on both devices */

    /*
     * How often the battery level goes out, in minutes. 0 takes the value from
     * config.json, which is where it belongs; anything else overrides it.
     */
    uint8_t battery_minutes;

    /* Link state colours, packed 0xRRGGBB. now_connect_load_colors() fills
       these from config.json. */
    uint32_t led_connected;
    uint32_t led_disconnected;
    uint32_t led_pairing;
    uint32_t led_no_config;

    now_connect_led_cb_t          on_led;          /*!< NULL: no LED        */
    now_connect_packet_cb_t       on_packet;       /*!< NULL: frame dropped */
    now_connect_report_cb_t       on_report;       /*!< NULL: ignored       */
    now_connect_peer_battery_cb_t on_peer_battery; /*!< NULL: ignored       */
    now_connect_battery_cb_t      on_battery;      /*!< NULL: sends 254     */
    now_connect_status_cb_t       on_status;       /*!< NULL: ignored       */
} now_connect_config_t;

#ifdef CONFIG_NOW_CONNECT_ROLE_MASTER
#define NOW_CONNECT_KCONFIG_ROLE NOW_CONNECT_ROLE_MASTER
#else
#define NOW_CONNECT_KCONFIG_ROLE NOW_CONNECT_ROLE_SLAVE
#endif

#ifdef CONFIG_NOW_CONNECT_PAIR_ACTIVE_HIGH
#define NOW_CONNECT_KCONFIG_ACTIVE_HIGH true
#else
#define NOW_CONNECT_KCONFIG_ACTIVE_HIGH false
#endif

#define NOW_CONNECT_DEFAULT_CONFIG() ((now_connect_config_t){         \
    .role             = NOW_CONNECT_KCONFIG_ROLE,                     \
    .pair_gpio        = CONFIG_NOW_CONNECT_PAIR_GPIO,                 \
    .pair_active_high = NOW_CONNECT_KCONFIG_ACTIVE_HIGH,              \
    .pair_hold_ms     = CONFIG_NOW_CONNECT_PAIR_HOLD_MS,              \
    .pair_window_ms   = CONFIG_NOW_CONNECT_PAIR_WINDOW_S * 1000U,     \
    .channel          = CONFIG_NOW_CONNECT_CHANNEL,                   \
    .battery_minutes  = 0,                                            \
    .led_connected    = 0x00FF00,                                     \
    .led_disconnected = 0x3FFF00,                                     \
    .led_pairing      = 0x003FFF,                                     \
    .led_no_config    = 0xFFFF00,                                     \
})

/* --------------------------------------------------------- public API --- */

/**
 * @brief Bring up Wi-Fi and ESP-NOW, restore the pairing and start the task.
 *
 * config_init() must have run first: the stored peer address comes from
 * config.json, and pairing writes back to it. With no peer stored the status
 * starts at NO_CONFIG and only pairing moves it on.
 */
esp_err_t now_connect_start(const now_connect_config_t *config);

/**
 * @brief Stop the task and release the radio. The stored pairing survives.
 */
esp_err_t now_connect_stop(void);

/**
 * @brief Fill the four colour fields from config.json.
 *
 * Convenience for the caller that keeps its colours in the file rather than in
 * the firmware; call it on a NOW_CONNECT_DEFAULT_CONFIG() before starting.
 */
void now_connect_load_colors(now_connect_config_t *config);

/**
 * @brief Open the pairing window.
 *
 * What the button does after its hold time, exposed so a host command or a menu
 * entry can do the same. While the window is open the device broadcasts its
 * role a few times a second and answers the first request from the opposite
 * role. Any previous pairing is kept until a new peer answers.
 */
esp_err_t now_connect_start_pairing(void);

/**
 * @brief Close the pairing window early, keeping whatever pairing was stored.
 */
esp_err_t now_connect_stop_pairing(void);

/**
 * @brief Forget the peer and persist that, leaving the device at NO_CONFIG.
 */
esp_err_t now_connect_forget(void);

now_connect_status_t now_connect_status(void);
now_connect_role_t   now_connect_role(void);

/** @brief Human-readable status, for logs and JSON. */
const char *now_connect_status_name(now_connect_status_t status);

/** @brief True once a peer address is known, connected or not. */
bool now_connect_is_paired(void);

/** @brief The peer's address. Returns false when there is no peer. */
bool now_connect_peer_mac(uint8_t out[NOW_CONNECT_MAC_LEN]);

/** @brief This device's own address, available after now_connect_start(). */
bool now_connect_self_mac(uint8_t out[NOW_CONNECT_MAC_LEN]);

/** @brief Render a MAC as "AA:BB:CC:DD:EE:FF". @p max should be at least 18. */
void now_connect_mac_str(const uint8_t mac[NOW_CONNECT_MAC_LEN], char *out, size_t max);

/**
 * @brief Hand the link over to USB, or take it back.
 *
 * With @p active true nothing is transmitted at all, so a slave on a cable
 * cannot duplicate what it is already reporting over USB. The radio stays up
 * and the pairing stays valid, so passing false resumes on the next heartbeat.
 * Has no effect on a master, which is on a cable by definition.
 */
esp_err_t now_connect_set_usb_active(bool active);

bool now_connect_usb_active(void);

/**
 * @brief Send a device report to the peer.
 *
 * This and the two below are callable from any task: ESP-NOW serialises
 * transmission itself, and the component's own task is just another caller.
 */
esp_err_t now_connect_send_report(const now_connect_report_t *report);

/**
 * @brief Send the battery level now, rather than waiting for the interval.
 */
esp_err_t now_connect_send_battery(void);

/**
 * @brief Send an arbitrary payload to the peer.
 *
 * @param type NOW_CONNECT_MSG_USER or above for anything of the caller's own.
 * @param len  At most NOW_CONNECT_PAYLOAD_MAX.
 */
esp_err_t now_connect_send(uint8_t type, const void *data, size_t len);

/**
 * @brief Replace the battery level source after start-up.
 */
void now_connect_set_battery_cb(now_connect_battery_cb_t cb);

/**
 * @brief The peer's last reported battery level, or NOW_CONNECT_BATTERY_UNKNOWN.
 */
uint8_t now_connect_peer_battery(void);

/* ---------------------------------------------------- report accessors --- */

/** @brief Clear every field. */
void now_connect_report_clear(now_connect_report_t *report);

/** @brief Position of slider @p id, 0 - 4095. */
uint16_t now_connect_report_slider(const now_connect_report_t *report, int id);

/** @brief Set slider @p id. Values above 4095 are clamped. */
void now_connect_report_set_slider(now_connect_report_t *report, int id, uint16_t value);

bool now_connect_report_touched(const now_connect_report_t *report, int id);
void now_connect_report_set_touched(now_connect_report_t *report, int id, bool touched);

bool now_connect_report_commanded(const now_connect_report_t *report, int id);
void now_connect_report_set_commanded(now_connect_report_t *report, int id, bool commanded);

/** @brief State of key @p index, 0 - 29. */
bool now_connect_report_key(const now_connect_report_t *report, int index);
void now_connect_report_set_key(now_connect_report_t *report, int index, bool down);

/** @brief All 30 keys at once, bit 0 being key 0. */
uint32_t now_connect_report_keys(const now_connect_report_t *report);
void now_connect_report_set_keys(now_connect_report_t *report, uint32_t keys);

void now_connect_report_encoder(const now_connect_report_t *report, int id,
                                now_connect_encoder_t *out);
void now_connect_report_set_encoder(now_connect_report_t *report, int id,
                                    const now_connect_encoder_t *encoder);

#ifdef __cplusplus
}
#endif
