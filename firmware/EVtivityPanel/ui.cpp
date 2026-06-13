#include "ui.h"
#include "config.h"
#include "net.h"
#include "logo_img.h"
#include <lvgl.h>
#include <time.h>

// theme tokens
#define C_BG 0x0f172a
#define C_CARD 0x1e293b
#define C_ELEV 0x273449
#define C_BORDER 0x334155
#define C_TEXT 0xf1f5f9
#define C_MUTED 0x94a3b8
#define C_PRIMARY 0x3b82f6
#define C_GREEN 0x22c55e
#define C_AMBER 0xf59e0b
#define C_RED 0xef4444
#define C_ORANGE 0xf97316
#define C_CYAN 0x06b6d4
#define C_VIOLET 0x8b5cf6

static void (*g_cmd_cb)(const Command &) = nullptr;
static SiteSnapshot g_snap;
static int g_sel = 0;
static int g_tab = 0; // 0 charging, 1 settings
static Settings g_settings;
static NetState g_net_state = NET_CONNECTING;
static String g_ap_ssid;
static String g_csms_version;
static String g_perm_warn;   // non-empty => API key permission problem
static SessionDetail g_session;
static String g_site_address;
static ConnectorSet g_connectors;
static int g_selPort = 0;             // selected connector index within g_connectors
static lv_obj_t *g_popup = nullptr;   // modal backdrop while a command is in flight, else null
static lv_obj_t *g_popupCard = nullptr;
static lv_obj_t *g_popupSpin = nullptr;
static lv_obj_t *g_popupText = nullptr;

// In-place update refs: live value changes update these widgets directly instead of
// tearing down the whole content tree (a full rebuild flashes the panel). A full
// ui_rebuild is done only when the structure changes (different station/ports/state).
static lv_obj_t *g_portStat[MAX_CONNECTORS]; // per-port status label
static int g_portStatN = 0;
static String g_portKey;                     // structure signature of the built port list
static lv_obj_t *g_statVal[4];               // session stat values: elapsed, energy, power, cost
static lv_obj_t *g_chartObj = nullptr;
static lv_chart_series_t *g_chartSer = nullptr;
static lv_obj_t *g_chartPeak = nullptr;       // Y-axis peak label (kW or kWh)
static lv_obj_t *g_chartEnd = nullptr;        // X-axis elapsed label
static lv_obj_t *g_chartCap = nullptr;        // caption: "Power (kW)..." / "Energy (kWh)..."
static lv_obj_t *g_chartTog[2] = {nullptr, nullptr}; // Power / Energy toggle buttons
static int g_chartMode = 0;                   // 0 = power (kW), 1 = cumulative energy (kWh)
static String g_sessKey;                     // structure signature of the session detail
static lv_obj_t *g_listRow[MAX_STATIONS];    // station list rows (highlight updated in place)
static lv_obj_t *g_rowStat[MAX_STATIONS];    // per-row status label (updated in place)
static int g_listRowN = 0;
static String g_listKey;                     // structure signature of the station list
static lv_obj_t *g_detail = nullptr;         // detail card; rebuilt alone on selection
static bool g_detailDirty = false;           // a poll changed detail structure; rebuild once at commit

static lv_obj_t *g_content = nullptr;
static lv_obj_t *g_tab_btn[2] = {nullptr, nullptr};
static lv_obj_t *g_wifi_lbl = nullptr;

void ui_rebuild();
static void ui_render_root();
static void build_detail(lv_obj_t *detail);
static void rebuild_detail();

// EVtivity brand mark, pre-decoded from evtivity-logo.svg (LVGL 8.4 has no SVG renderer).
#define LOGO_TOP 34
#define LOGO_BIG 104
static lv_obj_t *make_logo(lv_obj_t *parent, int size) {
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, size <= 40 ? &evtivity_logo_34 : &evtivity_logo_104);
    return img;
}

static bool prov_mode() { return g_net_state == NET_PROVISION; }

// Status -> color, matching the CSMS status palette (chart-theme.ts).
static lv_color_t status_color(const String &s) {
    if (s == "available" || s == "occupied") return lv_color_hex(C_PRIMARY);
    if (s == "charging" || s == "discharging") return lv_color_hex(C_GREEN);
    if (s == "preparing" || s == "ev_connected") return lv_color_hex(C_CYAN);
    if (s == "suspended_ev" || s == "suspended_evse" || s == "idle") return lv_color_hex(C_AMBER);
    if (s == "reserved") return lv_color_hex(C_ORANGE);
    if (s == "finishing") return lv_color_hex(C_VIOLET);
    if (s == "faulted" || s == "unavailable") return lv_color_hex(C_RED);
    return lv_color_hex(C_MUTED);
}

// Status -> label, matching the CSMS i18n badge text.
static String status_label(const String &s) {
    if (s == "available") return "Available";
    if (s == "occupied") return "Occupied";
    if (s == "charging") return "Charging";
    if (s == "discharging") return "Discharging";
    if (s == "preparing") return "Preparing";
    if (s == "ev_connected") return "EV Connected";
    if (s == "suspended_ev") return "Suspended EV";
    if (s == "suspended_evse") return "Suspended EVSE";
    if (s == "idle") return "Idle";
    if (s == "reserved") return "Reserved";
    if (s == "finishing") return "Finishing";
    if (s == "faulted") return "Faulted";
    if (s == "unavailable") return "Unavailable";
    return "Unknown";
}

static lv_obj_t *plain(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    // Base objects are clickable by default in LVGL v8; a passive layout wrapper
    // must not intercept taps meant for a clickable parent (e.g. station rows).
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

static lv_obj_t *card(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 14, 0);
    return o;
}

