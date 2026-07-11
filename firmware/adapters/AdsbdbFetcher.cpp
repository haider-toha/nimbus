#include "adapters/AdsbdbFetcher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config/APIConfiguration.h"

namespace
{
enum class RequestResult
{
    Success,
    NotFound,
    Failed
};

String pathEncode(const String &value)
{
    String encoded;
    const char *hex = "0123456789ABCDEF";
    for (size_t index = 0; index < value.length(); ++index)
    {
        const unsigned char character = value[index];
        if ((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_' ||
            character == '.' ||
            character == '~')
        {
            encoded += static_cast<char>(character);
        }
        else
        {
            encoded += '%';
            encoded += hex[(character >> 4) & 0x0F];
            encoded += hex[character & 0x0F];
        }
    }
    return encoded;
}

String getString(const JsonObjectConst &object, const char *key)
{
    const char *value = object[key] | static_cast<const char *>(nullptr);
    return value == nullptr ? String() : String(value);
}

double getDouble(const JsonObjectConst &object, const char *key)
{
    return object[key].is<double>() ? object[key].as<double>() : NAN;
}

RequestResult getJson(const String &url, JsonDocument &document)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(30);

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Accept", "application/json");
    http.setTimeout(15000);

    const int statusCode = http.GET();
    if (statusCode == HTTP_CODE_NOT_FOUND)
    {
        http.end();
        return RequestResult::NotFound;
    }
    if (statusCode != HTTP_CODE_OK)
    {
        Serial.printf("AdsbdbFetcher: HTTP request failed with code %d\n", statusCode);
        http.end();
        return RequestResult::Failed;
    }

    const String body = http.getString();
    http.end();

    // [DIAG] temporary on-device instrumentation (Phase 1). Remove after diagnosis.
    Serial.printf("AdsbdbFetcher: DIAG status=%d len=%u url=%s\n",
                  statusCode, (unsigned)body.length(), url.c_str());
    Serial.printf("AdsbdbFetcher: DIAG body=%.160s\n", body.c_str());

    const DeserializationError error = deserializeJson(document, body);
    if (error)
    {
        Serial.printf("AdsbdbFetcher: JSON parsing failed: %s\n", error.c_str());
        return RequestResult::Failed;
    }

    return RequestResult::Success;
}

bool fetchRoute(const String &callsign, FlightInfo &flight)
{
    if (callsign.isEmpty())
    {
        return false;
    }

    JsonDocument document;
    const String url = String(APIConfiguration::ADSBDB_BASE_URL) +
                       "/callsign/" + pathEncode(callsign);
    if (getJson(url, document) != RequestResult::Success)
    {
        return false;
    }

    const JsonObjectConst route = document["response"]["flightroute"].as<JsonObjectConst>();
    // [DIAG] temporary on-device instrumentation (Phase 1). Remove after diagnosis.
    Serial.printf("AdsbdbFetcher: DIAG route cs=%s routeNull=%d airlineNull=%d rc='%s'\n",
                  callsign.c_str(), (int)route.isNull(),
                  (int)route["airline"].isNull(),
                  (route["callsign"] | ""));
    if (route.isNull())
    {
        return false;
    }

    flight.ident = getString(route, "callsign");
    flight.ident_icao = getString(route, "callsign_icao");
    flight.ident_iata = getString(route, "callsign_iata");

    const JsonObjectConst airline = route["airline"].as<JsonObjectConst>();
    if (!airline.isNull())
    {
        flight.operator_code = getString(airline, "name");
        flight.operator_icao = getString(airline, "icao");
        flight.operator_iata = getString(airline, "iata");
        flight.airline_display_name_full = flight.operator_code;
        flight.airline_callsign = getString(airline, "callsign");
        flight.airline_country = getString(airline, "country");
    }

    const JsonObjectConst origin = route["origin"].as<JsonObjectConst>();
    if (!origin.isNull())
    {
        flight.origin.code_icao = getString(origin, "icao_code");
        flight.origin.code_iata = getString(origin, "iata_code");
        flight.origin.name = getString(origin, "name");
        flight.origin.municipality = getString(origin, "municipality");
        flight.origin.country = getString(origin, "country_name");
        flight.origin.latitude = getDouble(origin, "latitude");
        flight.origin.longitude = getDouble(origin, "longitude");
    }

    const JsonObjectConst destination = route["destination"].as<JsonObjectConst>();
    if (!destination.isNull())
    {
        flight.destination.code_icao = getString(destination, "icao_code");
        flight.destination.code_iata = getString(destination, "iata_code");
        flight.destination.name = getString(destination, "name");
        flight.destination.municipality = getString(destination, "municipality");
        flight.destination.country = getString(destination, "country_name");
        flight.destination.latitude = getDouble(destination, "latitude");
        flight.destination.longitude = getDouble(destination, "longitude");
    }

    return true;
}

bool fetchAircraft(const String &icao24Hex, FlightInfo &flight)
{
    if (icao24Hex.isEmpty())
    {
        return false;
    }

    JsonDocument document;
    const String url = String(APIConfiguration::ADSBDB_BASE_URL) +
                       "/aircraft/" + pathEncode(icao24Hex);
    if (getJson(url, document) != RequestResult::Success)
    {
        return false;
    }

    const JsonObjectConst aircraft = document["response"]["aircraft"].as<JsonObjectConst>();
    if (aircraft.isNull())
    {
        return false;
    }

    const String aircraftCode = getString(aircraft, "icao_type");
    if (!aircraftCode.isEmpty())
    {
        flight.aircraft_code = aircraftCode;
    }
    flight.aircraft_display_name_short = getString(aircraft, "type");
    if (flight.operator_icao.isEmpty())
    {
        flight.operator_icao = getString(
            aircraft,
            "registered_owner_operator_flag_code");
    }
    flight.registration = getString(aircraft, "registration");
    flight.aircraft_manufacturer = getString(aircraft, "manufacturer");
    flight.registered_owner = getString(aircraft, "registered_owner");

    return true;
}
}

bool AdsbdbFetcher::fetchFlightInfo(
    const String &flightIdent,
    const String &icao24Hex,
    FlightInfo &outInfo)
{
    String callsign = flightIdent;
    callsign.trim();

    String hex = icao24Hex;
    hex.trim();

    const bool routeFound = fetchRoute(callsign, outInfo);
    const bool aircraftFound = fetchAircraft(hex, outInfo);
    return routeFound || aircraftFound;
}
