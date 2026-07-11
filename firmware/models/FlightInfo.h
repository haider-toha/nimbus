#pragma once

#include <Arduino.h>
#include "AirportInfo.h"

struct FlightInfo
{
    // Flight identifiers
    String ident;
    String ident_icao;
    String ident_iata;

    // Operator
    String operator_code;
    String operator_icao;
    String operator_iata;

    // Route
    AirportInfo origin;
    AirportInfo destination;

    // Aircraft
    String aircraft_code;
    double altitude_ft = NAN;
    bool on_ground = false;

    // Human-friendly display strings
    String airline_display_name_full;
    String aircraft_display_name_short;

    // enrichment (adsbdb)
    String registration;
    String aircraft_manufacturer;
    String aircraft_full_type;
    String registered_owner;
    String airline_callsign;
    String airline_country;

    // live (airplanes.live, copied from StateVector for message rendering)
    double ground_speed_kt = NAN;
    double vertical_rate_fpm = NAN;
    double track_deg = NAN;
    String squawk;
    String emergency;
    String category;
    double distance_km = NAN;
    double bearing_deg = NAN;
};
