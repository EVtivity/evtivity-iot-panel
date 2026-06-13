# ESP32-S3-Touch-LCD-7B Hardware Reference (authoritative)

Pulled from Waveshare's official 7B Arduino demo
(https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B, `examples/Arduino/examples/06_LCD`). The 7B is
NOT the same board as the non-B "7". Use these values, not the 800x480 / CH422G figures from the
`waveshare-7inch-esp32s3` spec folder (that folder documents the non-B 7).

## Why the first firmware did not light the screen

The firmware used `ESP32_Display_Panel`'s only Waveshare-7 profile, which assumes an 800x480 panel and a
CH422G IO expander. The 7B is a 1024x600 panel with a different IO expander (a register-based chip at
I2C 0x24), so the CH422G driver NACKs and the LCD never initializes. `ESP32_Display_Panel` 1.0.4 has no
7B profile (and none on master). The fix is to drive the 7B with its own drivers (below), not
`ESP32_Display_Panel`.

## Panel (RGB, 16-bit, RGB565)

| Field | Value |
| --- | --- |
| Resolution | 1024 x 600 |
| Pixel clock | 30 MHz, active on the falling edge (`pclk_active_neg = 1`) |
| Data width / bpp | 16-bit, RGB565 |
| HSYNC pulse / back / front | 162 / 152 / 48 |
| VSYNC pulse / back / front | 45 / 13 / 3 |
| Frame buffers | 2 (double buffer, in PSRAM) |
| Bounce buffer | H_RES * 10 pixels |

## RGB GPIO pins

| Signal | GPIO | Signal | GPIO |
| --- | --- | --- | --- |
| VSYNC | 3 | HSYNC | 46 |
| DE | 5 | PCLK | 7 |
| DISP | -1 (unused) | LCD reset | via expander, panel RST -1 |
| B3..B7 (DATA0-4) | 14, 38, 18, 17, 10 | | |
| G2..G7 (DATA5-10) | 39, 0, 45, 48, 47, 21 | | |
| R3..R7 (DATA11-15) | 1, 2, 42, 41, 40 | | |

(The RGB pins happen to match the non-B 7; the resolution, timing, and expander do not.)

## I2C (shared bus)

`SDA = GPIO8`, `SCL = GPIO9`, 400 kHz, `I2C_NUM_0`, using the ESP-IDF new driver
(`driver/i2c_master.h`). Do NOT use Arduino `Wire` on this bus: it uses the old I2C driver and aborts
with "CONFLICT! driver_ng is not allowed to be used with this old driver".

## IO expander (custom, NOT CH422G)

A register-based IO expander at I2C address `0x24`. Protocol: write the mode byte to register `0x24`,
then drive outputs through command `0x03` (`IO_EXTENSION_IO_OUTPUT_ADDR`). Pin map:

| Expander IO | Function |
| --- | --- |
| IO1 | Touch (GT911) reset |
| IO2 | LCD backlight on/off |
| IO3 | LCD reset |
| IO4 | SD card CS |
| IO5 | USB (0) / CAN (1) select |

Registers: mode `0x02`, output `0x03`, input `0x04`, PWM `0x05`, ADC `0x06`. The backlight must be
turned on through this chip (IO2); there is no direct backlight GPIO (panel `BK_LIGHT = -1`).

## Touch

GT911 on the same I2C bus (addresses 0x5D default / 0x14). Reset line is expander IO1, so the GT911
reset sequence runs through the IO expander before the touch is probed.

## LVGL

Waveshare's 7B demo uses LVGL 8.4.0. The firmware currently pins 8.3.11; either works, but pin 8.4.0 to
match the demo exactly.

## Implementation plan for the firmware (next pass)

The display layer must be rebased on Waveshare's 7B drivers; keep `ui.cpp`, `net.*`, `api_client.*`,
`settings_store.*` (logic is panel-agnostic).

1. Copy from the 7B demo `06_LCD` (and `08_TOUCH`) into `firmware/EVtivityPanel/`: `i2c.{h,cpp}`,
   `io_extension.{h,cpp}`, `rgb_lcd_port.{h,cpp}`, `gt911.{h,cpp}`, `touch.{h,cpp}`.
2. Remove the `ESP32_Display_Panel` + `lvgl_v8_port.*` + `esp_panel_board_supported_conf.h` path. Pin
   `lvgl@8.4.0`.
3. In `setup()`: `DEV_I2C_Init()`, `IO_EXTENSION_Init()`, turn the backlight on (IO2), reset the LCD
   (IO3) and the touch (IO1), `waveshare_rgb_lcd_init()` to get the `esp_lcd_panel_handle_t`, init GT911.
4. Write a minimal LVGL 8 port: `lv_disp_draw_buf_init` with two PSRAM buffers, a `flush_cb` that calls
   `esp_lcd_panel_draw_bitmap(panel, x1, y1, x2+1, y2+1, color_map)` then `lv_disp_flush_ready`, and a
   touch `read_cb` that reads GT911 points. Tick via `esp_timer` (as Waveshare's port does).
5. Set `lv_conf.h` `LV_HOR_RES`/screen to 1024x600 (or just create the display at 1024x600), keep
   `LV_COLOR_DEPTH 16`.
6. Rework `ui.cpp` and `sim/screens.c` layout for 1024x600 (top bar 1024 wide, content 1024x544). The
   extra width and height give more room: a wider station list and a roomier detail panel.

## Sources

- 7B repo: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B
- Demo files: `examples/Arduino/examples/06_LCD/{rgb_lcd_port,io_extension,i2c}.{h,cpp}`, `08_TOUCH/{gt911,touch}.{h,cpp}`
- LVGL transplant reference: `examples/Arduino/examples/13_LVGL_TRANSPLANT`
