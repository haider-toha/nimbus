#pragma once

#include <Arduino.h>

struct AirportInfo
{
    String code_icao;
    String code_iata;
    String name;
    String municipality;
    String country; // adsbdb country_name
    double latitude = NAN;
    double longitude = NAN;
};
