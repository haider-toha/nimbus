#pragma once

#include <Arduino.h>

namespace UserConfiguration
{
    // Location configuration
    static const double CENTER_LAT = 51.49902401104305;
    static const double CENTER_LON = -0.09823109814490183;
    static const double RADIUS_KM = 100.0;
    static const uint8_t MAX_TRACKED_FLIGHTS = 5;

    // Resolve the three-letter airline prefix in each ADS-B callsign and
    // display only aircraft whose mapped IATA code has a bundled logo.
    static const bool REQUIRE_AIRLINE_LOGO = true;

    // Display customization
    // Brightness controls overall display brightness (0-255)
    static const uint8_t DISPLAY_BRIGHTNESS = 96;

    // RGB color for all text rendering on the LED matrix
    static const uint8_t TEXT_COLOR_R = 255;
    static const uint8_t TEXT_COLOR_G = 255;
    static const uint8_t TEXT_COLOR_B = 255;
}
