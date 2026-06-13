/* Portable LVGL UI for the EVtivity CSMS panel.
 * These build functions use only LVGL widgets and run unchanged on the device;
 * the simulator feeds them sample data so the docs can show pixel-true renders. */
#include "lvgl.h"
#include "screens.h"
#include <stdbool.h>

LV_IMAGE_DECLARE(logo32);
LV_IMAGE_DECLARE(logo96);

/* ---- theme tokens (dark) ---------------------------------------------- */
#define C_BG       0x0f172a
#define C_CARD     0x1e293b
#define C_ELEV     0x273449
#define C_BORDER   0x334155
#define C_TEXT     0xf1f5f9
#define C_MUTED    0x94a3b8
#define C_PRIMARY  0x3b82f6
#define C_GREEN    0x22c55e
#define C_AMBER    0xf59e0b
#define C_RED      0xef4444
#define C_CYAN     0x06b6d4
#define C_ORANGE   0xf97316

typedef struct {
    const char *id;
    const char *model;
    const char *status; /* available|charging|faulted|offline|reserved|preparing */
    const char *connector;
} Station;

static const Station STATIONS[] = {
    {"CS-0001", "Alpitronic HYC", "charging", "CCS2 - 150 kW"},
    {"CS-0002", "ABB Terra 184", "available", "CCS2 - 180 kW"},
    {"CS-0003", "Kempower S", "faulted", "CCS2 - 400 kW"},
    {"CS-0004", "Wallbox Pulsar", "available", "Type2 - 22 kW"},
    {"CS-0005", "Tesla SC", "reserved", "NACS - 250 kW"},
    {"CS-0006", "Siemens VC", "offline", "Type2 - 11 kW"},
};
#define STATION_COUNT (int)(sizeof(STATIONS) / sizeof(STATIONS[0]))

static lv_color_t status_color(const char *s) {
    if (!lv_strcmp(s, "charging")) return lv_color_hex(C_GREEN);
    if (!lv_strcmp(s, "available")) return lv_color_hex(C_PRIMARY);
    if (!lv_strcmp(s, "faulted")) return lv_color_hex(C_RED);
    if (!lv_strcmp(s, "reserved")) return lv_color_hex(C_ORANGE);
    if (!lv_strcmp(s, "preparing")) return lv_color_hex(C_CYAN);
    return lv_color_hex(C_MUTED);
}

static const char *status_label(const char *s) {
    if (!lv_strcmp(s, "charging")) return "Charging";
    if (!lv_strcmp(s, "available")) return "Available";
    if (!lv_strcmp(s, "faulted")) return "Faulted";
    if (!lv_strcmp(s, "reserved")) return "Reserved";
    if (!lv_strcmp(s, "preparing")) return "Preparing";
    return "Offline";
}

/* ---- helpers ---------------------------------------------------------- */
static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

static lv_obj_t *panel(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 14, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static lv_obj_t *badge(lv_obj_t *parent, const char *txt, lv_color_t color) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_bg_opa(b, 38, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 5, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return b;
}

static lv_obj_t *action_btn(lv_obj_t *parent, const char *sym, const char *txt,
                            uint32_t color, bool enabled) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 54);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, enabled ? LV_OPA_COVER : 70, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_fmt(l, "%s  %s", sym, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(l, enabled ? LV_OPA_COVER : 130, 0);
    lv_obj_center(l);
    return b;
}

/* ---- top bar ---------------------------------------------------------- */
static void build_topbar(lv_obj_t *parent, const char *active) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, 800, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_hor(bar, 18, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *logo = lv_image_create(bar);
    lv_image_set_src(logo, &logo32);
    lv_obj_t *name = label(bar, "EVtivity", &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_style_pad_left(name, 8, 0);
    lv_obj_t *site = label(bar, "Main Campus", &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_style_pad_left(site, 10, 0);

    lv_obj_t *spacer = plain(bar);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);

    const char *tabs[2] = {"Charging", "Settings"};
    for (int i = 0; i < 2; i++) {
        bool on = !lv_strcmp(active, tabs[i]);
        lv_obj_t *t = lv_obj_create(bar);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(t, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_PRIMARY), 0);
        lv_obj_set_style_bg_opa(t, on ? LV_OPA_COVER : 0, 0);
        lv_obj_set_style_pad_hor(t, 16, 0);
        lv_obj_set_style_margin_left(t, 6, 0);
        lv_obj_t *tl = lv_label_create(t);
        lv_label_set_text(tl, tabs[i]);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(on ? 0xffffff : C_MUTED), 0);
        lv_obj_center(tl);
    }

    lv_obj_t *wifi = label(bar, LV_SYMBOL_WIFI, &lv_font_montserrat_16, C_GREEN);
    lv_obj_set_style_pad_left(wifi, 18, 0);
    lv_obj_t *cnt = label(bar, "5/6 online", &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_style_pad_left(cnt, 8, 0);
}

