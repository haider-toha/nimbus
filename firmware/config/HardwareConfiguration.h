#pragma once

#include <Arduino.h>

namespace HardwareConfiguration
{
    static const uint16_t PANEL_WIDTH = 128;
    static const uint16_t PANEL_HEIGHT = 64;
    // Three bitplanes (512 colors) reduce DMA bandwidth enough to prevent
    // WiFi interrupt collisions that cause random pixel sparkle on ESP32-S3.
    static const uint8_t BIT_DEPTH = 3;
    static const bool DOUBLE_BUFFER = true;

    static uint8_t RGB_PINS[] = {42, 41, 40, 38, 39, 37};
    static uint8_t ADDRESS_PINS[] = {45, 36, 48, 35, 21};
    static const uint8_t CLOCK_PIN = 2;
    static const uint8_t LATCH_PIN = 47;
    static const uint8_t OUTPUT_ENABLE_PIN = 14;
}
