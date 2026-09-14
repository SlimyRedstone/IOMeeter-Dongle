/*
 * Everything that carries device state off the board: the status LED, the USB
 * protocol and the ESP-NOW link, wired to each other.
 *
 * The role is the Kconfig choice under "NowConnect", and decides what gets
 * wired up:
 *
 *   master  Sits in the computer. Everything the slave reports is turned into
 *           JSON and pushed to the host over the vendor interface, and slider
 *           positions the host commands are passed back the other way.
 *
 *   slave   Carries the sliders, keys and encoders. It reports them over the
 *           radio, or straight to the computer when it is plugged in -- never
 *           both, or the master would forward everything twice.
 *
 * The status LED belongs to NowConnect on both devices, so there is one owner
 * for it: usb_proto's own status colours are switched off, and an explicit LED
 * command from the host still reaches the strip directly.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the LED, USB and the radio, and start the slave's loop.
 *
 * config_init() must have run first: colours and the stored pairing both come
 * out of config.json. Individual failures are logged and survived rather than
 * being fatal, so a device with no LED or no radio still enumerates.
 */
void link_start(void);

#ifdef __cplusplus
}
#endif
