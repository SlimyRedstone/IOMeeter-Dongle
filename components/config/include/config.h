/*
 * JSON configuration stored on a SPIFFS volume.
 *
 * File layout, at /config.json inside the volume:
 *
 *   {"led":{"on_boot":"FF0000","on_connected":"00FF00","on_receive":"FF00FF",
 *           "on_absent":"FF6000","brightness":8},
 *    "pair":{"master":"AA:BB:CC:DD:EE:FF","slave":"","this":"AA:BB:CC:DD:EE:FF",
 *            "led":{"connected":"00FF00","disconnected":"3FFF00",
 *                   "pairing":"003FFF","no_config":"FFFF00"},
 *            "battery_min":1}}
 *
 * The "pair" object is the ESP-NOW link's own state, written by the
 * now_connect component: the two peers' MAC addresses, this device's own
 * address, the colours of the four link states and how often the battery level
 * is reported. An empty or absent MAC string means "not paired yet".
 *
 * Colours are RRGGBB hex strings at full scale; a leading '#' is accepted when
 * reading and omitted when writing. "brightness" is 0-255 and dims all of them
 * together. Missing keys fall back to the defaults above, so a truncated or
 * hand-edited file still yields a usable configuration.
 *
 * The volume lives in the "storage" partition declared by partitions.csv and is
 * mounted at /spiffs, so the file's full VFS path is /spiffs/config.json.
 * config_file_path() returns it. If the partition is empty or the file is
 * missing or unparseable, defaults are written out on first boot.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED colour slots, one per device state.
 *
 * Deliberately not prefixed CONFIG_, which is reserved for Kconfig macros.
 */
typedef enum {
    LED_STATE_BOOT = 0,   /*!< Booting; USB not ready yet          */
    LED_STATE_CONNECTED,  /*!< Host has configured the device      */
    LED_STATE_RECEIVE,    /*!< A packet is arriving over USB       */
    LED_STATE_ABSENT,     /*!< No host on the other end of the cable */
    LED_STATE_MAX,
} config_led_state_t;

/** Number of sliders tracked on the device. */
#define CONFIG_SLIDER_COUNT 4

/**
 * @brief Record a slider position received from the host.
 *
 * Signature matches usb_proto_slider_set_cb_t, so it can be wired straight to
 * usb_proto_config_t::on_slider_set.
 *
 * @param id    Slider index, 0..CONFIG_SLIDER_COUNT-1.
 * @param value New position.
 */
esp_err_t config_set_slider(int id, int value);

/**
 * @brief Last recorded position of a slider, or 0 for an unknown index.
 */
int config_get_slider(int id);

/**
 * @brief Mount the SPIFFS volume and load the configuration.
 *
 * Formats the partition if it cannot be mounted, and writes the default file
 * when none exists. Safe to call once at start-up, before anything reads a
 * colour.
 */
esp_err_t config_init(void);

/**
 * @brief Unmount the volume. Unsaved changes are lost.
 */
esp_err_t config_deinit(void);

/**
 * @brief Colour for @p state as packed 0xRRGGBB.
 *
 * Returns the compiled-in default for an out-of-range state or before
 * config_init() has run, so callers never need to check for failure.
 */
uint32_t config_led_color(config_led_state_t state);

/**
 * @brief Change a colour in memory. Call config_save() to persist it.
 */
esp_err_t config_set_led_color(config_led_state_t state, uint32_t rgb);

/**
 * @brief Global LED brightness, 0 - 255.
 *
 * Colours are stored at full scale and dimmed by this one number, so it is the
 * single place overall LED intensity is decided. Hand it to
 * neopixel_set_brightness().
 */
uint8_t config_led_brightness(void);

/**
 * @brief Change the brightness in memory. Call config_save() to persist it.
 */
esp_err_t config_set_led_brightness(uint8_t level);

/**
 * @brief Write the current configuration back to the file.
 */
esp_err_t config_save(void);

/**
 * @brief Re-read the file, discarding in-memory changes.
 */
esp_err_t config_reload(void);

/**
 * @brief Full VFS path of the configuration file, e.g. "/spiffs/config.json".
 */
const char *config_file_path(void);

/* ------------------------------------------------------ ESP-NOW pairing -- */

/** Length of a MAC address, in bytes. */
#define CONFIG_PAIR_MAC_LEN 6

/** Longest "AA:BB:CC:DD:EE:FF" rendering, including the terminator. */
#define CONFIG_PAIR_MAC_STR_LEN 18

/** The three addresses kept under "pair". */
typedef enum {
    PAIR_MAC_MASTER = 0,  /*!< The device plugged into the computer */
    PAIR_MAC_SLAVE,       /*!< The device linked to it over ESP-NOW  */
    PAIR_MAC_THIS,        /*!< This device's own address             */
    PAIR_MAC_MAX,
} config_pair_mac_t;

/** Link state colours, kept under "pair.led". */
typedef enum {
    PAIR_LED_CONNECTED = 0,  /*!< Peer is answering                    */
    PAIR_LED_DISCONNECTED,   /*!< Paired, but the peer has gone quiet   */
    PAIR_LED_PAIRING,        /*!< Pairing window is open                */
    PAIR_LED_NO_CONFIG,      /*!< No peer stored; needs pairing         */
    PAIR_LED_MAX,
} config_pair_led_t;

/**
 * @brief Read a stored MAC address.
 *
 * @param slot Which address to read.
 * @param out  Receives the six bytes; untouched when none is stored.
 * @return true if @p slot holds an address.
 */
bool config_pair_mac(config_pair_mac_t slot, uint8_t out[CONFIG_PAIR_MAC_LEN]);

/**
 * @brief Store a MAC address in memory. Call config_save() to persist it.
 *
 * @param mac Six bytes, or NULL to clear the slot.
 */
esp_err_t config_set_pair_mac(config_pair_mac_t slot,
                              const uint8_t mac[CONFIG_PAIR_MAC_LEN]);

/**
 * @brief Colour for a link state as packed 0xRRGGBB.
 *
 * Like config_led_color(), returns the compiled-in default before
 * config_init() has run, so callers never need to check for failure.
 */
uint32_t config_pair_led_color(config_pair_led_t slot);

/**
 * @brief Change a link state colour in memory. Call config_save() to persist.
 */
esp_err_t config_set_pair_led_color(config_pair_led_t slot, uint32_t rgb);

/**
 * @brief How often the battery level is reported over the link, in minutes.
 */
uint8_t config_pair_battery_minutes(void);

/**
 * @brief Change the battery reporting interval. 0 is rejected.
 */
esp_err_t config_set_pair_battery_minutes(uint8_t minutes);

/**
 * @brief Serialise the current configuration to a JSON object string.
 *
 * Same shape as the file on disk. The caller owns the result and must release
 * it with free(). Returns NULL on allocation failure.
 */
char *config_to_json(void);

/**
 * @brief Merge a JSON object into the configuration and persist it.
 *
 * Accepts the same shape as the file. Keys that are absent keep their current
 * value, so a partial object such as {"led":{"on_boot":"112233"}} is a valid
 * update. Returns ESP_ERR_INVALID_ARG if @p json is not a JSON object or holds
 * no recognised key.
 */
esp_err_t config_from_json(const char *json);

#ifdef __cplusplus
}
#endif
