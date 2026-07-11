#pragma once

#include <Arduino.h>

namespace AirlineLogoLibrary
{
    static constexpr uint8_t LOGO_WIDTH = 40;
    static constexpr uint8_t LOGO_HEIGHT = 30;
    static constexpr size_t PIXELS_PER_LOGO = 1200;
    static constexpr size_t BYTES_PER_LOGO = 2400;

    const uint8_t *findByIata(const String &iata);
    bool hasIata(const String &iata);
    String findIataByIcao(const String &icao);
}
