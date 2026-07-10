#include "adapters/AirplanesLiveFetcher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config/APIConfiguration.h"

namespace
{
constexpr double KM_PER_NAUTICAL_MILE = 1.852;
constexpr double MAX_RADIUS_NM = 250.0;
constexpr double MAX_POSITION_AGE_SECONDS = 30.0;
constexpr unsigned long MIN_REQUEST_INTERVAL_MS = 1000;
}

bool AirplanesLiveFetcher::fetchStateVectors(
    double centerLat,
    double centerLon,
    double radiusKm,
    std::vector<StateVector> &outStateVectors)
{
    outStateVectors.clear();
    if (radiusKm <= 0)
    {
        Serial.println("AirplanesLiveFetcher: Radius must be positive");
        return false;
    }

    if (_hasRequested)
    {
        const unsigned long elapsedMs = millis() - _lastRequestMs;
        if (elapsedMs < MIN_REQUEST_INTERVAL_MS)
        {
            delay(MIN_REQUEST_INTERVAL_MS - elapsedMs);
        }
    }
    _lastRequestMs = millis();
    _hasRequested = true;

    const double requestedRadiusNm = radiusKm / KM_PER_NAUTICAL_MILE;
    const double radiusNm = requestedRadiusNm > MAX_RADIUS_NM
                                ? MAX_RADIUS_NM
                                : requestedRadiusNm;
    const String url = String(APIConfiguration::AIRPLANES_LIVE_BASE_URL) +
                       "/point/" + String(centerLat, 6) +
                       "/" + String(centerLon, 6) +
                       "/" + String(radiusNm, 2);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Accept", "application/json");
    http.setTimeout(15000);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK)
    {
        Serial.printf("AirplanesLiveFetcher: HTTP request failed with code %d\n", statusCode);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["ac"][0]["hex"] = true;
    filter["ac"][0]["flight"] = true;
    filter["ac"][0]["lat"] = true;
    filter["ac"][0]["lon"] = true;
    filter["ac"][0]["alt_baro"] = true;
    filter["ac"][0]["t"] = true;
    filter["ac"][0]["seen_pos"] = true;
    filter["ac"][0]["dst"] = true;
    filter["ac"][0]["dir"] = true;

    JsonDocument doc;
    const DeserializationError error = deserializeJson(
        doc,
        http.getStream(),
        DeserializationOption::Filter(filter));
    http.end();

    if (error)
    {
        Serial.printf("AirplanesLiveFetcher: JSON parsing failed: %s\n", error.c_str());
        return false;
    }

    const JsonArray aircraft = doc["ac"].as<JsonArray>();
    if (aircraft.isNull())
    {
        return true;
    }

    for (JsonObject entry : aircraft)
    {
        if (entry["lat"].isNull() ||
            entry["lon"].isNull() ||
            entry["seen_pos"].isNull())
        {
            continue;
        }

        const double seenPositionSeconds = entry["seen_pos"].as<double>();
        if (seenPositionSeconds > MAX_POSITION_AGE_SECONDS)
        {
            continue;
        }

        const char *hexValue = entry["hex"] | "";
        const String icao24(hexValue);
        if (icao24.isEmpty() || icao24[0] == '~')
        {
            continue;
        }

        StateVector state;
        state.icao24 = icao24;
        state.callsign = String(entry["flight"] | "");
        state.callsign.trim();
        state.aircraft_code = String(entry["t"] | "");

        if (entry["alt_baro"].is<const char *>() &&
            String(entry["alt_baro"].as<const char *>()) == "ground")
        {
            state.on_ground = true;
            state.baro_altitude_ft = 0;
        }
        else if (!entry["alt_baro"].isNull())
        {
            state.baro_altitude_ft = entry["alt_baro"].as<double>();
        }

        if (!entry["dst"].isNull())
        {
            state.distance_km = entry["dst"].as<double>() * KM_PER_NAUTICAL_MILE;
        }
        if (!entry["dir"].isNull())
        {
            state.bearing_deg = entry["dir"].as<double>();
        }

        outStateVectors.push_back(state);
    }

    return true;
}
