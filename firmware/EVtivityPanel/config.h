#pragma once

// Firmware
#define FW_VERSION              "1.0.0"

// Provisioning access point (open) shown before configuration
#define AP_SSID_PREFIX          "EVtivity-Panel-"
#define AP_IP_OCTET             "192.168.4.1"
#define WEB_PORT                80
#define DNS_PORT                53

// Timing
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define DEFAULT_POLL_MS         5000
#define HTTP_GET_TIMEOUT_MS     8000
#define HTTP_CMD_TIMEOUT_MS     40000   // OCPP commands can take up to 35 s

// Limits
#define MAX_STATIONS            40

// NVS (Preferences) namespace and keys
#define NVS_NS                  "evtivity"
#define K_CONFIGURED            "configured"
#define K_WIFI_SSID             "wifi_ssid"
#define K_WIFI_PASS             "wifi_pass"
#define K_CSMS_URL              "csms_url"
#define K_API_KEY               "api_key"
#define K_SITE_ID               "site_id"
#define K_TLS_SKIP              "tls_skip"
#define K_DEV_NAME              "dev_name"
#define K_BRIGHTNESS            "brightness"
#define K_POLL_MS               "poll_ms"
#define K_SETUP_USER            "setup_user"
#define K_SETUP_PASS            "setup_pass"

// Setup portal login (HTTP Basic Auth on the provisioning AP)
#define DEFAULT_SETUP_USER      "admin"
#define DEFAULT_SETUP_PASS      "admin123"
