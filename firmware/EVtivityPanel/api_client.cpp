#include "api_client.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static Settings g;

// One persistent client per transport, reused across requests. They MUST outlive
// every HTTPClient: ~HTTPClient calls _client->stop(), so a per-request client that
// is freed before the HTTPClient destructs is a use-after-free (heap corruption).
static WiFiClient s_plain;
static WiFiClientSecure s_secure;

void api_begin(const Settings &s) {
    g = s;
    // Tolerate a base URL with no scheme (e.g. "host:7102"). The HTTP client needs
    // "http(s)://" or every request fails with a URL error.
    if (g.csmsUrl.length() && !g.csmsUrl.startsWith("http://") && !g.csmsUrl.startsWith("https://"))
        g.csmsUrl = "http://" + g.csmsUrl;
}

// Open an HTTPClient to base+path, set the Bearer header. Returns false on bad config.
static bool begin_req(HTTPClient &http, const String &path, uint32_t timeoutMs) {
    String url = g.csmsUrl + path;
    bool ok;
    if (url.startsWith("https://")) {
        if (g.tlsSkip) s_secure.setInsecure();
        ok = http.begin(s_secure, url);
    } else {
        ok = http.begin(s_plain, url);
    }
    if (!ok) {
        Serial.printf("[api] begin failed for '%s'\n", url.c_str());
        return false;
    }
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    http.addHeader("Authorization", "Bearer " + g.apiKey);
    http.addHeader("Content-Type", "application/json");
    return true;
}

static void cleanup(HTTPClient &http) { http.end(); }

bool api_fetch_site(SiteSnapshot &out) {
    out.valid = false;
    out.error = "";
    if (g.csmsUrl.length() == 0 || g.siteId.length() == 0) { out.error = "Not configured"; return false; }

    HTTPClient http;
    // Use /v1/stations (not /v1/sites/{id}/stations) so the derived station status
    // matches the CSMS portal exactly (shared buildDerivedStatusSubquery), and this
    // list includes isSimulator and connectorTypes.
    String path = "/v1/stations?siteId=" + g.siteId + "&page=1&limit=" + String(MAX_STATIONS);
    if (!begin_req(http, path, HTTP_GET_TIMEOUT_MS)) { out.error = "URL error"; cleanup(http); return false; }

    int code = http.GET();
    if (code != 200) {
        out.error = (code == 401) ? "Unauthorized (check API key)"
                  : (code == 404) ? "Site not found / no access"
                  : "HTTP " + String(code);
        cleanup(http);
        return false;
    }

    // Deserialize only the fields we use to keep RAM small.
    JsonDocument filter;
    filter["data"][0]["id"] = true;
    filter["data"][0]["stationId"] = true;
    filter["data"][0]["model"] = true;
    filter["data"][0]["status"] = true;
    filter["data"][0]["isOnline"] = true;
    filter["data"][0]["isSimulator"] = true;
    filter["data"][0]["connectorTypes"] = true;

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    cleanup(http);
    if (e) { out.error = String("JSON: ") + e.c_str(); return false; }

    out.count = 0;
    out.onlineCount = 0;
    for (JsonObject st : doc["data"].as<JsonArray>()) {
        if (out.count >= MAX_STATIONS) break;
        Station &s = out.stations[out.count];
        s.id = st["id"] | "";
        s.ocppId = st["stationId"] | "";
        s.model = st["model"] | "";
        s.status = st["status"] | "unknown";
        s.isOnline = st["isOnline"] | false;
        s.isSimulator = st["isSimulator"] | false;
        const char *ctype = st["connectorTypes"][0] | "";
        s.connector = String(ctype);
        if (s.isOnline) out.onlineCount++;
        out.count++;
    }
    out.valid = true;
    out.updatedAtMs = millis();
    return true;
}

bool api_get_system_version(String &outVersion, String &err) {
    outVersion = "";
    if (g.csmsUrl.length() == 0) { err = "Not configured"; return false; }

    HTTPClient http;
    if (!begin_req(http, "/v1/system/info", HTTP_GET_TIMEOUT_MS)) { err = "URL error"; cleanup(http); return false; }

    int code = http.GET();
    if (code != 200) {
        err = (code == 401) ? "Unauthorized"
            : (code == 403) ? "Needs settings.system:read"
            : "HTTP " + String(code);
        cleanup(http);
        return false;
    }

    JsonDocument filter;
    filter["version"] = true;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    cleanup(http);
    if (e) { err = String("JSON: ") + e.c_str(); return false; }
    outVersion = doc["version"] | "";
    return outVersion.length() > 0;
}

