#pragma once

#include <Arduino.h>

struct StateVector
{
    String icao24;
    String callsign;
    String aircraft_code;
    double baro_altitude_ft = NAN;
    bool on_ground = false;
    double distance_km = NAN;
    double bearing_deg = NAN;

    // Live ADS-B fields (airplanes.live)
    double ground_speed_kt = NAN;
    double vertical_rate_fpm = NAN;
    double track_deg = NAN;
    String squawk;
    String emergency;
    String category;
    String registration;
    String aircraft_full_type; // airplanes.live "desc", e.g. "AIRBUS A-320neo"
};
