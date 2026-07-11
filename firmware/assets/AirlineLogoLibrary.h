#pragma once

#include <Arduino.h>

namespace AirlineLogoLibrary
{
    static constexpr uint8_t LOGO_WIDTH = 40;
    static constexpr uint8_t LOGO_HEIGHT = 30;

    const uint16_t *findByIata(const String &iata);
}
