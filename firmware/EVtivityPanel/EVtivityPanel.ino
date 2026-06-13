/* EVtivity CSMS IoT Panel
 * Waveshare ESP32-S3-Touch-LCD-7B (1024x600). LVGL 8.4 + Waveshare 7B drivers.
 * See docs/ARCHITECTURE.md, docs/HARDWARE-7B.md, and docs/BUILD.md. */
#include <Arduino.h>
#include <lvgl.h>
#include "lvgl_port.h"   // pulls in rgb_lcd_port.h, touch.h, gt911.h, lvgl.h

#include "config.h"
#include "settings_store.h"
#include "model.h"
#include "net.h"
#include "api_client.h"
#include "ui.h"

static Settings g_settings;
static QueueHandle_t g_cmdQueue = nullptr;

static void enqueue_command(const Command &c) {
    if (g_cmdQueue) xQueueSend(g_cmdQueue, &c, 0);
}

// --- Serial screenshot: send 'S' over USB to dump the live screen as base64 RGB565.
// lv_snapshot re-renders the active screen tree into a PSRAM buffer, so the frame is
// exactly what is displayed (top-layer modals excluded). Decoded by scripts/screenshot.py.
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static size_t b64_encode(const uint8_t *in, size_t n, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static void send_screenshot() {
    const int W = EXAMPLE_LCD_H_RES, H = EXAMPLE_LCD_V_RES;
    static uint8_t *buf = nullptr;
    const size_t need = (size_t)W * H * 2; // TRUE_COLOR = RGB565, 2 bytes/px
    if (!buf) buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
    if (!buf) { Serial.println("[shot] alloc fail"); return; }

    lv_img_dsc_t dsc;
    lv_res_t r = LV_RES_INV;
    if (lvgl_port_lock(-1)) {
        r = lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR, &dsc, buf, need);
        lvgl_port_unlock();
    }
    if (r != LV_RES_OK) { Serial.println("[shot] snapshot fail"); return; }

    const size_t rowBytes = (size_t)W * 2;
    static char line[((1024 * 2 + 2) / 3) * 4 + 4]; // base64 of one row + NUL
    Serial.printf("[shot] BEGIN %d %d RGB565\n", W, H);
    for (int y = 0; y < H; y++) {
        b64_encode(buf + (size_t)y * rowBytes, rowBytes, line);
        Serial.print("D:");
        Serial.println(line);
    }
    Serial.println("[shot] END");
}

static void run_command(const Command &c) {
    if (c.type == CMD_REFRESH) return; // caller resets lastPoll to force an immediate re-poll
    String id(c.ocppId), sim(c.sim), err;
    bool ok = false;
    const char *title = "";
    switch (c.type) {
        case CMD_START:    ok = api_start_charging(id, c.evseId, err);                title = "Start";    break;
        case CMD_STOP:     ok = api_css_action(id, c.evseId, "stopCharging", err);  title = "Stop";     break;
        case CMD_CLEAR:    ok = api_css_action(id, c.evseId, "clearFault", err);    title = "Clear";    break;
        case CMD_SIMULATE: ok = api_css_action(id, c.evseId, sim, err);             title = "Simulate"; break;
        case CMD_PLUGIN:   ok = api_css_action(id, c.evseId, "plugIn", err);        title = "Plug In";  break;
        case CMD_UNPLUG:   ok = api_css_action(id, c.evseId, "unplug", err);        title = "Unplug";   break;
        default: return;
    }
    // Surface the API result on screen (the spinner popup is waiting for it).
    if (lvgl_port_lock(-1)) { ui_set_command_result(title, ok, err); lvgl_port_unlock(); }
    Serial.printf("[cmd] %s %s -> %s %s\n", id.c_str(), sim.c_str(), ok ? "ok" : "fail", err.c_str());
}

