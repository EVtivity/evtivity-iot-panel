#pragma once
#include <Arduino.h>
#include "config.h"

// One charging station as the panel needs to display and act on it.
struct Station {
    String id;        // internal id (sta_...)
    String ocppId;    // OCPP id (CS-0001), shown and used by CSS/OCPP commands
    String model;
    String status;    // derived: charging|available|reserved|faulted|unavailable|unknown
    String connector; // e.g. "CCS2 - 150 kW"
    bool isOnline = false;
    bool isSimulator = false; // CSS simulator-backed station (plug in / unplug available)
};

// One physical connector (port) on a station, from GET /v1/stations/{id}/connectors.
#define MAX_CONNECTORS 8
struct Connector {
    int evseId = 0;
    int connectorId = 0;
    String type;      // CCS2, CHAdeMO, Type2, ...
    float powerKw = 0;
    String status;    // OCPP-derived connector status
};

// All connectors of one station, published by the network task to the UI.
struct ConnectorSet {
    Connector items[MAX_CONNECTORS];
    int count = 0;
    String stationId; // internal id these belong to
};

// A snapshot of the configured site, published by the network task and read by the UI.
struct SiteSnapshot {
    Station stations[MAX_STATIONS];
    int count = 0;
    int onlineCount = 0;
    bool valid = false;     // a successful poll has populated this
    String error;           // last error, shown as a banner when non-empty
    uint32_t updatedAtMs = 0;
};

// Aggregate site figures for the Dashboard tab, from /v1/dashboard/{stats,financial-stats}.
// Site-scoped to the API key's user. Needs the dashboard:read permission.
struct DashboardStats {
    bool valid = false;         // a successful fetch populated this
    bool forbidden = false;     // 403: the API key lacks dashboard:read
    double todayEnergyWh = 0;   // energy delivered today (last 24h)
    long todaySessions = 0;     // sessions started today (last 24h)
    long todayRevenueCents = 0; // revenue today (server-side calendar day)
    long todayProfitCents = 0;  // revenue minus electricity cost today
};

// Live detail of the active charging session on the selected station.
#define MAX_POWER_SAMPLES 30
struct SessionDetail {
    bool active = false;            // a charging session is in progress
    String stationId;              // internal station id this detail belongs to
    float energyKwh = 0;
    long costCents = 0;
    uint32_t startedAtEpoch = 0;   // unix seconds (UTC), 0 if unknown
    float powerKw = 0;             // latest reading
    int sampleCount = 0;           // power samples in chronological order
    float samples[MAX_POWER_SAMPLES];
    uint32_t sampleEpoch[MAX_POWER_SAMPLES]; // unix time of each sample (for the chart X axis)
};

// A user-initiated action queued by the UI and run by the network task.
enum CommandType { CMD_NONE, CMD_START, CMD_STOP, CMD_CLEAR, CMD_SIMULATE,
                   CMD_PLUGIN, CMD_UNPLUG, CMD_REFRESH };

// POD so it copies safely through a FreeRTOS queue (no String members).
struct Command {
    CommandType type = CMD_NONE;
    char ocppId[24] = {0};
    int evseId = 1;
    char sim[24] = {0};   // CSS action name for CMD_SIMULATE (e.g. "injectFault")
};
