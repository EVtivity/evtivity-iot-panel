# Arduino IDE Setup and Build

Step-by-step setup to compile and flash the panel firmware in the Arduino IDE for the Waveshare
ESP32-S3-Touch-LCD-7B (1024x600). Follow the sections in order. The board settings and library versions
here are the ones verified against this board. The 7B display is driven by Waveshare's own 7B drivers,
bundled in the sketch; see [HARDWARE-7B.md](HARDWARE-7B.md).

## 1. Install the Arduino IDE and the ESP32 core

1. Install the Arduino IDE 2.x from https://www.arduino.cc/en/software.
2. Open Preferences (Arduino IDE menu, Settings on macOS).
3. In "Additional boards manager URLs" add:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
4. Open Tools > Board > Boards Manager, search `esp32`, and install "esp32 by Espressif Systems"
   version 3.0.0 or newer. This package provides the ESP32-S3 toolchain plus the bundled `WiFi`,
   `HTTPClient`, `WiFiClientSecure`, `Preferences`, `WebServer`, and `DNSServer` libraries.

## 2. Install the libraries

Open Tools > Manage Libraries and install:

| Library | Version | Author | Notes |
| --- | --- | --- | --- |
| `lvgl` | 9.x (9.4 or newer) | LVGL | GUI framework. Do not use the 8.x line. |
| `ESP32_Display_Panel` | 1.0.4 or newer | esp-arduino-libs | Display, touch, IO-expander driver for this board family. |
| `ESP32_IO_Expander` | latest | esp-arduino-libs | CH422G support. Install if the Library Manager does not pull it automatically with the panel driver. |
| `ArduinoJson` | 7.x | Benoit Blanchon | CSMS JSON parsing. |

Accept the prompt to install dependencies when the Library Manager offers it. `ESP32_Display_Panel`
also depends on `esp-lib-utils`, which installs the same way.

### LVGL version and the display driver (important)

`ESP32_Display_Panel` 1.0.4 ships its LVGL port and examples for **LVGL v8** (`template_files/lvgl_v8_port.cpp`,
`examples/.../lvgl_v8/...`). It does not include a v9 port. There are two supported paths:

- **LVGL 8.3 (recommended for a first bring-up).** Install `lvgl@8.3.11`, copy the library's
  `template_files/lvgl_v8_port.cpp` and `lvgl_v8_port.h` into the sketch, and call `lvgl_port_init(...)`.
  This is the path Waveshare's own Arduino demo uses, so it compiles and runs with no custom glue.
- **LVGL 9.x.** Keep `lvgl@9.4`, but write a small custom v9 display/touch port (the v8 port adapted to
  `lv_display_*` / `lv_indev_*` / `lv_tick_set_cb` and the RGB anti-tearing path). The library does not
  bless this, so it needs validation on hardware.

The portable UI in `sim/screens.c` is LVGL v9. Choose the path before writing the firmware board layer.

### Verified on this machine (2026-06-11)

Installed and confirmed with `arduino-cli` 1.5.1: `esp32:esp32` 3.3.x, `lvgl` 9.4.0,
`ESP32_Display_Panel` 1.0.4, `ESP32_IO_Expander` 1.1.1, `ArduinoJson` 7.4.3, `esp-lib-utils` 0.2.3. A
WiFi + HTTPClient + Preferences + ArduinoJson sketch compiles for this board at 27% of the 3 MB
partition using this FQBN:

```
esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M
```

## 3. Configure LVGL (lv_conf.h)

LVGL reads a configuration file named `lv_conf.h` placed one level above the `lvgl` library folder.

1. Copy `lv_conf.h` from this repo's `sim/` folder to your Arduino libraries directory, next to the
   `lvgl` folder:
   ```
   ~/Documents/Arduino/libraries/lv_conf.h          (macOS / Linux)
   Documents\Arduino\libraries\lv_conf.h            (Windows)
   ```
   The repo's `sim/lv_conf.h` already has the right settings: content enabled, 32-bit color, and the
   fonts the UI uses.
2. Confirm these values in that file:
   - The top guard is `#if 1` (content enabled).
   - `LV_COLOR_DEPTH 16` for the device (the RGB panel is RGB565). The simulator uses 32; set 16 for
     hardware. This is the one value that differs between the simulator and the device.
   - Montserrat fonts 12, 14, 16, 20, 24, 28 are set to `1`.
   - `LV_USE_TABVIEW`, `LV_USE_KEYBOARD`, `LV_USE_MSGBOX`, `LV_USE_SLIDER`, `LV_USE_IMAGE` are `1`.
   - `LV_MEM_SIZE` is `(128U * 1024U)`. The default 48 KB overruns and crashes the device in a reboot
     loop on sites with many stations (the list builds about five LVGL objects per station). See
     [UI-RENDERING.md](UI-RENDERING.md).