static void network_task(void *arg) {
    SiteSnapshot snap;
    uint32_t lastPoll = 0;
    int burst = 0; // fast re-polls after an action so a stop/start status change is caught promptly
    NetState lastState = (NetState)-1;
    bool checkedPerms = false, gotVersion = false, gotAddress = false;
    for (;;) {
        net_loop();

        NetState st = net_state();
        if (st != lastState) {
            lastState = st;
            if (lvgl_port_lock(-1)) { ui_set_status(st, net_ap_ssid()); lvgl_port_unlock(); }
        }

        Command c;
        while (g_cmdQueue && xQueueReceive(g_cmdQueue, &c, 0) == pdTRUE) {
            run_command(c);
            lastPoll = 0; // refresh right after an action
            burst = 4;    // then keep polling fast for a few cycles to catch the status transition
        }

        if (st == NET_ONLINE) {
            // One-time on first connect: API-key permission preflight + CSMS version.
            if (!checkedPerms) {
                String missing, err;
                bool ok = api_check_permissions(missing, err);
                if (ok || missing.length()) { // definitive result (not a transport error)
                    checkedPerms = true;
                    if (lvgl_port_lock(-1)) { ui_set_perm_status(missing, err); lvgl_port_unlock(); }
                }
            }
            if (!gotVersion) {
                String ver, err;
                if (api_get_system_version(ver, err)) {
                    gotVersion = true;
                    if (lvgl_port_lock(-1)) { ui_set_csms_version(ver); lvgl_port_unlock(); }
                }
            }
            if (!gotAddress) {
                String addr, err;
                if (api_get_site_info(addr, err)) {
                    gotAddress = true;
                    if (lvgl_port_lock(-1)) { ui_set_site_address(addr); lvgl_port_unlock(); }
                }
            }
            uint32_t pollEvery = (burst > 0) ? 2000 : g_settings.pollMs;
            if (lastPoll == 0 || millis() - lastPoll > pollEvery) {
                lastPoll = millis();
                if (burst > 0) burst--;
                api_fetch_site(snap); // fills snap.valid / snap.error either way
                if (lvgl_port_lock(-1)) {
                    ui_set_snapshot(snap);
                    lvgl_port_unlock();
                }
                // live detail for the selected charging station, if any
                String sid;
                if (lvgl_port_lock(-1)) { sid = ui_selected_station_id(); lvgl_port_unlock(); }
                if (sid.length()) {
                    SessionDetail sd; String e;
                    api_fetch_session(sid, sd, e);
                    if (lvgl_port_lock(-1)) { ui_set_session(sd); lvgl_port_unlock(); }
                }
                // connectors (ports) for the selected station, regardless of charging
                String csid;
                if (lvgl_port_lock(-1)) { csid = ui_selected_station_internal_id(); lvgl_port_unlock(); }
                if (csid.length()) {
                    ConnectorSet cs; String ce;
                    if (api_fetch_connectors(csid, cs, ce))
                        if (lvgl_port_lock(-1)) { ui_set_connectors(cs); lvgl_port_unlock(); }
                }
                // one coalesced detail rebuild for everything this poll changed
                if (lvgl_port_lock(-1)) { ui_commit_detail(); lvgl_port_unlock(); }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup() {
    Serial.begin(1000000); // UART0 via the CH343 bridge; high baud so screenshots transfer fast
    delay(400);
    settings_load(g_settings);
    api_begin(g_settings);

    // 7B display bring-up (Waveshare drivers): GT911 touch, RGB panel, backlight,
    // then the LVGL port (own task + esp_timer tick + mutex). See docs/HARDWARE-7B.md.
    esp_lcd_touch_handle_t tp = touch_gt911_init();
    esp_lcd_panel_handle_t panel = waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();
    if (lvgl_port_init(panel, tp) == ESP_OK && lvgl_port_lock(-1)) {
        ui_init(g_settings);
        ui_set_command_cb(enqueue_command);
        lvgl_port_unlock();
    } else {
        // Keep the system alive so the serial log explains why instead of
        // crashing on a null LVGL display.
        Serial.println("[boot] LVGL/display init FAILED - skipping UI.");
    }

    // FLICKER_TEST_NO_WIFI: temporary diagnostic. With WiFi + the network task
    // disabled, the panel runs LVGL alone. If the screen is rock-solid in this
    // build but flickers normally, the flicker is WiFi/flash-bus contention.
    // Set back to 0 to restore normal operation.
#define FLICKER_TEST_NO_WIFI 0
#if FLICKER_TEST_NO_WIFI
    Serial.println("[boot] FLICKER TEST: WiFi + network task DISABLED");
#else
    g_cmdQueue = xQueueCreate(8, sizeof(Command));
    net_begin(g_settings);
    xTaskCreatePinnedToCore(network_task, "net", 16384, nullptr, 1, nullptr, 0);
#endif

    Serial.println("[boot] ready");
}

void loop() {
    // LVGL runs on the port task; networking on the net task. Here we only watch the
    // USB serial for a screenshot trigger ('S').
    while (Serial.available()) {
        if (Serial.read() == 'S') send_screenshot();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
}
