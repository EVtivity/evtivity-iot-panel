# Flashing: Detect Port, Compile, Upload, Monitor

The operational runbook for building and flashing the firmware with `arduino-cli`. Assumes the toolchain
from [BUILD.md](BUILD.md) is installed (esp32 core, lvgl 8.3.11, ESP32_Display_Panel, ArduinoJson).

All commands use the sketch path `firmware/EVtivityPanel` and this verified FQBN:

```
esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M
```

FQBN options: `esp32:esp32:esp32s3` is the ESP32S3 Dev Module. `PSRAM=opi` enables the OPI PSRAM the RGB
panel needs. `PartitionScheme=app3M_fat9M_16MB` is the 3 MB app partition the firmware requires.
`FlashSize=16M` matches the 16 MB flash. `CDCOnBoot` is left at its default (off) so `Serial` is UART0,
which the board's CH343 USB-UART bridge carries to the `usbmodem` port. The firmware runs UART0 at
1 Mbaud; monitor at `1000000`. (With `CDCOnBoot=cdc`, `Serial` would be the native-USB CDC instead,
whose output does not reach the CH343 port.)

## 1. Detect the serial port

The ESP32-S3 must enumerate as a serial port before it can be flashed. Two ways to find it.

`arduino-cli board list` lists every serial port and any auto-detected board:

```bash
arduino-cli board list
```

Example output with the board connected (native USB):

```
Port                     Protocol Type        Board Name        FQBN                  Core
/dev/cu.usbmodem101      serial   Serial Port  ESP32S3 Dev Module esp32:esp32:esp32s3   esp32:esp32
```

Or list the raw devices and pick the right one:

```bash
ls /dev/cu.*
```

How to identify the board's port on macOS:

- **CH343 UART bridge (this board)**: `/dev/cu.usbmodemXXXX` (VID 0x1a86). The 7B carries UART0 over an
  on-board CH343, so despite the `usbmodem` name it is a UART bridge, not native USB. macOS 13+ has the
  driver built in. This is the port used for flashing and for `Serial`/screenshots.
- **Other UART bridges**: `/dev/cu.usbserialXXXX`, `/dev/cu.wchusbserialXXXX`, or `/dev/cu.SLAB_USBtoUART`
  on boards with a CP210x (Silicon Labs) or external WCH adapter.
- **Ignore** `/dev/cu.Bluetooth-Incoming-Port` and any audio devices (for example a headset). They are
  serial ports too but are not the board.

A one-liner that picks the most likely board port and stores it:

```bash
PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)
echo "PORT=$PORT"
```

If `PORT` is empty, the board is not enumerating. See [Troubleshooting](#5-troubleshooting-no-port).

## 2. Compile

```bash
arduino-cli compile -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M" firmware/EVtivityPanel
```

A successful build ends with the size report, for example:

```
Sketch uses 1419083 bytes (45%) of program storage space. Maximum is 3145728 bytes.
Global variables use 101228 bytes (30%) of dynamic memory, leaving 226452 bytes for local variables.
```

Compilation does not need the board connected.

## 3. Upload

Upload needs the detected port from step 1:

```bash
arduino-cli upload -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M" -p "$PORT" firmware/EVtivityPanel
```

If you skip `-p`, `arduino-cli` reports `Failed uploading: no upload port provided`, which means no port
was detected (the board is not connected or not enumerating).

If the upload cannot connect ("Failed to connect", "Wrong boot mode"), put the board in download mode:
hold BOOT, tap RESET, release BOOT, then re-run. Press RESET after the upload completes.

## 4. Monitor

Watch the boot logs over USB (USB CDC On Boot must be on, which the FQBN sets):

```bash
arduino-cli monitor -p "$PORT" -c baudrate=1000000
```

Press RESET on the board to see the boot sequence. The firmware prints `[boot] ready` once the display,
WiFi, and tasks are up.

## 5. Troubleshooting (no port)

`arduino-cli board list` shows only Bluetooth and audio devices, no `usbmodem` or `usbserial`:

1. **Data cable.** Many USB-C cables are charge-only. Swap to a known data cable. This is the most common
   cause and gives exactly this symptom.
2. **Download mode.** Hold BOOT, tap RESET, release BOOT, then re-check `arduino-cli board list`.
3. **Driver.** If the board uses a UART bridge it appears as `cu.usbserial` / `cu.wchusbserial` and needs
   the CH343 or CP210x driver. Native-USB boards need no driver.
4. **Power.** Confirm the board is powered (the display backlight or an LED is on).

## 6. One-shot script

Detect, compile, upload, then monitor:

```bash
#!/usr/bin/env bash
set -e
FQBN="esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M"
SKETCH="firmware/EVtivityPanel"

PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)
[ -z "$PORT" ] && { echo "No board port found. Check the cable / download mode / driver."; exit 1; }
echo "Using port: $PORT"

arduino-cli compile -b "$FQBN" "$SKETCH"
arduino-cli upload  -b "$FQBN" -p "$PORT" "$SKETCH"
arduino-cli monitor -p "$PORT" -c baudrate=1000000
```

## 7. Same steps in the Arduino IDE

If you prefer the GUI: select Tools > Port (the `usbmodem`/`usbserial` entry, not Bluetooth), set the
board options from [BUILD.md](BUILD.md#5-board-settings-tools-menu), click Upload, then open Serial
Monitor at 1000000 (the ROM boot banner is 115200). If Upload cannot connect, hold BOOT and tap RESET first.