3. LVGL v9 note: the tick is driven at runtime, not by a macro. The firmware calls
   `lv_tick_set_cb(millis)` after `lv_init()`. Do not rely on the removed `LV_TICK_CUSTOM` macros.

## 4. Select the board profile in ESP32_Display_Panel

The panel driver picks the pinout from a board profile. The sketch ships a
`esp_panel_board_supported_conf.h` that enables it:

```c
#define ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED   (1)
#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7        // for the 7B use the 7B macro if present
```

For the 7B, check the installed driver for the matching macro
(`BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7B`). If the installed version does not list the 7B as a supported
board, switch to a custom profile (`ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM (1)` plus
`esp_panel_board_custom_conf.h`) and fill in the 7B pinout and panel timing from its wiki:
https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7 and the board repo
https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B. The 7 and 7B share the ESP32-S3, the 800x480
RGB panel, and the GT911 touch, so the profile differences are pin-level.

## 5. Board settings (Tools menu)

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Disabled (so `Serial` is UART0, carried by the CH343 bridge) |
| CPU Frequency | 240 MHz |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB (128 Mb) |
| Partition Scheme | 16 MB Flash (3 MB APP / 9.9 MB FATFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

Two of these are mandatory and the most common cause of failure:

- **PSRAM = OPI PSRAM.** The 800x480 RGB panel needs about 1.5 MB of frame buffer that only fits in
  PSRAM. Without it the panel fails with "no mem for frame buffer".
- **Partition = the 3 MB APP scheme.** The firmware does not fit the default 1.2 MB partition.

## 6. Build and flash

1. Open `firmware/EVtivityPanel/EVtivityPanel.ino`.
2. Connect the board over USB-C (a data cable, not charge-only).
3. Select the port in Tools > Port. On macOS it appears as `/dev/cu.usbmodemXXXX`. If no port shows,
   the cable is charge-only or the driver is missing.
4. Click Upload. If the upload does not start, hold BOOT, tap RESET, release BOOT after "Connecting..."
   appears.
5. Press RESET after the upload completes.
6. Open Serial Monitor at 1000000 to see app logs (UART0 via the CH343 bridge; leave USB CDC On Boot off).
   The ROM boot banner is at 115200.

## 7. First boot

The device starts the `EVtivity-Panel-XXXX` access point and shows the Provisioning screen. Join the AP
from a phone, open `http://192.168.4.1`, and enter the WiFi, CSMS URL, API key, and site id. See
[PROVISIONING.md](PROVISIONING.md).

## 8. Troubleshooting

| Symptom | Fix |
| --- | --- |
| "Sketch too big" | Set Partition Scheme to the 3 MB APP scheme. |
| "no mem for frame buffer" | Set PSRAM to OPI PSRAM. |
| Black screen | PSRAM off, wrong partition, or the IO expander did not init (check Serial for I2C errors). |
| Partition table error / boot loop | Flash size set wrong. `esptool --chip esp32s3 erase-flash`, then set 16 MB flash and the 3 MB partition and re-upload. |
| No Serial output | Leave USB CDC On Boot off so `Serial` is UART0, set the monitor to 1000000, press RESET, select the `usbmodem` (CH343) port. |
| No port in Tools > Port | Use a data USB-C cable. On macOS look for `/dev/cu.usbmodem*`. |
| Touch not responding | The CH422G IO expander must init before touch. Confirm the board profile in step 4. |
| WiFi will not connect | 2.4 GHz only. The panel falls back to the provisioning AP after three failures. |

## 9. Optional: command-line builds (arduino-cli)

For reproducible or CI builds:

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install lvgl ArduinoJson ESP32_Display_Panel ESP32_IO_Expander
# copy lv_conf.h next to the lvgl library as in step 3, then:
arduino-cli compile -b esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M \
  firmware/EVtivityPanel
arduino-cli upload -b esp32:esp32:esp32s3 -p /dev/cu.usbmodemXXXX firmware/EVtivityPanel
```

The FQBN options map to the Tools settings in step 5. Adjust the port to the one your board enumerates.
