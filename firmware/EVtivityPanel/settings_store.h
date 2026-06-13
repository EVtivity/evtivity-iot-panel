#pragma once
#include <Arduino.h>
#include "config.h"

struct Settings {
    bool configured = false;
    String wifiSsid;
    String wifiPass;
    String csmsUrl;     // no trailing slash
    String apiKey;      // 64 hex
    String siteId;
    bool tlsSkip = false;
    String devName = "EVtivity Panel";
    uint8_t brightness = 80;
    uint32_t pollMs = 5000;
    String setupUser = DEFAULT_SETUP_USER;  // setup-portal login
    String setupPass = DEFAULT_SETUP_PASS;
};

void settings_load(Settings &s);
void settings_save(const Settings &s);
void settings_clear();
