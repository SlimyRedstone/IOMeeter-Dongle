# S3-IOMeeter Dongle

Firmware for the IOMeeter control surface, built around the [LilyGO T-Dongle S3 Plus](https://lilygo.cc/products/t-dongle-s3).

IOMeeter is a physical mixer for a computer: motorised sliders, a key matrix and rotary
encoders that drive per-application audio levels on the desktop.

The **master** is the dongle that lives in a USB port on the computer. It presents itself as
a composite USB device, listens on an ESP-NOW radio link, and translates what the control
surface reports into messages the desktop client understands.

The **slave** is the control surface itself: battery powered, carrying the sliders, keys and
encoders, reporting them to the master over the radio. Plugged into a computer directly it
talks to the desktop client itself and goes silent on the radio, so the master never
forwards a duplicate of something the computer already has.

---

## Versions

| Item | Value |
|---|---|
| Project version | 0.2.0 |
| ESP-IDF | v6.0.1 (requires 6.0 or newer) |
| Target | ESP32-S3 |
| Board | [LilyGO T-Dongle S3 Plus](https://lilygo.cc/products/t-dongle-s3), 16 MB flash |

### Managed dependencies

| Component | Version |
|---|---|
| lvgl/lvgl | 9.4.0 |
| espressif/esp_tinyusb | 2.2.1 |
| espressif/tinyusb | 0.19.0~3 |
| espressif/led_strip | 3.0.3 |
| espressif/cjson | 1.7.19~2 |

cJSON and LVGL are pulled from the component registry rather than the IDF tree; cJSON left
IDF core in v6.0, and the display needs LVGL 9 specifically because the generated UI is
written against that API.

---

## Firmware layout

| Component | Responsibility |
|---|---|
| `now_connect` | The ESP-NOW link: pairing, peer identity, link state, the binary report format |
| `usb_proto` | The composite USB device, its descriptors, and the JSON command protocol on top |
| `display` | SPI bus, ST7735 controller, orientation and backlight. Panel only, no drawing |
| `neopixel` | The addressable status LED, in either of the two wire protocols the hardware might use |
| `config` | The JSON configuration file on a SPIFFS volume |

The application itself is three files. One brings up the configuration and starts the other
two. `display_core` owns the screen: it starts the panel, registers LVGL against it, runs
the LVGL loop on its own task pinned to the second core, and implements the variables the
generated UI reads. `link_core` owns everything that carries device state off the board: the
status LED, the USB protocol and the radio, wired to each other.

---

## The radio link

ESP-NOW caps a frame at 250 bytes, so nothing on the radio is JSON. A complete device report
packs into fourteen bytes behind a four-byte header:

* four 12-bit slider positions
* a touch flag and a commanded flag for each of those sliders
* a thirty-key matrix, one bit per key
* the active profile
* four rotary encoders, each with whether it turned, whether it is pressed, and which way
  it was turned

The battery level travels as a message of its own, once a minute by default, rather than
riding along on every report.

### Pairing

Holding the button on both devices for three seconds opens a pairing window on each. While
it is open they broadcast their role, answer the first request that comes back from the
opposite role, and write both addresses into the configuration file. Two devices of the same
role ignore each other. A device that starts with no stored peer says so on its LED and
waits to be paired.

### USB takes priority

A slave on a cable stops transmitting entirely, apart from pairing, which a user may well be
doing with the cable plugged in. The radio and the pairing both stay live, so unplugging
resumes the link within one heartbeat rather than a re-initialisation.

---

## The USB protocol

The master enumerates as a composite device: a CDC-ACM serial port for logs, and a
vendor-specific interface carrying the real protocol on raw bulk endpoints. The descriptors
include the BOS and MS OS 2.0 blocks that bind WinUSB on Windows without a driver install.

The identity is Espressif's vendor ID with product ID 0x6902.
