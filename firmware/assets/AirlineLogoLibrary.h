#pragma once

#include <Arduino.h>

namespace AirlineLogoLibrary
{
    static constexpr uint8_t LOGO_WIDTH = 36;
    static constexpr uint8_t LOGO_HEIGHT = 24;

    const uint16_t *findByIata(const String &iata);
}
