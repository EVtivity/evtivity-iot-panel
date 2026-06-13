#include "settings_store.h"
#include <Preferences.h>
#include "config.h"
#include "net.h"   // device_id()

void settings_load(Settings &s) {
    Preferences p;
    p.begin(NVS_NS, true);
    s.configured = p.getUChar(K_CONFIGURED, 0) == 1;
    s.wifiSsid = p.getString(K_WIFI_SSID, "");
    s.wifiPass = p.getString(K_WIFI_PASS, "");
    s.csmsUrl = p.getString(K_CSMS_URL, "");
    s.apiKey = p.getString(K_API_KEY, "");
    s.siteId = p.getString(K_SITE_ID, "");
    s.tlsSkip = p.getUChar(K_TLS_SKIP, 0) == 1;
    s.devName = p.getString(K_DEV_NAME, device_id());
    s.brightness = p.getUChar(K_BRIGHTNESS, 80);
    s.pollMs = p.getULong(K_POLL_MS, DEFAULT_POLL_MS);
    s.setupUser = p.getString(K_SETUP_USER, DEFAULT_SETUP_USER);
    s.setupPass = p.getString(K_SETUP_PASS, DEFAULT_SETUP_PASS);
    p.end();
}

void settings_save(const Settings &s) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString(K_WIFI_SSID, s.wifiSsid);
    p.putString(K_WIFI_PASS, s.wifiPass);
    p.putString(K_CSMS_URL, s.csmsUrl);
    p.putString(K_API_KEY, s.apiKey);
    p.putString(K_SITE_ID, s.siteId);
    p.putUChar(K_TLS_SKIP, s.tlsSkip ? 1 : 0);
    p.putString(K_DEV_NAME, s.devName);
    p.putUChar(K_BRIGHTNESS, s.brightness);
    p.putULong(K_POLL_MS, s.pollMs);
    p.putString(K_SETUP_USER, s.setupUser);
    p.putString(K_SETUP_PASS, s.setupPass);
    p.putUChar(K_CONFIGURED, 1);
    p.end();
}

void settings_clear() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.clear();
    p.end();
}
