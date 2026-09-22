# T-Watch 2020 V1

This tree’s default PlatformIO environment is `t-watch2020-v1`. The build flag is `-D LILYGO_WATCH_2020_V1`, and `src/config.h` names that target `T-Watch2020V1`.

The connected watch that this firmware was flashed to reports:

| | |
|---|---|
| Chip | ESP32-D0WDQ6-V3, revision 3 |
| Flash | 16 MB (read by esptool) |
| MAC | `b8:f0:09:c0:e9:f0` |
| USB serial | Silicon Labs CP2104, `0215A3AE` |
| Firmware string in this tree | `2023091101` (`__FIRMWARE__` in `src/config.h`) |

Pin names and the parts list below come from LilyGO’s TTGO T-Watch library, [library version 1.4.2](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library), file `src/board/twatch2020_v1.h` and [docs/watch_2020_v1.md](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library/blob/master/docs/watch_2020_v1.md). This project pulls the same pin header through the `lvgl_update` fork of that library.

## What this version has

T-Watch 2020 V1 is the square 1.54 inch watch. It has no GPS and no microphone. The side button is the AXP202 power key, not an ESP32 GPIO. This firmware treats a short press as the power button and a long press as the quick-settings bar.

| Part | Role | Bus / address |
|---|---|---|
| ESP32-D0WDQ6 | CPU, Wi-Fi, Bluetooth | — |
| PSRAM | 8 MB, as listed by LilyGO | — |
| ST7789 | 240×240 display | SPI, write only |
| FT6336 | Touch controller | Its own I2C bus, address `0x38` |
| AXP202 | Battery, charging, backlight power, audio power, power button | Sensor I2C, address `0x35` |
| BMA423 | Accelerometer and step counter | Sensor I2C, address `0x18` |
| PCF8563 | Real-time clock | Sensor I2C, address `0x51` |
| MAX98357A | Speaker amplifier | I2S |
| Vibration motor | Haptic, GPIO on/off | GPIO 4 |
| Infrared LED | IR transmit | GPIO 13 |

V2 is the version with a Quectel L76K GPS and a DRV2605 haptic driver. V3 is the version with an SPM1423 microphone. Those parts are not on V1.

## Pins

GPIO numbers are the `TWATCH_*` macros in `twatch2020_v1.h`. A value of `-1` or `N/A` means that signal is not wired.

### Display (ST7789, SPI)

| Signal | GPIO |
|---|---|
| MOSI | 19 |
| SCLK | 18 |
| CS | 5 |
| DC | 27 |
| Backlight | 12 |
| MISO | not connected |
| RST | not connected |

Backlight power is AXP202 LDO2. GPIO 12 is the backlight enable.

### Touch (FT6336, separate I2C bus)

| Signal | GPIO |
|---|---|
| SDA | 23 |
| SCL | 32 |
| Interrupt | 38 |

### Sensors (I2C bus shared by AXP202, BMA423, and PCF8563)

| Signal | GPIO |
|---|---|
| SDA | 21 |
| SCL | 22 |
| BMA423 interrupt | 39 |
| PCF8563 interrupt | 37 |
| AXP202 interrupt | 35 |

### Audio, motor, infrared

| Signal | GPIO |
|---|---|
| I2S BCK | 26 |
| I2S WS | 25 |
| I2S DOUT | 33 |
| Motor | 4 |
| IR send | 13 |

## AXP202 power rails

From LilyGO’s V1 power table. DC3 feeds the ESP32 and must stay on. On V1, LDO2 is the backlight and LDO3 is the audio amplifier. DC2, LDO4, and EXTEN are unused. This firmware leaves LDO2 under the display driver; the library’s V1 `powerOff()` does not turn LDO2 off.

| Rail | Use on V1 |
|---|---|
| DC2 | unused |
| DC3 | ESP32 |
| LDO1 | always on, not software controlled |
| LDO2 | backlight |
| LDO3 | audio |
| LDO4 | unused |
| EXTEN | unused |

## Build

```bash
pio run -e t-watch2020-v1
```

The environment is `[env:t-watch2020-v1]` in `platformio.ini`: board `ttgo-t-watch`, 16 MB flash, 80 MHz QIO. Upload only to the watch’s CP2104 serial port. Other USB serial devices on the same machine are not this watch.