static lv_obj_t *body_create(lv_obj_t *parent) {
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(body, 800, 424);
    lv_obj_set_pos(body, 0, 56);
    lv_obj_set_style_bg_color(body, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 14, 0);
    return body;
}

/* ---- charging view ---------------------------------------------------- */
static void build_charging(lv_obj_t *root) {
    build_topbar(root, "Charging");
    lv_obj_t *body = body_create(root);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 14, 0);

    lv_obj_t *list = lv_obj_create(body);
    lv_obj_set_size(list, 332, LV_PCT(100));
    lv_obj_set_style_bg_color(list, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < STATION_COUNT; i++) {
        const Station *s = &STATIONS[i];
        bool sel = (i == 0);
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_set_style_bg_color(row, lv_color_hex(sel ? C_ELEV : C_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 12, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, status_color(s->status), 0);
        lv_obj_set_style_border_width(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *col = plain(row);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(col, 2, 0);
        label(col, s->id, &lv_font_montserrat_16, C_TEXT);
        label(col, s->connector, &lv_font_montserrat_12, C_MUTED);

        badge(row, status_label(s->status), status_color(s->status));
    }

    lv_obj_t *detail = panel(body);
    lv_obj_set_flex_grow(detail, 1);
    lv_obj_set_height(detail, LV_PCT(100));
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail, 12, 0);
    lv_obj_set_scrollbar_mode(detail, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *hdr = plain(detail);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(hdr, "CS-0001", &lv_font_montserrat_24, C_TEXT);
    lv_obj_t *hsp = plain(hdr);
    lv_obj_set_height(hsp, 1);
    lv_obj_set_flex_grow(hsp, 1);
    badge(hdr, "Charging", lv_color_hex(C_GREEN));

    label(detail, "Alpitronic HYC - CCS2 - 150 kW", &lv_font_montserrat_14, C_MUTED);

    lv_obj_t *tiles = plain(detail);
    lv_obj_set_width(tiles, LV_PCT(100));
    lv_obj_set_height(tiles, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tiles, 10, 0);
    const char *tk[3] = {"Power", "Energy", "Duration"};
    const char *tv[3] = {"7.4 kW", "12.3 kWh", "00:42"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *tile = lv_obj_create(tiles);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(tile, 1);
        lv_obj_set_height(tile, 76);
        lv_obj_set_style_bg_color(tile, lv_color_hex(C_ELEV), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_pad_all(tile, 12, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(tile, 4, 0);
        label(tile, tk[i], &lv_font_montserrat_12, C_MUTED);
        label(tile, tv[i], &lv_font_montserrat_24, C_TEXT);
    }

    lv_obj_t *grow = plain(detail);
    lv_obj_set_width(grow, LV_PCT(100));
    lv_obj_set_flex_grow(grow, 1);

    lv_obj_t *actions = plain(detail);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 10, 0);
    action_btn(actions, LV_SYMBOL_PLAY, "Start", C_GREEN, false);
    action_btn(actions, LV_SYMBOL_STOP, "Stop", C_RED, true);
    action_btn(actions, LV_SYMBOL_WARNING, "Clear", C_AMBER, false);
    action_btn(actions, LV_SYMBOL_SETTINGS, "Simulate", C_ELEV, true);
}

/* ---- settings view ---------------------------------------------------- */
static void field(lv_obj_t *parent, const char *k, const char *v) {
    lv_obj_t *row = plain(parent);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_ver(row, 11, 0);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(row, k, &lv_font_montserrat_14, C_MUTED);
    label(row, v, &lv_font_montserrat_16, C_TEXT);
}

static void build_settings(lv_obj_t *root) {
    build_topbar(root, "Settings");
    lv_obj_t *body = body_create(root);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 14, 0);

    lv_obj_t *left = panel(body);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_OFF);
    label(left, "Connection", &lv_font_montserrat_20, C_TEXT);
    field(left, "CSMS URL", "csms.example.com");
    field(left, "API Key", LV_SYMBOL_EYE_CLOSE "  ....3f2a");
    field(left, "Site ID", "sit_vlraxerzhwi6");
    field(left, "WiFi", "depot-iot  " LV_SYMBOL_WIFI);
    field(left, "IP Address", "10.0.4.18");

    lv_obj_t *right = panel(body);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(right, LV_SCROLLBAR_MODE_OFF);
    label(right, "Device", &lv_font_montserrat_20, C_TEXT);

    lv_obj_t *brow = plain(right);
    lv_obj_set_style_pad_ver(brow, 12, 0);
    lv_obj_set_width(brow, LV_PCT(100));
    lv_obj_set_height(brow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(brow, 8, 0);
    label(brow, "Brightness", &lv_font_montserrat_14, C_MUTED);
    lv_obj_t *sl = lv_slider_create(brow);
    lv_obj_set_width(sl, LV_PCT(100));
    lv_slider_set_value(sl, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_PRIMARY), LV_PART_KNOB);

    field(right, "Refresh interval", "5 s");
    field(right, "Firmware", "v1.0.0");

    lv_obj_t *mrow = plain(right);
    lv_obj_set_style_pad_top(mrow, 18, 0);
    lv_obj_set_width(mrow, LV_PCT(100));
    lv_obj_set_height(mrow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mrow, 10, 0);
    action_btn(mrow, LV_SYMBOL_REFRESH, "Reconnect", C_PRIMARY, true);
    action_btn(mrow, LV_SYMBOL_POWER, "Reboot", C_ELEV, true);
    action_btn(mrow, LV_SYMBOL_TRASH, "Reset", C_RED, true);
}

