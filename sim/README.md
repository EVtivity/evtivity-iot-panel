# LVGL UI Simulator

A tiny host program that runs the panel's real LVGL UI and renders each screen to an 800x480 PNG. It is
headless (no display server needed), so it works in any shell and in CI. The renders are pixel-true:
`screens.c` is the same UI code that runs on the device, so a screenshot here is what the panel shows.

This is how the images in `docs/images/` are produced.

## Why it exists

- Design and review the UI without hardware.
- Generate documentation screenshots that always match the code.
- Develop the LVGL layout once and reuse it in the firmware (the firmware splits `screens.c` into
  `ui_charging` / `ui_settings` and feeds them live data instead of the built-in sample data).

## Prerequisites

- CMake 3.16+ and a C compiler (Apple clang, gcc, or MSVC).
- The LVGL source under `sim/lvgl`. If it is missing:
  ```bash
  cd sim && git clone --depth 1 -b release/v9.4 https://github.com/lvgl/lvgl.git
  ```
- For the optional logo asset pipeline only: Python 3 with `pillow` and `pypng`.

No SDL is required for the headless render. SDL is only needed if you want the optional interactive
window (not used here).

## Build and render

```bash
cd sim
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

./build/sim charging  ../docs/images/charging.png
./build/sim settings  ../docs/images/settings.png
./build/sim provision ../docs/images/provision.png
```

The first argument is the screen name (`charging`, `settings`, `provision`); the second is the output
PNG path.

## How it works

- `main.c` creates an LVGL display backed by a single ARGB8888 buffer in
  `LV_DISPLAY_RENDER_MODE_DIRECT`, so LVGL renders straight into that buffer. After building a screen it
  pumps `lv_timer_handler()` a dozen times, then writes the buffer to PNG with `stb_image_write.h`. The
  LVGL byte order (`0xAARRGGBB`) is swizzled to RGBA in the writer.
- The tick comes from `lv_tick_set_cb` reading a monotonic clock. LVGL v9 requires this runtime callback
  (the `LV_TICK_CUSTOM` macros were removed); the device does the same with `millis`.
- `lv_conf.h` is the host LVGL config. It is also the starting point for the device `lv_conf.h`; the only
  value to change for hardware is `LV_COLOR_DEPTH` (16 on the RGB565 panel, 32 here for clean PNGs). See
  [docs/BUILD.md](../docs/BUILD.md#3-configure-lvgl-lv_confh).

## Files

| File | Role |
| --- | --- |
| `main.c` | Headless harness: display, render loop, PNG writer |
| `screens.c` / `screens.h` | The portable UI (shared with the firmware) |
| `lv_conf.h` | LVGL config for the host build |
| `CMakeLists.txt` | Build: LVGL + harness + image assets |
| `assets/logo32.c`, `assets/logo96.c` | The EVtivity logo as LVGL images (generated) |
| `assets/logo*.png`, `assets/logo.svg` | Logo asset sources |
| `stb_image_write.h` | Vendored single-header PNG writer |
| `lvgl/` | LVGL source (cloned, gitignored) |
| `build/` | CMake build output (gitignored) |

## Regenerating the logo asset

The EVtivity logo is the real brand SVG converted to LVGL C images:

```bash
# render the SVG to a transparent PNG at the target size, then:
python3 lvgl/scripts/LVGLImage.py --ofmt C --cf ARGB8888 -o assets assets/logo96.png
python3 lvgl/scripts/LVGLImage.py --ofmt C --cf ARGB8888 -o assets assets/logo32.png
```

The same pipeline converts the planned charging-station illustration. See
[docs/UI.md](../docs/UI.md#assets).

## Interactive window (optional)

To click through the UI on your desktop, LVGL's built-in SDL driver can replace the headless display.
SDL2 is required (`brew install sdl2`). This is a convenience for development and is not part of the
documentation pipeline.
