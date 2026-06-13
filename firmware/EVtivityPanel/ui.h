#pragma once
#include "model.h"
#include "settings_store.h"
#include "net.h"   // NetState

// Build the UI on the active LVGL screen. Call once, inside lvgl_port_lock().
void ui_init(const Settings &s);

// Publish a new site snapshot. The caller MUST hold lvgl_port_lock().
void ui_set_snapshot(const SiteSnapshot &snap);

// Publish the network state (and the AP SSID while provisioning). Switches between
// the provisioning screen and the main UI. The caller MUST hold lvgl_port_lock().
void ui_set_status(NetState st, const String &apSsid);

// Publish the CSMS version shown in Settings. The caller MUST hold lvgl_port_lock().
void ui_set_csms_version(const String &version);

// Publish the API-key permission check result. `missing` is a comma-separated list
// of absent permissions (empty when all present); `err` is a transport error.
// Shows a warning banner when something is wrong. Caller MUST hold lvgl_port_lock().
void ui_set_perm_status(const String &missing, const String &err);

// Internal id of the selected station when it is charging, else "". The network
// task reads this to decide whether to poll live session detail. Caller MUST hold
// lvgl_port_lock().
String ui_selected_station_id();

// Internal id of the currently selected station regardless of status (else ""),
// used to poll its connectors. Caller MUST hold lvgl_port_lock().
String ui_selected_station_internal_id();

// Publish the selected station's connectors (ports). Caller MUST hold lvgl_port_lock().
void ui_set_connectors(const ConnectorSet &set);

// Apply at most one coalesced detail rebuild after a poll's fetches. Call once at
// the end of each poll. Caller MUST hold lvgl_port_lock().
void ui_commit_detail();

// Update the open action popup with a command result. Caller MUST hold lvgl_port_lock().
void ui_set_command_result(const String &action, bool ok, const String &msg);

// Publish live session detail for the selected station. Caller MUST hold lvgl_port_lock().
void ui_set_session(const SessionDetail &sd);

// Publish the site address shown in Settings. Caller MUST hold lvgl_port_lock().
void ui_set_site_address(const String &addr);

// Publish aggregate site figures for the Dashboard tab. Caller MUST hold lvgl_port_lock().
void ui_set_dashboard(const DashboardStats &ds);

// True when the Dashboard tab is showing; the network task polls dashboard stats only then.
// Caller MUST hold lvgl_port_lock().
bool ui_on_dashboard();

// Register the callback the UI calls when the operator taps an action.
void ui_set_command_cb(void (*cb)(const Command &));
