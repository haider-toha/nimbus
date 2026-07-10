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
};