bool api_check_permissions(String &missing, String &err) {
    missing = ""; err = "";
    static const char *NEEDED[] = {"sites:read", "stations:write", "settings.system:read", "users:read"};
    if (g.csmsUrl.length() == 0 || g.apiKey.length() == 0) { err = "Not configured"; return false; }

    HTTPClient http;
    if (!begin_req(http, "/v1/users/me/permissions", HTTP_GET_TIMEOUT_MS)) { err = "URL error"; cleanup(http); return false; }

    int code = http.GET();
    if (code == 403) { cleanup(http); missing = "users:read"; return false; }
    if (code == 401) { cleanup(http); err = "Unauthorized (check API key)"; return false; }
    if (code != 200) { cleanup(http); err = "HTTP " + String(code); return false; }

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream());
    cleanup(http);
    if (e) { err = String("JSON: ") + e.c_str(); return false; }

    for (const char *need : NEEDED) {
        bool found = false;
        for (JsonVariant v : doc.as<JsonArray>()) {
            const char *p = v.as<const char *>();
            if (p && strcmp(p, need) == 0) { found = true; break; }
        }
        if (!found) { if (missing.length()) missing += ", "; missing += need; }
    }
    return missing.length() == 0;
}

// Parse an ISO-8601 UTC timestamp ("2026-06-12T14:30:00.000Z") to a unix epoch
// (days_from_civil, Howard Hinnant). Returns 0 on parse failure.
static uint32_t iso_to_epoch(const char *iso) {
    int Y, Mo, D, H, Mi, S;
    if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) != 6) return 0;
    int y = Y - (Mo <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (unsigned)(Mo + (Mo > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + (unsigned)D - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400L + H * 3600 + Mi * 60 + S);
}

bool api_get_site_info(String &addressOut, String &err) {
    addressOut = ""; err = "";
    if (g.csmsUrl.length() == 0 || g.siteId.length() == 0) { err = "Not configured"; return false; }

    HTTPClient http;
    if (!begin_req(http, "/v1/sites/" + g.siteId, HTTP_GET_TIMEOUT_MS)) { err = "URL error"; cleanup(http); return false; }
    int code = http.GET();
    if (code != 200) { err = "HTTP " + String(code); cleanup(http); return false; }

    JsonDocument filter;
    filter["name"] = true;
    for (const char *k : {"address", "city", "state", "postalCode", "country"}) filter[k] = true;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    cleanup(http);
    if (e) { err = String("JSON: ") + e.c_str(); return false; }

    // Site name first, then the postal address.
    const char *nm = doc["name"] | "";
    if (nm[0]) addressOut = nm;
    for (const char *k : {"address", "city", "state", "postalCode", "country"}) {
        const char *v = doc[k] | "";
        if (v[0]) { if (addressOut.length()) addressOut += ", "; addressOut += v; }
    }
    return true;
}

bool api_fetch_session(const String &stationInternalId, SessionDetail &out, String &err) {
    out = SessionDetail();
    out.stationId = stationInternalId;
    err = "";
    if (g.csmsUrl.length() == 0) { err = "Not configured"; return false; }

    HTTPClient http;
    String path = "/v1/sessions?stationId=" + stationInternalId + "&status=active&limit=1";
    if (!begin_req(http, path, HTTP_GET_TIMEOUT_MS)) { err = "URL error"; cleanup(http); return false; }
    int code = http.GET();
    if (code != 200) { err = "HTTP " + String(code); cleanup(http); return false; }

    JsonDocument fil;
    fil["data"][0]["id"] = true;
    fil["data"][0]["startedAt"] = true;
    fil["data"][0]["energyDeliveredWh"] = true;
    fil["data"][0]["currentCostCents"] = true;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(fil));
    cleanup(http);
    if (e) { err = String("JSON: ") + e.c_str(); return false; }

    JsonArray arr = doc["data"].as<JsonArray>();
    if (arr.size() == 0) return true; // no active session: out.active stays false
    JsonObject s = arr[0];
    String sessionId = s["id"] | "";
    out.active = true;
    out.energyKwh = (float)((double)(s["energyDeliveredWh"] | 0.0) / 1000.0);
    out.costCents = s["currentCostCents"] | 0L;
    out.startedAtEpoch = iso_to_epoch(s["startedAt"] | "");

    if (sessionId.length() == 0) return true;
    HTTPClient h2;
    String mpath = "/v1/sessions/" + sessionId + "/meter-values?measurand=Power.Active.Import&limit=" + String(MAX_POWER_SAMPLES);
    if (!begin_req(h2, mpath, HTTP_GET_TIMEOUT_MS)) { cleanup(h2); return true; }
    if (h2.GET() == 200) {
        JsonDocument mf;
        mf["data"][0]["value"] = true;
        mf["data"][0]["unit"] = true;
        mf["data"][0]["timestamp"] = true;
        JsonDocument md;
        if (!deserializeJson(md, h2.getStream(), DeserializationOption::Filter(mf))) {
            JsonArray ma = md["data"].as<JsonArray>();
            int n = ma.size();
            if (n > MAX_POWER_SAMPLES) n = MAX_POWER_SAMPLES;
            int idx = 0;
            for (int i = n - 1; i >= 0; i--) { // response is newest-first; store oldest-first
                JsonVariant vv = ma[i]["value"];
                float v = vv.is<const char *>() ? atof(vv.as<const char *>()) : vv.as<float>();
                const char *u = ma[i]["unit"] | "";
                out.samples[idx] = (strcmp(u, "W") == 0) ? v / 1000.0f : v;
                out.sampleEpoch[idx] = iso_to_epoch(ma[i]["timestamp"] | "");
                idx++;
            }
            out.sampleCount = idx;
            if (idx > 0) out.powerKw = out.samples[idx - 1];
        }
    }
    cleanup(h2);
    return true;
}

