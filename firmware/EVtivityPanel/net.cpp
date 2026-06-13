#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_mac.h>

static Settings *g = nullptr;
static NetState state = NET_OFFLINE;
static bool provisioning = false;
static String apSsid;
static WebServer server(WEB_PORT);
static DNSServer dns;
static uint32_t connectStartMs = 0;
static bool ntpStarted = false;

// The real EVtivity brand mark. A browser renders SVG directly, so inline it.
static const char LOGO_SVG[] PROGMEM =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 120 120' class=logo>"
    "<defs><mask id='rg'><rect width='120' height='120' fill='white'/>"
    "<polygon points='68.82,-8.24 76.70,-6.86 69.82,32.54 61.94,31.16' fill='black'/>"
    "<polygon points='52.08,87.46 59.96,88.84 53.08,128.24 45.20,126.86' fill='black'/></mask></defs>"
    "<circle cx='60' cy='60' r='50' fill='none' stroke='#22c55e' stroke-width='12' mask='url(#rg)'/>"
    "<g transform='translate(60 60) scale(0.95) translate(-60 -60)'>"
    "<path d='M68 20L38 68h22l-6 32 30-48H62l6-32z' fill='#22c55e'/></g></svg>";

static const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>EVtivity Panel Setup</title><style>
body{font-family:system-ui;background:#0f172a;color:#f1f5f9;margin:0;padding:24px}
.card{max-width:460px;margin:0 auto;background:#1e293b;border:1px solid #334155;border-radius:14px;padding:24px}
.logo{width:64px;height:64px;display:block;margin:0 auto 10px}
h1{font-size:22px;margin:0 0 4px;text-align:center}p{color:#94a3b8;margin:0 0 18px;font-size:14px;text-align:center}
label{display:block;font-size:13px;color:#94a3b8;margin:12px 0 6px}
input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #334155;background:#273449;color:#f1f5f9}
button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-size:16px}
.foot{margin:18px 0 0;font-size:12px;color:#64748b}
.spin{display:inline-block;width:16px;height:16px;margin-right:8px;border:2px solid #fff;border-top-color:transparent;border-radius:50%;animation:sp .8s linear infinite;vertical-align:middle}
@keyframes sp{to{transform:rotate(360deg)}}
</style></head><body><div class=card>{{LOGO}}<h1>EVtivity Panel</h1><p>Connect this panel to your CSMS.</p>
<form method=POST action=/save>
<label>WiFi network</label><input name=ssid value="{{SSID}}" required>
<label>WiFi password</label><input name=pass type=password placeholder="leave blank to keep">
<label>EVtivity API URL</label><input name=url value="{{URL}}" placeholder="http://host:7102" required>
<label>API key (64 hex)</label><input name=key placeholder="leave blank to keep">
<label>Site ID</label><input name=site value="{{SITE}}" placeholder="sit_..." required>
<hr style="border:0;border-top:1px solid #334155;margin:18px 0">
<label>Setup username</label><input name=setup_user value="{{SUSER}}" placeholder="admin">
<label>Setup password</label><input name=setup_pass type=password placeholder="leave blank to keep">
<button id=savebtn>Save and connect</button></form>
<p class=foot>Firmware {{FW}}</p></div>
<script>document.querySelector('form').addEventListener('submit',function(){var b=document.getElementById('savebtn');b.disabled=true;b.innerHTML='<span class=spin></span>Connecting...';});</script>
</body></html>
)HTML";

// Build a centered card page (logo + colored title + body html) and send it.
static void sendCard(const String &title, const char *color, const String &body) {
    String logo = FPSTR(LOGO_SVG);
    String html =
        "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><style>"
        "body{font-family:system-ui;background:#0f172a;color:#f1f5f9;margin:0;padding:24px}"
        ".card{max-width:440px;margin:0 auto;background:#1e293b;border:1px solid #334155;border-radius:16px;padding:40px;text-align:center}"
        ".logo{width:80px;height:80px;display:block;margin:0 auto 18px}"
        "h1{font-size:28px;margin:0 0 10px}p{color:#94a3b8;margin:0;font-size:16px}"
        "a{display:inline-block;margin-top:20px;padding:12px 28px;border-radius:8px;background:#3b82f6;color:#fff;text-decoration:none}"
        "</style></head><body><div class=card>" + logo +
        "<h1 style='color:" + color + "'>" + title + "</h1>" + body + "</div></body></html>";
    server.send(200, "text/html", html);
}

// The setup portal is gated by HTTP Basic Auth (default admin / admin123,
// changeable from the form itself). Credentials live in NVS.
static bool needsAuth() {
    if (server.authenticate(g->setupUser.c_str(), g->setupPass.c_str())) return false;
    server.requestAuthentication();
    return true;
}

static void handleRoot() {
    if (needsAuth()) return;
    String html = FPSTR(PORTAL_HTML);
    String logo = FPSTR(LOGO_SVG);
    html.replace("{{LOGO}}", logo);
    html.replace("{{FW}}", FW_VERSION);
    html.replace("{{SSID}}", g->wifiSsid);
    html.replace("{{URL}}", g->csmsUrl);
    html.replace("{{SITE}}", g->siteId);
    html.replace("{{SUSER}}", g->setupUser);
    server.send(200, "text/html", html);
}

static void handleSave() {
    if (needsAuth()) return;

    // Merge edits onto the current config; blank secret fields keep the current value.
    Settings t = *g;
    String v;
    v = server.arg("ssid"); if (v.length()) t.wifiSsid = v;
    v = server.arg("pass"); if (v.length()) t.wifiPass = v;
    v = server.arg("url"); v.replace(" ", ""); while (v.endsWith("/")) v.remove(v.length() - 1);
    if (v.length() && !v.startsWith("http://") && !v.startsWith("https://")) v = "http://" + v;
    if (v.length()) t.csmsUrl = v;
    v = server.arg("key"); v.trim(); if (v.length()) t.apiKey = v;
    v = server.arg("site"); v.trim(); if (v.length()) t.siteId = v;
    v = server.arg("setup_user"); v.trim(); if (v.length()) t.setupUser = v;
    v = server.arg("setup_pass"); if (v.length()) t.setupPass = v;

    // Pre-check that the network is in range. The password cannot be verified from
    // here: testing a station connect while the AP is up forces a radio channel change
    // that breaks this portal. The password and the API are confirmed on-device right
    // after the reboot, with this portal still reachable to fix anything.
    int n = WiFi.scanNetworks(false, false, false, 300);
    bool found = (n <= 0); // scan failed or empty: do not block
    for (int i = 0; i < n; i++)
        if (WiFi.SSID(i) == t.wifiSsid) { found = true; break; }
    WiFi.scanDelete();
    if (!found) {
        sendCard("Network not found", "#ef4444",
                 "<p>'" + t.wifiSsid + "' is not in range. Check the name, and that it is a 2.4 GHz "
                 "network (the panel does not support 5 GHz).</p><a href='/'>Back</a>");
        return;
    }

    *g = t;
    settings_save(*g);
    sendCard("Saved", "#22c55e", "<a href='/'>Go back</a>");
    delay(800);
    ESP.restart();
}

// Bring up the setup AP + captive portal. Always on (AP+STA) so 192.168.4.1 stays
// reachable to update settings, whether or not the panel is configured.
static void startAp() {
    apSsid = device_id();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid.c_str());
    dns.start(DNS_PORT, "*", WiFi.softAPIP());
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRoot); // captive portal: any path opens the form
    server.begin();
}

static void startProvisioning() {
    provisioning = true;
    state = NET_PROVISION;
    startAp();
}

static void startStation() {
    provisioning = false;
    state = NET_CONNECTING;
    startAp();   // keep the portal reachable at 192.168.4.1 while connected
    WiFi.begin(g->wifiSsid.c_str(), g->wifiPass.c_str());
    connectStartMs = millis();
}

void net_begin(Settings &s) {
    g = &s;
    if (!s.configured || s.wifiSsid.length() == 0) startProvisioning();
    else startStation();
}

void net_loop() {
    dns.processNextRequest();   // portal is always served (AP+STA)
    server.handleClient();
    if (provisioning) return;
    if (WiFi.status() == WL_CONNECTED) {
        state = NET_ONLINE;
        if (!ntpStarted) { configTime(0, 0, "pool.ntp.org", "time.nist.gov"); ntpStarted = true; }
        return;
    }
    // Not connected: CONNECTING during the first window, then OFFLINE. Retry periodically
    // so a dropped link (or a failed save) reconnects on its own.
    if (state != NET_OFFLINE && millis() - connectStartMs <= WIFI_CONNECT_TIMEOUT_MS) {
        state = NET_CONNECTING;
    } else {
        state = NET_OFFLINE;
        if (millis() - connectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
            WiFi.disconnect();
            WiFi.begin(g->wifiSsid.c_str(), g->wifiPass.c_str());
            connectStartMs = millis();
        }
    }
}

NetState net_state() { return state; }
bool net_is_provisioning() { return provisioning; }
String net_ap_ssid() { return apSsid; }

String device_id() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // factory MAC, valid before WiFi init
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(AP_SSID_PREFIX) + suffix;
}