/* ---- provisioning view ------------------------------------------------ */
static void build_provision(lv_obj_t *root) {
    lv_obj_t *card = panel(root);
    lv_obj_set_size(card, 560, 384);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 28, 0);
    lv_obj_set_style_pad_row(card, 10, 0);

    lv_obj_t *icon = lv_image_create(card);
    lv_image_set_src(icon, &logo96);

    lv_obj_t *t = label(card, "Set up this panel", &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(t, LV_PCT(100));

    lv_obj_t *s = label(card,
                        "1.  Join the WiFi network below\n"
                        "2.  Open  http://192.168.4.1\n"
                        "3.  Enter WiFi, CSMS URL, API key, site",
                        &lv_font_montserrat_16, C_MUTED);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s, LV_PCT(100));

    lv_obj_t *ap = lv_obj_create(card);
    lv_obj_remove_flag(ap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ap, lv_color_hex(C_ELEV), 0);
    lv_obj_set_style_bg_opa(ap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ap, 0, 0);
    lv_obj_set_style_radius(ap, 10, 0);
    lv_obj_set_style_pad_hor(ap, 22, 0);
    lv_obj_set_style_pad_ver(ap, 12, 0);
    lv_obj_set_style_margin_top(ap, 6, 0);
    lv_obj_t *apl = lv_label_create(ap);
    lv_label_set_text(apl, LV_SYMBOL_WIFI "   EVtivity-Panel-3F2A");
    lv_obj_set_style_text_font(apl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(apl, lv_color_hex(C_GREEN), 0);

    lv_obj_t *wait = plain(card);
    lv_obj_set_style_margin_top(wait, 6, 0);
    lv_obj_set_size(wait, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wait, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wait, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wait, 10, 0);
    lv_obj_t *sp = lv_spinner_create(wait);
    lv_obj_set_size(sp, 22, 22);
    lv_obj_set_style_arc_color(sp, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, lv_color_hex(C_PRIMARY), LV_PART_INDICATOR);
    label(wait, "Waiting for configuration", &lv_font_montserrat_14, C_MUTED);
}

/* ---- public ----------------------------------------------------------- */
void screens_theme_init(void) {
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(d, lv_color_hex(C_PRIMARY),
                                           lv_color_hex(C_GREEN), true, &lv_font_montserrat_14);
    lv_display_set_theme(d, th);
}

void screens_build(const char *name) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    if (!lv_strcmp(name, "settings")) build_settings(root);
    else if (!lv_strcmp(name, "provision")) build_provision(root);
    else build_charging(root);
}