static lv_obj_t *txt(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static void emit(CommandType type, const char *sim) {
    if (!g_cmd_cb || g_snap.count == 0 || g_sel >= g_snap.count) return;
    Command c;
    c.type = type;
    Station &s = g_snap.stations[g_sel];
    strncpy(c.ocppId, s.ocppId.c_str(), sizeof(c.ocppId) - 1);
    c.evseId = 1; // fall back to EVSE 1 before connectors load
    if (g_connectors.stationId == s.id && g_selPort >= 0 && g_selPort < g_connectors.count)
        c.evseId = g_connectors.items[g_selPort].evseId;
    if (sim) strncpy(c.sim, sim, sizeof(c.sim) - 1);
    g_cmd_cb(c);
}

static void on_port(lv_event_t *e) {
    g_selPort = (int)(intptr_t)lv_event_get_user_data(e);
    rebuild_detail(); // only the detail depends on the selected port
}

// Modal message box for a simulator action, updated in place with the result.
static void popup_close(lv_event_t *e) {
    if (g_popup) lv_obj_del_async(g_popup); // safe to call from a child's own event
    g_popup = nullptr; g_popupCard = nullptr; g_popupSpin = nullptr; g_popupText = nullptr;
}

// Shared modal: dimmed backdrop + centered card + title. Returns the card to fill.
static lv_obj_t *make_modal(const char *title) {
    popup_close(nullptr);
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    // Opaque backdrop: a semi-transparent full-screen overlay makes direct-mode
    // recomposite the whole screen on each spinner frame, which flickers.
    lv_obj_set_style_bg_color(bg, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(bg);
    lv_obj_set_size(card, 480, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 18, 0);

    lv_obj_t *t = txt(card, title, &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

    g_popup = bg; g_popupCard = card;
    return card;
}

// Full-width pill button used inside the modals.
static lv_obj_t *popup_btn(lv_obj_t *parent, const char *label, uint32_t color, lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_height(b, 56);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_center(l);
    return b;
}

// Modal with a spinner shown while a command runs; ui_set_command_result fills in
// the API response and an OK button when it returns.
static void show_pending_popup(const char *title, const String &text) {
    lv_obj_t *card = make_modal(title);
    g_popupSpin = lv_spinner_create(card, 900, 60);
    lv_obj_set_size(g_popupSpin, 54, 54);
    lv_obj_set_style_arc_color(g_popupSpin, lv_color_hex(C_PRIMARY), LV_PART_INDICATOR);
    g_popupText = txt(card, text.c_str(), &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_style_text_align(g_popupText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_popupText, LV_PCT(100));
    lv_label_set_long_mode(g_popupText, LV_LABEL_LONG_WRAP);
}

static void add_ok_button(lv_obj_t *card) {
    lv_obj_t *okb = lv_btn_create(card);
    lv_obj_set_size(okb, 160, 54);
    lv_obj_set_style_bg_color(okb, lv_color_hex(C_ELEV), 0);
    lv_obj_set_style_radius(okb, 10, 0);
    lv_obj_set_style_shadow_width(okb, 0, 0);
    lv_obj_add_event_cb(okb, popup_close, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *okl = lv_label_create(okb);
    lv_label_set_text(okl, "OK");
    lv_obj_set_style_text_font(okl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(okl, lv_color_hex(0xffffff), 0);
    lv_obj_center(okl);
}

// Static (no API call) result popup, same style as the command result.
static void show_message_popup(const char *title, const String &msg, bool good) {
    lv_obj_t *card = make_modal(title);
    lv_obj_t *m = txt(card, msg.c_str(), &lv_font_montserrat_20, good ? C_GREEN : C_RED);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    add_ok_button(card);
}

// Status of the connector the actions target, or "" if not loaded yet.
static String sel_port_status() {
    if (g_snap.count == 0 || g_sel >= g_snap.count) return "";
    if (g_connectors.stationId == g_snap.stations[g_sel].id && g_selPort >= 0 && g_selPort < g_connectors.count)
        return g_connectors.items[g_selPort].status;
    return "";
}

static void on_action(lv_event_t *e) {
    intptr_t which = (intptr_t)lv_event_get_user_data(e);
    switch (which) {
        case 0: {
            // startCharging needs the EV plugged in; the CSMS rejects an available
            // connector. Tell the operator plainly instead of calling the API.
            String st = sel_port_status();
            if (st == "available" || st == "unavailable" || st.length() == 0) {
                show_message_popup("Start", "Connector is not plugged in.", false);
            } else {
                show_pending_popup("Start", "Sending command...");
                emit(CMD_START, "startCharging");
            }
            break;
        }
        case 1: show_pending_popup("Stop", "Sending command...");  emit(CMD_STOP, "stopCharging");  break;
        case 2: emit(CMD_CLEAR, "clearFault"); break;
        case 3: emit(CMD_SIMULATE, "injectFault"); break;
    }
}

static void on_refresh(lv_event_t *e) {
    if (!g_cmd_cb) return;
    Command c;
    c.type = CMD_REFRESH;
    g_cmd_cb(c);
}

// Simulator "Simulate" button: opens a popup to plug in or unplug the selected port.
static void on_sim_plugin(lv_event_t *e) {
    show_pending_popup("Plug In", "Sending command...");
    emit(CMD_PLUGIN, "plugIn");
}
static void on_sim_unplug(lv_event_t *e) {
    show_pending_popup("Unplug", "Sending command...");
    emit(CMD_UNPLUG, "unplug");
}

// Simulator "Simulate" button: a styled popup (same look as Start/Stop) to plug in
// or unplug the selected port. Tap the dimmed backdrop to dismiss.
static void on_simulate(lv_event_t *e) {
    lv_obj_t *card = make_modal("Simulate");
    lv_obj_t *sub = txt(card, "Connector action for the selected port", &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_t *row = plain(card);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    popup_btn(row, "Plug In", C_PRIMARY, on_sim_plugin);
    popup_btn(row, "Unplug", C_ELEV, on_sim_unplug);
    lv_obj_add_event_cb(g_popup, popup_close, LV_EVENT_CLICKED, nullptr);
}

static void on_row(lv_event_t *e) {
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    if (n == g_sel) return;
    g_sel = n;
    g_selPort = 0;
    // Highlight the list rows in place; only the detail needs a rebuild.
    for (int i = 0; i < g_listRowN && i < MAX_STATIONS; i++)
        if (g_listRow[i])
            lv_obj_set_style_bg_color(g_listRow[i], lv_color_hex(i == g_sel ? C_ELEV : C_CARD), 0);
    rebuild_detail();
    // Pull the newly selected station's ports/session promptly instead of waiting a full poll.
    if (g_cmd_cb) { Command c; c.type = CMD_REFRESH; g_cmd_cb(c); }
}

static lv_obj_t *abtn(lv_obj_t *p, const char *label, uint32_t color, intptr_t id, bool en) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_height(b, 52);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, en ? LV_OPA_COVER : 70, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (en) lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED, (void *)id);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *stat(lv_obj_t *p, const char *label, const String &val) {
    lv_obj_t *col = plain(p);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_t *v = txt(col, val.c_str(), &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    txt(col, label, &lv_font_montserrat_14, C_MUTED);
    return v; // value label, updated in place by ui_set_session
}

static String elapsed_str(uint32_t startedEpoch) {
    time_t now = time(nullptr);
    if (startedEpoch == 0 || now < 1600000000) return "--";
    long sec = (long)now - (long)startedEpoch;
    if (sec < 0) sec = 0;
    int h = sec / 3600, m = (sec % 3600) / 60, ss = sec % 60;
    char b[16];
    if (h > 0) snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, ss);
    else snprintf(b, sizeof(b), "%d:%02d", m, ss);
    return String(b);
}

// Fill vals[] with the series for the current chart mode and return its peak.
// Power mode: the kW samples as-is. Energy mode: cumulative kWh, trapezoidal
// integration of power over the sample timestamps (monotonic, so it climbs).
static float chart_series(const SessionDetail &sd, float *vals, int n) {
    float mx = 0;
    if (g_chartMode == 1) {
        float e = 0;
        vals[0] = 0;
        for (int i = 1; i < n; i++) {
            long dt = (long)sd.sampleEpoch[i] - (long)sd.sampleEpoch[i - 1];
            if (dt < 0) dt = 0;
            e += (sd.samples[i] + sd.samples[i - 1]) * 0.5f * (dt / 3600.0f);
            vals[i] = e;
        }
        mx = vals[n - 1];
        if (mx < 0.01f) mx = 0.01f;
    } else {
        for (int i = 0; i < n; i++) { vals[i] = sd.samples[i]; if (vals[i] > mx) mx = vals[i]; }
        if (mx < 1) mx = 1;
    }
    return mx;
}

// Push the current-mode data, Y range, peak label and caption into the chart.
static void chart_apply(const SessionDetail &sd) {
    if (!g_chartObj || !g_chartSer || sd.sampleCount < 2) return;
    int n = sd.sampleCount;
    static float vals[MAX_POWER_SAMPLES];
    float mx = chart_series(sd, vals, n);
    int scale = (g_chartMode == 1) ? 100 : 10; // fixed-point into lv_coord_t (int16)
    int top = (int)(mx * scale) + 5;
    if (top < 10) top = 10;
    lv_chart_set_point_count(g_chartObj, n);
    lv_chart_set_range(g_chartObj, LV_CHART_AXIS_PRIMARY_Y, 0, top);
    for (int i = 0; i < n; i++)
        lv_chart_set_value_by_id(g_chartObj, g_chartSer, i, (lv_coord_t)(vals[i] * scale));
    lv_chart_refresh(g_chartObj);
    if (g_chartPeak) {
        String pk = (g_chartMode == 1) ? String(mx, 2) : String(mx, 1);
        if (pk != lv_label_get_text(g_chartPeak)) lv_label_set_text(g_chartPeak, pk.c_str());
    }
    if (g_chartCap) {
        const char *cap = (g_chartMode == 1) ? "Energy (kWh) vs time" : "Power (kW) vs time";
        if (String(cap) != lv_label_get_text(g_chartCap)) lv_label_set_text(g_chartCap, cap);
    }
}

static void chart_tog_style() {
    for (int i = 0; i < 2; i++)
        if (g_chartTog[i])
            lv_obj_set_style_bg_color(g_chartTog[i],
                lv_color_hex(g_chartMode == i ? C_PRIMARY : C_ELEV), 0);
}

static void on_chart_mode(lv_event_t *e) {
    int m = (int)(intptr_t)lv_event_get_user_data(e);
    if (m == g_chartMode) return;
    g_chartMode = m;
    chart_tog_style();
    chart_apply(g_session);
}

static void build_charging(lv_obj_t *parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(parent, 14, 0);

    // left column: a card holding the scrollable station list plus a sticky refresh
    // button pinned at the bottom (the button is a sibling of the scrolling list, so
    // it does not scroll away on sites with many stations).
    lv_obj_t *listcard = lv_obj_create(parent);
    lv_obj_set_size(listcard, 380, LV_PCT(100));
    lv_obj_clear_flag(listcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(listcard, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(listcard, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(listcard, 1, 0);
    lv_obj_set_style_radius(listcard, 12, 0);
    lv_obj_set_style_pad_all(listcard, 8, 0);
    lv_obj_set_flex_flow(listcard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listcard, 8, 0);

    // scrollable station rows (grows to fill the space above the refresh button)
    lv_obj_t *list = lv_obj_create(listcard);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    // Elastic/momentum scroll rubber-bands on a tap and cancels the row click.
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    if (g_snap.count == 0) {
        const char *msg;
        if (g_snap.error.length()) msg = g_snap.error.c_str();
        else if (g_net_state == NET_CONNECTING) msg = "Connecting to Wi-Fi...";
        else if (g_net_state == NET_OFFLINE) msg = "Offline - check Wi-Fi";
        else msg = g_snap.valid ? "No stations at this site" : "Loading stations...";
        lv_obj_t *empty = txt(list, msg, &lv_font_montserrat_16, C_MUTED);
        lv_obj_center(empty);
    }
    for (int i = 0; i < g_snap.count; i++) {
        Station &s = g_snap.stations[i];
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_set_style_bg_color(row, lv_color_hex(i == g_sel ? C_ELEV : C_CARD), 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_left(row, 14, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, status_color(s.status), 0);
        lv_obj_set_style_border_width(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (i < MAX_STATIONS) g_listRow[i] = row; // highlight updated in place on selection

        lv_obj_t *col = plain(row);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        txt(col, s.ocppId.c_str(), &lv_font_montserrat_16, C_TEXT);
        txt(col, s.connector.length() ? s.connector.c_str() : s.model.c_str(), &lv_font_montserrat_12, C_MUTED);

        lv_obj_t *b = txt(row, status_label(s.status).c_str(), &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(b, status_color(s.status), 0);
        if (i < MAX_STATIONS) g_rowStat[i] = b;
    }
    g_listRowN = g_snap.count;
    g_listKey = String(g_snap.count);
    for (int i = 0; i < g_snap.count; i++) g_listKey += "|" + g_snap.stations[i].ocppId;

    // sticky refresh control pinned at the bottom of the list card
    lv_obj_t *refresh = lv_btn_create(listcard);
    lv_obj_set_width(refresh, LV_PCT(100));
    lv_obj_set_height(refresh, 38);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(C_ELEV), 0);
    lv_obj_set_style_radius(refresh, 8, 0);
    lv_obj_set_style_shadow_width(refresh, 0, 0);
    lv_obj_add_event_cb(refresh, on_refresh, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ri = lv_label_create(refresh);
    lv_label_set_text(ri, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_font(ri, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ri, lv_color_hex(C_MUTED), 0);
    lv_obj_center(ri);

    // detail card; its contents are (re)built by build_detail so a selection can
    // redraw just this panel instead of the whole content (list + detail).
    lv_obj_t *detail = card(parent);
    lv_obj_set_flex_grow(detail, 1);
    lv_obj_set_height(detail, LV_PCT(100));
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail, 12, 0);
    g_detail = detail;
    build_detail(detail);
}

// Rebuild only the detail card. Reset the in-place refs first: they point into the
// subtree being deleted.
static void rebuild_detail() {
    if (!g_detail) { ui_rebuild(); return; }
    g_portStatN = 0; g_portKey = "";
    for (int i = 0; i < 4; i++) g_statVal[i] = nullptr;
    g_chartObj = nullptr; g_chartSer = nullptr; g_chartPeak = nullptr; g_chartEnd = nullptr;
    g_chartCap = nullptr; g_chartTog[0] = nullptr; g_chartTog[1] = nullptr; g_sessKey = "";
    lv_obj_clean(g_detail);
    build_detail(g_detail);
}

static void build_detail(lv_obj_t *detail) {
    if (g_snap.count > 0 && g_sel < g_snap.count) {
        Station &s = g_snap.stations[g_sel];
        // header: station title on the left, status on the right
        lv_obj_t *hdr = plain(detail);
        lv_obj_set_width(hdr, LV_PCT(100));
        lv_obj_set_height(hdr, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        txt(hdr, s.ocppId.c_str(), &lv_font_montserrat_24, C_TEXT);
        lv_obj_t *st = txt(hdr, status_label(s.status).c_str(), &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(st, status_color(s.status), 0);
        txt(detail, s.model.length() ? s.model.c_str() : "--", &lv_font_montserrat_14, C_MUTED);

        // ports (all connectors of this station); tap a port to target it with the actions
        if (g_connectors.stationId == s.id && g_connectors.count > 0) {
            if (g_selPort >= g_connectors.count) g_selPort = 0;
            lv_obj_t *ports = plain(detail);
            lv_obj_set_width(ports, LV_PCT(100));
            lv_obj_set_height(ports, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_top(ports, 4, 0);
            lv_obj_set_flex_flow(ports, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(ports, 6, 0);
            for (int i = 0; i < g_connectors.count; i++) {
                Connector &k = g_connectors.items[i];
                bool sel = (i == g_selPort);
                lv_obj_t *pr = lv_obj_create(ports);
                lv_obj_clear_flag(pr, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_width(pr, LV_PCT(100));
                lv_obj_set_height(pr, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_color(pr, lv_color_hex(sel ? C_BORDER : C_ELEV), 0);
                lv_obj_set_style_border_color(pr, lv_color_hex(C_PRIMARY), 0);
                lv_obj_set_style_border_width(pr, sel ? 2 : 0, 0);
                lv_obj_set_style_radius(pr, 8, 0);
                lv_obj_set_style_pad_hor(pr, 14, 0);
                lv_obj_set_style_pad_ver(pr, 16, 0);
                lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_add_flag(pr, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(pr, on_port, LV_EVENT_CLICKED, (void *)(intptr_t)i);
                String lbl = "Port " + String(k.connectorId);
                if (k.type.length()) lbl += "  -  " + k.type;
                if (k.powerKw > 0) lbl += "  -  " + String(k.powerKw, 0) + " kW";
                txt(pr, lbl.c_str(), &lv_font_montserrat_14, C_TEXT);
                lv_obj_t *cs = txt(pr, status_label(k.status).c_str(), &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(cs, status_color(k.status), 0);
                if (i < MAX_CONNECTORS) g_portStat[i] = cs; // for in-place status updates
            }
            g_portStatN = g_connectors.count;
            g_portKey = String(s.id) + "|" + String(g_connectors.count) + "|" + String(g_selPort);
            for (int i = 0; i < g_connectors.count; i++)
                g_portKey += "|" + String(g_connectors.items[i].connectorId);
            if (g_selPort < g_connectors.count)
                g_portKey += "|sel:" + g_connectors.items[g_selPort].status;
        }

        if (s.status == "charging" && g_session.active && g_session.stationId == s.id) {
            lv_obj_t *stats = plain(detail);
            lv_obj_set_width(stats, LV_PCT(100));
            lv_obj_set_height(stats, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_top(stats, 6, 0);
            lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            g_statVal[0] = stat(stats, "Elapsed", elapsed_str(g_session.startedAtEpoch));
            g_statVal[1] = stat(stats, "Energy", String(g_session.energyKwh, 2) + " kWh");
            g_statVal[2] = stat(stats, "Power", String(g_session.powerKw, 1) + " kW");
            g_statVal[3] = stat(stats, "Cost", "$" + String(g_session.costCents / 100.0, 2));

            if (g_session.sampleCount >= 2) {
                int n = g_session.sampleCount;
                long span = (g_session.sampleEpoch[0] > 0)
                                ? (long)g_session.sampleEpoch[n - 1] - (long)g_session.sampleEpoch[0] : 0;
                if (span < 0) span = 0;

                // Caption (left) + Power/Energy toggle (right).
                lv_obj_t *caprow = plain(detail);
                lv_obj_set_width(caprow, LV_PCT(100));
                lv_obj_set_height(caprow, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(caprow, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(caprow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
                g_chartCap = txt(caprow, "Power (kW) vs time", &lv_font_montserrat_12, C_MUTED);
                lv_obj_t *tog = plain(caprow);
                lv_obj_set_size(tog, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(tog, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_column(tog, 4, 0);
                const char *tnames[2] = {"Power", "Energy"};
                for (int i = 0; i < 2; i++) {
                    lv_obj_t *b = lv_btn_create(tog);
                    lv_obj_set_height(b, LV_SIZE_CONTENT);
                    lv_obj_set_style_pad_hor(b, 12, 0);
                    lv_obj_set_style_pad_ver(b, 5, 0);
                    lv_obj_set_style_radius(b, 6, 0);
                    lv_obj_set_style_bg_color(b, lv_color_hex(g_chartMode == i ? C_PRIMARY : C_ELEV), 0);
                    lv_obj_add_event_cb(b, on_chart_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
                    lv_obj_t *l = lv_label_create(b);
                    lv_label_set_text(l, tnames[i]);
                    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
                    lv_obj_center(l);
                    g_chartTog[i] = b;
                }

                // Y labels (peak / 0) placed beside the plot, no fragile auto ticks.
                lv_obj_t *crow = plain(detail);
                lv_obj_set_width(crow, LV_PCT(100));
                lv_obj_set_height(crow, 150);
                lv_obj_set_flex_flow(crow, LV_FLEX_FLOW_ROW);
                lv_obj_set_style_pad_column(crow, 8, 0);

                lv_obj_t *ycol = plain(crow);
                lv_obj_set_width(ycol, 46);
                lv_obj_set_height(ycol, LV_PCT(100));
                lv_obj_set_flex_flow(ycol, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(ycol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
                g_chartPeak = txt(ycol, "0", &lv_font_montserrat_12, C_MUTED);
                txt(ycol, "0", &lv_font_montserrat_12, C_MUTED);

                lv_obj_t *chart = lv_chart_create(crow);
                lv_obj_set_flex_grow(chart, 1);
                lv_obj_set_height(chart, LV_PCT(100));
                lv_obj_set_style_bg_color(chart, lv_color_hex(C_ELEV), 0);
                lv_obj_set_style_border_width(chart, 0, 0);
                lv_obj_set_style_radius(chart, 10, 0);
                lv_obj_set_style_pad_all(chart, 8, 0);
                lv_obj_set_style_line_color(chart, lv_color_hex(C_BORDER), LV_PART_MAIN);
                lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
                lv_chart_set_div_line_count(chart, 3, 4);
                lv_chart_series_t *ser = lv_chart_add_series(chart, lv_color_hex(C_GREEN), LV_CHART_AXIS_PRIMARY_Y);
                lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
                lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
                lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
                g_chartObj = chart; g_chartSer = ser;
                chart_apply(g_session); // fills data, range, peak label and caption for the active mode

                // X labels: start (0:00) and elapsed (end), aligned under the plot.
                lv_obj_t *xrow = plain(detail);
                lv_obj_set_width(xrow, LV_PCT(100));
                lv_obj_set_height(xrow, LV_SIZE_CONTENT);
                lv_obj_set_style_pad_left(xrow, 54, 0); // ycol width (46) + column gap (8)
                lv_obj_set_flex_flow(xrow, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(xrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                txt(xrow, "0:00", &lv_font_montserrat_12, C_MUTED);
                char eb[12]; snprintf(eb, sizeof(eb), "%d:%02d", (int)(span / 60), (int)(span % 60));
                g_chartEnd = txt(xrow, eb, &lv_font_montserrat_12, C_MUTED);
            }
            // a change here (charging stopped, or chart appeared) forces a structural rebuild
            g_sessKey = String(s.id) + "|1|" + String(g_session.sampleCount >= 2);
        }

        lv_obj_t *grow = plain(detail);
        lv_obj_set_width(grow, LV_PCT(100));
        lv_obj_set_flex_grow(grow, 1);

        lv_obj_t *actions = plain(detail);
        lv_obj_set_width(actions, LV_PCT(100));
        lv_obj_set_height(actions, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(actions, 10, 0);
        // Gate the actions on the selected port's status (fall back to station status).
        String aStatus = s.status;
        if (g_connectors.stationId == s.id && g_selPort >= 0 && g_selPort < g_connectors.count)
            aStatus = g_connectors.items[g_selPort].status;
        bool startable = (aStatus == "available" || aStatus == "preparing" ||
                          aStatus == "occupied" || aStatus == "ev_connected");
        bool stoppable = (aStatus == "charging" || aStatus == "suspended_ev" ||
                          aStatus == "suspended_evse");
        abtn(actions, LV_SYMBOL_PLAY "  Start", C_GREEN, 0, startable);
        abtn(actions, LV_SYMBOL_STOP "  Stop", C_RED, 1, stoppable);
        // Simulators get a Simulate button that opens a Plug In / Unplug popup.
        if (s.isSimulator) {
            lv_obj_t *sb = lv_btn_create(actions);
            lv_obj_set_height(sb, 52);
            lv_obj_set_flex_grow(sb, 1);
            lv_obj_set_style_bg_color(sb, lv_color_hex(C_ELEV), 0);
            lv_obj_set_style_radius(sb, 10, 0);
            lv_obj_set_style_shadow_width(sb, 0, 0);
            lv_obj_add_event_cb(sb, on_simulate, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *sl = lv_label_create(sb);
            lv_label_set_text(sl, LV_SYMBOL_SETTINGS "  Simulate");
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(sl, lv_color_hex(0xffffff), 0);
            lv_obj_center(sl);
        }
    } else {
        lv_obj_t *hint = txt(detail, "Select a station", &lv_font_montserrat_16, C_MUTED);
        lv_obj_center(hint);
    }
}

static void field(lv_obj_t *p, const char *k, const String &v) {
    lv_obj_t *row = plain(p);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_ver(row, 11, 0);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    txt(row, k, &lv_font_montserrat_14, C_MUTED);
    txt(row, v.length() ? v.c_str() : "--", &lv_font_montserrat_16, C_TEXT);
}

static void build_settings(lv_obj_t *parent) {
    lv_obj_t *cc = card(parent);
    lv_obj_set_size(cc, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(cc, LV_FLEX_FLOW_COLUMN);
    txt(cc, "Connection", &lv_font_montserrat_20, C_TEXT);
    field(cc, "Device ID", g_settings.devName);
    field(cc, "API URL", g_settings.csmsUrl);
    // Never show the full key: last 4 characters only.
    String keyMask = g_settings.apiKey.length() >= 4
                         ? "...." + g_settings.apiKey.substring(g_settings.apiKey.length() - 4)
                         : (g_settings.apiKey.length() ? "(set)" : "");
    field(cc, "API Key", keyMask);
    field(cc, "Site ID", g_settings.siteId);
    field(cc, "Site Address", g_site_address);
    field(cc, "WiFi", g_settings.wifiSsid);
    field(cc, "CSMS Version", g_csms_version);
    field(cc, "Firmware", String(FW_VERSION));
}

static void step(lv_obj_t *p, const char *num, const char *label, const String &value) {
    lv_obj_t *row = plain(p);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);

    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 34, 34);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(C_PRIMARY), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_t *bl = lv_label_create(badge);
    lv_label_set_text(bl, num);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(0xffffff), 0);
    lv_obj_center(bl);

    lv_obj_t *col = plain(row);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    txt(col, label, &lv_font_montserrat_16, C_TEXT);
    if (value.length()) txt(col, value.c_str(), &lv_font_montserrat_20, C_GREEN);
}

static void build_provision(lv_obj_t *scr) {
    lv_obj_t *wrap = plain(scr);
    lv_obj_set_size(wrap, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wrap, 6, 0);

    make_logo(wrap, LOGO_BIG);
    txt(wrap, "EVtivity Panel", &lv_font_montserrat_28, C_TEXT);
    lv_obj_t *sub = txt(wrap, "Set up this panel to begin", &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_pad_bottom(sub, 12, 0);

    lv_obj_t *c = card(wrap);
    lv_obj_set_width(c, 620);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 16, 0);

    String ssid = g_ap_ssid.length() ? g_ap_ssid : String(AP_SSID_PREFIX) + "XXXX";
    String login = "Login " + g_settings.setupUser + " / " + g_settings.setupPass;
    step(c, "1", "Join this Wi-Fi network", ssid);
    step(c, "2", "Open this address in a browser", String("http://") + AP_IP_OCTET);
    step(c, "3", "Sign in, then enter your CSMS details", login);
}

static void build_main(lv_obj_t *scr) {
    // top bar
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_HOR_RES, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_hor(bar, 18, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_logo(bar, LOGO_TOP);
    lv_obj_t *nm = txt(bar, "EVtivity", &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_style_pad_left(nm, 2, 0);
    lv_obj_t *site = txt(bar, g_settings.devName.c_str(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_style_pad_left(site, 10, 0);

    lv_obj_t *sp = plain(bar);
    lv_obj_set_height(sp, 1);
    lv_obj_set_flex_grow(sp, 1);

    lv_obj_t *tabwrap = plain(bar);
    lv_obj_set_size(tabwrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tabwrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabwrap, 6, 0);

    const char *tabs[2] = {"Charging", "Settings"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = lv_obj_create(tabwrap);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(t, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_PRIMARY), 0);
        lv_obj_set_style_bg_opa(t, i == g_tab ? LV_OPA_COVER : 0, 0);
        lv_obj_set_style_pad_hor(t, 16, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, [](lv_event_t *e) {
            g_tab = (int)(intptr_t)lv_event_get_user_data(e);
            ui_rebuild();
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *tl = lv_label_create(t);
        lv_label_set_text(tl, tabs[i]);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(0xffffff), 0);
        lv_obj_center(tl);
        g_tab_btn[i] = t;
    }

    g_wifi_lbl = txt(bar, LV_SYMBOL_WIFI, &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_pad_left(g_wifi_lbl, 18, 0);
    lv_obj_set_style_text_color(g_wifi_lbl,
        lv_color_hex(g_net_state == NET_ONLINE ? C_GREEN : g_net_state == NET_OFFLINE ? C_RED : C_MUTED), 0);

    // content
    g_content = lv_obj_create(scr);
    lv_obj_clear_flag(g_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_content, LV_HOR_RES, LV_VER_RES - 56);
    lv_obj_set_pos(g_content, 0, 56);
    lv_obj_set_style_bg_color(g_content, lv_color_hex(C_BG), 0);
    lv_obj_set_style_radius(g_content, 0, 0);
    lv_obj_set_style_border_width(g_content, 0, 0);
    lv_obj_set_style_pad_all(g_content, 14, 0);

    ui_rebuild();
}

static void banner(lv_obj_t *parent, const char *sym, const String &text, uint32_t color) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, 36, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_all(b, 12, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 10, 0);
    txt(b, sym, &lv_font_montserrat_20, color);
    lv_obj_t *l = txt(b, text.c_str(), &lv_font_montserrat_14, C_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(l, 1);
}

void ui_rebuild() {
    if (!g_content) return;
    // The in-place refs point into the subtree we are about to delete; clear them.
    g_portStatN = 0; g_portKey = "";
    for (int i = 0; i < 4; i++) g_statVal[i] = nullptr;
    g_chartObj = nullptr; g_chartSer = nullptr; g_chartPeak = nullptr; g_chartEnd = nullptr;
    g_chartCap = nullptr; g_chartTog[0] = nullptr; g_chartTog[1] = nullptr; g_sessKey = "";
    g_listRowN = 0; g_listKey = ""; g_detail = nullptr;
    lv_obj_clean(g_content);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_content, 12, 0);

    if (g_net_state == NET_OFFLINE)
        banner(g_content, LV_SYMBOL_WIFI, "Wi-Fi disconnected. Reconnecting...", C_RED);
    if (g_perm_warn.length())
        banner(g_content, LV_SYMBOL_WARNING, g_perm_warn, C_AMBER);

    lv_obj_t *body = plain(g_content);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    if (g_tab == 1) build_settings(body);
    else build_charging(body);

    for (int i = 0; i < 2; i++) {
        if (g_tab_btn[i]) lv_obj_set_style_bg_opa(g_tab_btn[i], i == g_tab ? LV_OPA_COVER : 0, 0);
    }
}

static void ui_render_root() {
    lv_obj_t *scr = lv_scr_act();
    g_content = nullptr;
    g_wifi_lbl = nullptr;
    g_tab_btn[0] = g_tab_btn[1] = nullptr;
    lv_obj_clean(scr);
    if (prov_mode()) build_provision(scr);
    else build_main(scr);
}

void ui_init(const Settings &s) {
    g_settings = s;
    g_net_state = s.configured ? NET_CONNECTING : NET_PROVISION;
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    ui_render_root();
}

void ui_set_snapshot(const SiteSnapshot &snap) {
    String prevSelStatus = (g_sel < g_snap.count) ? g_snap.stations[g_sel].status : String("");
    g_snap = snap;
    if (g_sel >= g_snap.count) g_sel = 0;
    if (prov_mode()) return;
    if (g_wifi_lbl) lv_obj_set_style_text_color(g_wifi_lbl, lv_color_hex(snap.valid ? C_GREEN : C_MUTED), 0);
    if (g_tab != 0) return;
    // Same set of stations: update each row's status in place. Rebuilding the whole
    // list every poll is heavy with many stations and flashes (it also overran the
    // LVGL pool on big sites). Full rebuild only when the station set changes.
    String key = String(snap.count);
    for (int i = 0; i < snap.count; i++) key += "|" + snap.stations[i].ocppId;
    if (key == g_listKey && g_listRowN == snap.count) {
        for (int i = 0; i < snap.count && i < g_listRowN && i < MAX_STATIONS; i++) {
            if (!g_rowStat[i]) continue;
            String lbl = status_label(snap.stations[i].status);
            // Skip if unchanged: lv_label_set_text always invalidates, so writing the
            // same text every poll would redraw the whole list and flicker.
            if (lbl == lv_label_get_text(g_rowStat[i])) continue;
            lv_label_set_text(g_rowStat[i], lbl.c_str());
            lv_obj_set_style_text_color(g_rowStat[i], status_color(snap.stations[i].status), 0);
            if (g_listRow[i])
                lv_obj_set_style_border_color(g_listRow[i], status_color(snap.stations[i].status), 0);
        }
        // The selected station's status feeds the detail header and action buttons.
        String selStatus = (g_sel < snap.count) ? snap.stations[g_sel].status : String("");
        if (selStatus != prevSelStatus) g_detailDirty = true;
        return;
    }
    ui_rebuild();
}

void ui_set_status(NetState st, const String &apSsid) {
    bool was = prov_mode();
    g_net_state = st;
    if (apSsid.length()) g_ap_ssid = apSsid;
    if (was != prov_mode() || prov_mode()) {
        ui_render_root();
        return;
    }
    if (g_wifi_lbl)
        lv_obj_set_style_text_color(g_wifi_lbl,
            lv_color_hex(st == NET_ONLINE ? C_GREEN : st == NET_OFFLINE ? C_RED : C_MUTED), 0);
    ui_rebuild();   // refresh the Wi-Fi disconnected banner + empty-state text on any tab
}

void ui_set_csms_version(const String &version) {
    if (version == g_csms_version) return;
    g_csms_version = version;
    if (!prov_mode() && g_tab == 1) ui_rebuild();
}

void ui_set_perm_status(const String &missing, const String &err) {
    String w;
    if (missing.length()) w = "API key missing: " + missing;
    else if (err.length()) w = "Permission check failed: " + err;
    if (w == g_perm_warn) return;
    g_perm_warn = w;
    if (!prov_mode()) ui_rebuild();
}

String ui_selected_station_id() {
    if (prov_mode() || g_snap.count == 0 || g_sel >= g_snap.count) return "";
    Station &s = g_snap.stations[g_sel];
    return (s.status == "charging") ? s.id : String("");
}

void ui_set_session(const SessionDetail &sd) {
    g_session = sd;
    if (prov_mode() || g_tab != 0) return;
    // Structure key must match build_charging's g_sessKey exactly.
    String key = String(sd.stationId) + "|" + String(sd.active) + "|" + String(sd.sampleCount >= 2);
    if (sd.active && key == g_sessKey && g_statVal[0]) {
        // Same layout: update the live values in place, only those that changed.
        String v0 = elapsed_str(sd.startedAtEpoch);
        String v1 = String(sd.energyKwh, 2) + " kWh";
        String v2 = String(sd.powerKw, 1) + " kW";
        String v3 = "$" + String(sd.costCents / 100.0, 2);
        if (v0 != lv_label_get_text(g_statVal[0])) lv_label_set_text(g_statVal[0], v0.c_str());
        if (v1 != lv_label_get_text(g_statVal[1])) lv_label_set_text(g_statVal[1], v1.c_str());
        if (v2 != lv_label_get_text(g_statVal[2])) lv_label_set_text(g_statVal[2], v2.c_str());
        if (v3 != lv_label_get_text(g_statVal[3])) lv_label_set_text(g_statVal[3], v3.c_str());
        if (g_chartObj && g_chartSer && sd.sampleCount >= 2) {
            chart_apply(sd); // data + range + peak label + caption for the active mode
            if (g_chartEnd) {
                long span = (sd.sampleEpoch[0] > 0)
                                ? (long)sd.sampleEpoch[sd.sampleCount - 1] - (long)sd.sampleEpoch[0] : 0;
                if (span < 0) span = 0;
                char eb[12]; snprintf(eb, sizeof(eb), "%d:%02d", (int)(span / 60), (int)(span % 60));
                if (String(eb) != lv_label_get_text(g_chartEnd)) lv_label_set_text(g_chartEnd, eb);
            }
        }
        return;
    }
    g_detailDirty = true; // coalesced into one detail rebuild at ui_commit_detail()
}

void ui_set_site_address(const String &addr) {
    if (addr == g_site_address) return;
    g_site_address = addr;
    if (!prov_mode() && g_tab == 1) ui_rebuild();
}

String ui_selected_station_internal_id() {
    if (prov_mode() || g_snap.count == 0 || g_sel >= g_snap.count) return "";
    return g_snap.stations[g_sel].id;
}

void ui_set_connectors(const ConnectorSet &set) {
    g_connectors = set;
    if (prov_mode() || g_tab != 0) return;
    // Structure key must match build_charging's g_portKey exactly.
    String key = String(set.stationId) + "|" + String(set.count) + "|" + String(g_selPort);
    for (int i = 0; i < set.count; i++) key += "|" + String(set.items[i].connectorId);
    if (g_selPort < set.count) key += "|sel:" + set.items[g_selPort].status;
    if (key == g_portKey && g_portStatN == set.count) {
        // Same layout: update the port status chips in place, no teardown (a full rebuild flashes).
        for (int i = 0; i < set.count && i < g_portStatN && i < MAX_CONNECTORS; i++) {
            if (!g_portStat[i]) continue;
            String lbl = status_label(set.items[i].status);
            if (lbl == lv_label_get_text(g_portStat[i])) continue; // unchanged: skip redraw
            lv_label_set_text(g_portStat[i], lbl.c_str());
            lv_obj_set_style_text_color(g_portStat[i], status_color(set.items[i].status), 0);
        }
        return;
    }
    g_detailDirty = true; // coalesced into one detail rebuild at ui_commit_detail()
}

// Apply at most one detail rebuild after a poll's fetches, so a transition does
// not rebuild the detail several times in a row (which flickers).
void ui_commit_detail() {
    if (!g_detailDirty) return;
    g_detailDirty = false;
    rebuild_detail();
}

void ui_set_command_result(const String &action, bool ok, const String &msg) {
    if (!g_popup || !g_popupCard) return;
    if (g_popupSpin) { lv_obj_del(g_popupSpin); g_popupSpin = nullptr; }
    if (g_popupText) {
        String text = ok ? (action + " succeeded") : (action + " failed:\n" + msg);
        lv_label_set_text(g_popupText, text.c_str());
        lv_obj_set_style_text_color(g_popupText, lv_color_hex(ok ? C_GREEN : C_RED), 0);
    }
    add_ok_button(g_popupCard);
}

void ui_set_command_cb(void (*cb)(const Command &)) { g_cmd_cb = cb; }
