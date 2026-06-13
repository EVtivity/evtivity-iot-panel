/* Headless LVGL host harness: software-renders a screen to a PNG at 800x480.
 * Build with CMake (see CMakeLists.txt). Run: ./sim <screen> <out.png>
 * The UI build functions live in screens.c and are the same LVGL calls the
 * device firmware uses, so these renders are pixel-true. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "screens.h"

#define SCR_W 800
#define SCR_H 480

static uint32_t fb[SCR_W * SCR_H]; /* ARGB8888 frame + LVGL direct draw buffer */

static uint32_t tick_cb(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    (void)px_map; /* DIRECT mode renders straight into fb */
    lv_display_flush_ready(disp);
}

static void write_png(const char *path) {
    static uint8_t rgba[SCR_W * SCR_H * 4];
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint32_t p = fb[i]; /* 0xAARRGGBB */
        rgba[i * 4 + 0] = (p >> 16) & 0xFF;
        rgba[i * 4 + 1] = (p >> 8) & 0xFF;
        rgba[i * 4 + 2] = (p) & 0xFF;
        rgba[i * 4 + 3] = 0xFF;
    }
    stbi_write_png(path, SCR_W, SCR_H, 4, rgba, SCR_W * 4);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    const char *screen = argc > 1 ? argv[1] : "charging";
    const char *out = argc > 2 ? argv[2] : "out.png";

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(disp, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    screens_theme_init();
    screens_build(screen);

    /* Pump the renderer so the full screen paints. */
    for (int i = 0; i < 12; i++) {
        lv_timer_handler();
        struct timespec ts = {0, 20 * 1000000L};
        nanosleep(&ts, NULL);
    }

    write_png(out);
    return 0;
}
