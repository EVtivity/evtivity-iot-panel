#pragma once
#include "settings_store.h"
#include "model.h"

void api_begin(const Settings &s);

// GET /v1/sites/{siteId}/stations -> fills out. Returns false and sets out.error on failure.
bool api_fetch_site(SiteSnapshot &out);

// GET /v1/system/info -> outVersion. The API key needs the settings.system:read scope.
bool api_get_system_version(String &outVersion, String &err);

// Verifies the API key holds every permission the panel needs (sites:read,
// stations:write, settings.system:read, users:read). Returns true when all are
// present. On false, `missing` lists the absent permissions (comma separated),
// or `err` carries a transport error when the check could not run.
bool api_check_permissions(String &missing, String &err);

// GET /v1/sites/{siteId} -> a single-line postal address. Needs sites:read.
bool api_get_site_info(String &addressOut, String &err);

// Live detail of the active charging session on a station (internal id).
// Fills out.active=false when the station has no active session (still returns true).
bool api_fetch_session(const String &stationInternalId, SessionDetail &out, String &err);

// GET /v1/stations/{id}/connectors -> every EVSE/connector on the station.
bool api_fetch_connectors(const String &stationInternalId, ConnectorSet &out, String &err);

// POST /v1/css/actions/{action} with {stationId, evseId}. Simulator-backed stations.
bool api_css_action(const String &ocppId, int evseId, const String &action, String &err);

// Start charging via the guest charging portal endpoint
// (POST /v1/portal/guest/start/{stationId}/{evseId}). For a free tariff the CSMS opens a
// guest session with a unique token, so the payment gate passes and the session charges --
// no anonymous-session stop, no ConcurrentTx. Paid tariffs return PAYMENT_METHOD_REQUIRED.
bool api_start_charging(const String &ocppId, int evseId, String &err);

// POST /v1/ocpp/commands/{version}/{command} with a raw JSON body. Real stations.
bool api_ocpp_command(const String &version, const String &command, const String &jsonBody, String &err);
