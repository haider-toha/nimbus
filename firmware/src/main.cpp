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

void setup()
{
    Serial.begin(115200);
    delay(200);

    g_display.initialize();
    g_display.displayMessage(String("Nimbus"));

    if (strlen(WiFiConfiguration::WIFI_SSID) > 0)
    {
        WiFi.mode(WIFI_STA);
        g_display.displayMessage(String("WiFi: ") + WiFiConfiguration::WIFI_SSID);
        WiFi.begin(WiFiConfiguration::WIFI_SSID, WiFiConfiguration::WIFI_PASSWORD);
        Serial.print("Connecting to WiFi");
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50)
        {
            delay(200);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.print("WiFi connected: ");
            Serial.println(WiFi.localIP());
            g_display.displayMessage(String("WiFi OK ") + WiFi.localIP().toString());
            delay(3000);
            g_display.showLoading();
        }
        else
        {
            Serial.println("WiFi not connected; proceeding without network");
            g_display.displayMessage(String("WiFi FAIL"));
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