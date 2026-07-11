#pragma once

#include <Arduino.h>

namespace UserConfiguration
{
    // Location configuration
    static const double CENTER_LAT = 51.49902401104305;
    static const double CENTER_LON = -0.09823109814490183;
    static const double RADIUS_KM = 20.0;
    static const uint8_t MAX_TRACKED_FLIGHTS = 5;

    // When true, only large/heavy jets (DO-260B categories A3-A6) are
    // tracked. Light aircraft, helicopters, gliders, drones, and ground
    // vehicles are skipped so the display shows commercial airline traffic.
    static const bool COMMERCIAL_FLIGHTS_ONLY = true;

    // Display customization
    // Brightness controls overall display brightness (0-255)
    static const uint8_t DISPLAY_BRIGHTNESS = 32;

    // RGB color for all text rendering on the LED matrix
    static const uint8_t TEXT_COLOR_R = 255;
    static const uint8_t TEXT_COLOR_G = 255;
    static const uint8_t TEXT_COLOR_B = 255;
}
