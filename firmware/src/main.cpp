#include <vector>
#include <WiFi.h>
#include "config/UserConfiguration.h"
#include "config/WiFiConfiguration.h"
#include "config/TimingConfiguration.h"
#include "adapters/AirplanesLiveFetcher.h"
#include "adapters/AdsbdbFetcher.h"
#include "adapters/Hub75Display.h"
#include "core/FlightDataFetcher.h"

static AirplanesLiveFetcher g_airplanesLive;
static AdsbdbFetcher g_adsbdb;
static FlightDataFetcher g_fetcher(&g_airplanesLive, &g_adsbdb);
static Hub75Display g_display;

static std::vector<FlightInfo> g_flights;
static unsigned long g_lastFetchMs = 0;
static unsigned long g_lastDisplayMs = 0;
static bool g_hasFetched = false;

static bool ensureWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }
    if (strlen(WiFiConfiguration::WIFI_SSID) == 0)
    {
        return false;
    }

    Serial.printf("WiFi connecting to \"%s\"...\n", WiFiConfiguration::WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WiFiConfiguration::WIFI_SSID, WiFiConfiguration::WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 50)
    {
        delay(200);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.printf("WiFi failed (status %d)\n", WiFi.status());
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(200);

    if (!g_display.initialize())
    {
        Serial.println("Display initialization failed; firmware halted");
        while (true)
        {
            delay(1000);
        }
    }
    g_display.displayMessage(String("Nimbus"));

    if (strlen(WiFiConfiguration::WIFI_SSID) > 0)
    {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        g_display.displayMessage(String("WiFi: ") + WiFiConfiguration::WIFI_SSID);

        if (ensureWiFi())
        {
            g_display.displayMessage(String("WiFi OK ") + WiFi.localIP().toString());
            delay(3000);
            g_display.showLoading();
        }
        else
        {
            g_display.displayMessage(String("WiFi FAIL"));
            delay(2000);
            g_display.showLoading();
        }
    }
}

void loop()
{
    const unsigned long fetchIntervalMs =
        TimingConfiguration::FETCH_INTERVAL_SECONDS * 1000UL;
    const unsigned long displayIntervalMs =
        TimingConfiguration::DISPLAY_CYCLE_SECONDS * 1000UL;
    const unsigned long now = millis();
    if (!g_hasFetched || now - g_lastFetchMs >= fetchIntervalMs)
    {
        g_hasFetched = true;
        g_lastFetchMs = now;

        if (!ensureWiFi())
        {
            Serial.println("WiFi not available, skipping fetch");
            return;
        }

        std::vector<StateVector> states;
        size_t enriched = g_fetcher.fetchFlights(states, g_flights);

        Serial.print("airplanes.live state vectors: ");
        Serial.println((int)states.size());
        Serial.print("adsbdb enriched flights: ");
        Serial.println((int)enriched);

        for (const auto &s : states)
        {
            Serial.print(" ");
            Serial.print(s.callsign);
            Serial.print(" @ ");
            Serial.print(s.distance_km, 1);
            Serial.print("km bearing ");
            Serial.println(s.bearing_deg, 1);
        }

        for (const auto &f : g_flights)
        {
            Serial.println("=== FLIGHT INFO ===");
            Serial.print("Ident: ");
            Serial.println(f.ident);
            Serial.print("Ident ICAO: ");
            Serial.println(f.ident_icao);
            Serial.print("Ident IATA: ");
            Serial.println(f.ident_iata);
            Serial.print("Airline: ");
            Serial.println(f.airline_display_name_full);
            Serial.print("Aircraft: ");
            Serial.println(f.aircraft_display_name_short.length() ? f.aircraft_display_name_short : f.aircraft_code);
            Serial.print("Operator Code: ");
            Serial.println(f.operator_code);
            Serial.print("Operator ICAO: ");
            Serial.println(f.operator_icao);
            Serial.print("Operator IATA: ");
            Serial.println(f.operator_iata);

            Serial.println("--- Origin ---");
            Serial.print("Code IATA: ");
            Serial.println(f.origin.code_iata);
            Serial.print("Code ICAO: ");
            Serial.println(f.origin.code_icao);
            Serial.print("Name: ");
            Serial.println(f.origin.name);

            Serial.println("--- Destination ---");
            Serial.print("Code IATA: ");
            Serial.println(f.destination.code_iata);
            Serial.print("Code ICAO: ");
            Serial.println(f.destination.code_icao);
            Serial.print("Name: ");
            Serial.println(f.destination.name);
            Serial.println("===================");
        }

        g_display.displayFlights(g_flights);
        g_lastDisplayMs = now;
    }
    else if (!g_flights.empty() &&
             now - g_lastDisplayMs >= displayIntervalMs)
    {
        g_display.displayFlights(g_flights);
        g_lastDisplayMs = now;
    }
    delay(10);
}