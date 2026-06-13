#pragma once
#include "settings_store.h"

enum NetState { NET_PROVISION, NET_CONNECTING, NET_ONLINE, NET_OFFLINE };

// Starts AP + captive portal when unconfigured, otherwise joins WiFi as a station.
void net_begin(Settings &s);
// Call frequently from loop(): services the DNS and HTTP servers while provisioning.
void net_loop();
NetState net_state();
bool net_is_provisioning();
String net_ap_ssid();

// Stable, unique device ID derived from the WiFi MAC: "EVtivity-Panel-XXXX".
// Doubles as the provisioning AP SSID and the default device name. Safe to call
// before net_begin (reads the efuse MAC, no WiFi init required).
String device_id();