bool api_fetch_connectors(const String &stationInternalId, ConnectorSet &out, String &err) {
    out = ConnectorSet();
    out.stationId = stationInternalId;
    err = "";
    if (g.csmsUrl.length() == 0) { err = "Not configured"; return false; }

    HTTPClient http;
    String path = "/v1/stations/" + stationInternalId + "/connectors";
    if (!begin_req(http, path, HTTP_GET_TIMEOUT_MS)) { err = "URL error"; cleanup(http); return false; }
    int code = http.GET();
    if (code != 200) { err = "HTTP " + String(code); cleanup(http); return false; }

    // This endpoint returns a bare top-level array of EVSEs, not a {data:[...]} envelope.
    JsonDocument filter;
    filter[0]["evseId"] = true;
    filter[0]["connectors"][0]["connectorId"] = true;
    filter[0]["connectors"][0]["connectorType"] = true;
    filter[0]["connectors"][0]["maxPowerKw"] = true;
    filter[0]["connectors"][0]["status"] = true;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    cleanup(http);
    if (e) { err = String("JSON: ") + e.c_str(); return false; }

    for (JsonObject evse : doc.as<JsonArray>()) {
        int evseId = evse["evseId"] | 0;
        for (JsonObject c : evse["connectors"].as<JsonArray>()) {
            if (out.count >= MAX_CONNECTORS) break;
            Connector &k = out.items[out.count];
            k.evseId = evseId;
            k.connectorId = c["connectorId"] | 0;
            k.type = (const char *)(c["connectorType"] | "");
            JsonVariant pv = c["maxPowerKw"]; // schema stores power as a string
            k.powerKw = pv.is<const char *>() ? atof(pv.as<const char *>()) : (pv | 0.0f);
            k.status = (const char *)(c["status"] | "");
            out.count++;
        }
    }
    return true;
}

static bool post(const String &path, const String &body, uint32_t timeoutMs, String &err) {
    HTTPClient http;
    if (!begin_req(http, path, timeoutMs)) { err = "URL error"; cleanup(http); return false; }
    int code = http.POST(body);
    bool ok = (code >= 200 && code < 300);
    if (!ok) {
        err = "HTTP " + String(code);
        // Surface the API's error message ({error|message}) when present.
        String resp = http.getString();
        if (resp.length()) {
            JsonDocument d;
            if (!deserializeJson(d, resp)) {
                const char *m = d["error"] | "";
                if (!m[0]) m = d["message"] | "";
                if (m[0]) err = String(m);
            }
        }
    }
    cleanup(http);
    return ok;
}

bool api_css_action(const String &ocppId, int evseId, const String &action, String &err) {
    // The CSS action bodies are strict (additionalProperties:false). Send ONLY the
    // fields each action accepts, or the request is rejected with HTTP 400.
    String body = "{\"stationId\":\"" + ocppId + "\",\"evseId\":" + String(evseId);
    if (action == "startCharging" || action == "authorize")
        body += ",\"idToken\":\"SIM-TOKEN\",\"tokenType\":\"ISO14443\"";
    else if (action == "stopCharging")
        body += ",\"reason\":\"Remote\"";
    else if (action == "injectFault")
        body += ",\"errorCode\":\"OtherError\"";
    body += "}";
    return post("/v1/css/actions/" + action, body, HTTP_CMD_TIMEOUT_MS, err);
}

bool api_start_charging(const String &ocppId, int evseId, String &err) {
    if (g.csmsUrl.length() == 0) { err = "Not configured"; return false; }
    // Mirror the guest charging portal: POST /v1/portal/guest/start/{stationId}/{evseId}.
    // For a free tariff the CSMS creates a guest_sessions row (payment_authorized) and
    // sends RequestStartTransaction with a unique session token, so the payment gate
    // passes and the session actually charges -- no anonymous-session stop and no
    // ConcurrentTx. A paid tariff returns PAYMENT_METHOD_REQUIRED.
    String path = "/v1/portal/guest/start/" + ocppId + "/" + String(evseId);
    return post(path, "{}", HTTP_CMD_TIMEOUT_MS, err);
}

bool api_ocpp_command(const String &version, const String &command, const String &jsonBody, String &err) {
    return post("/v1/ocpp/commands/" + version + "/" + command, jsonBody, HTTP_CMD_TIMEOUT_MS, err);
}

// Aggregate site figures for the Dashboard. Both endpoints are site-scoped to the API
// key's user and require dashboard:read; a 403 sets out.forbidden so the UI can hide them.
bool api_fetch_dashboard(DashboardStats &out) {
    out = DashboardStats();
    if (g.csmsUrl.length() == 0) return false;

    // Energy today: sum the day rows from the trailing-24h history.
    {
        HTTPClient http;
        if (!begin_req(http, "/v1/dashboard/energy-history?days=1", HTTP_GET_TIMEOUT_MS)) { cleanup(http); return false; }
        int code = http.GET();
        if (code == 403) { out.forbidden = true; cleanup(http); return false; }
        if (code != 200) { cleanup(http); return false; }
        JsonDocument filter; filter[0]["energyWh"] = true;
        JsonDocument doc;
        DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        cleanup(http);
        if (e) return false;
        double sum = 0;
        for (JsonObject r : doc.as<JsonArray>()) sum += (double)(r["energyWh"] | 0.0);
        out.todayEnergyWh = sum;
    }

    // Sessions today.
    {
        HTTPClient http;
        if (!begin_req(http, "/v1/dashboard/session-history?days=1", HTTP_GET_TIMEOUT_MS)) { cleanup(http); return false; }
        int code = http.GET();
        if (code == 403) { out.forbidden = true; cleanup(http); return false; }
        if (code != 200) { cleanup(http); return false; }
        JsonDocument filter; filter[0]["count"] = true;
        JsonDocument doc;
        DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        cleanup(http);
        if (e) return false;
        long sum = 0;
        for (JsonObject r : doc.as<JsonArray>()) sum += (long)(r["count"] | 0L);
        out.todaySessions = sum;
    }

    // Revenue + profit today (server-side calendar day).
    {
        HTTPClient http;
        if (!begin_req(http, "/v1/dashboard/financial-stats", HTTP_GET_TIMEOUT_MS)) { cleanup(http); return false; }
        int code = http.GET();
        if (code == 403) { out.forbidden = true; cleanup(http); return false; }
        if (code != 200) { cleanup(http); return false; }
        JsonDocument filter;
        filter["todayRevenueCents"] = true;
        filter["todayProfitCents"] = true;
        JsonDocument doc;
        DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        cleanup(http);
        if (e) return false;
        out.todayRevenueCents = doc["todayRevenueCents"] | 0L;
        out.todayProfitCents = doc["todayProfitCents"] | 0L;
    }

    out.valid = true;
    return true;
}
