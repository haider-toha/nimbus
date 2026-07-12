#include "adapters/Hub75Display.h"

#include <math.h>
#include <string.h>
#include "assets/AirlineLogoLibrary.h"
#include "config/HardwareConfiguration.h"
#include "config/TimingConfiguration.h"
#include "config/UserConfiguration.h"
#include "core/FlightMessages.h"
#include "core/FlightText.h"

namespace
{
constexpr int CHARACTER_WIDTH = 6;
constexpr int CHARACTER_HEIGHT = 8;
constexpr int LOGO_X = 2;
constexpr int LOGO_Y = 1;
constexpr int TOP_TEXT_X = 42;
// Topmost bottom-block text line (see displaySingleFlightCard's rotating
// message pair). WS4's logo cell is sized against this staying put.
constexpr int BOTTOM_TEXT_Y = 31;
constexpr uint8_t BAYER_SIZE = 8;
constexpr uint8_t BAYER_AREA = BAYER_SIZE * BAYER_SIZE;
constexpr uint8_t BAYER_8X8[BAYER_AREA] = {
    0, 32, 8, 40, 2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4, 36, 14, 46, 6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
    3, 35, 11, 43, 1, 33, 9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7, 39, 13, 45, 5, 37,
    63, 31, 55, 23, 61, 29, 53, 21};

// WS4 (bigger per-airline logos): the logo cell must not run into either
// text zone. Enforced at compile time -- rather than only by a runtime QA
// check -- so a future logo-size or text-position change that breaks this
// fails the build instead of silently overlapping on the panel.
static_assert(
    LOGO_X + AirlineLogoLibrary::LOGO_WIDTH <= TOP_TEXT_X,
    "airline logo cell must not overlap the top-right text column");
static_assert(
    LOGO_Y + AirlineLogoLibrary::LOGO_HEIGHT <= BOTTOM_TEXT_Y,
    "airline logo cell must not overlap the bottom message block");

uint8_t expandLevel(uint32_t level, uint16_t levels)
{
    return static_cast<uint8_t>((level * 255U + levels / 2U) / levels);
}

uint8_t scaleTextChannel(uint8_t channel, uint16_t brightness)
{
    const uint16_t levels =
        (1U << HardwareConfiguration::BIT_DEPTH) - 1U;
    const uint32_t denominator = 255U * 255U;
    const uint32_t scaled =
        static_cast<uint32_t>(channel) * brightness * levels;
    const uint32_t scaledLevel =
        (scaled + denominator / 2U) / denominator;
    return expandLevel(scaledLevel, levels);
}

uint8_t ditherLogoChannel(
    uint8_t channel,
    uint16_t brightness,
    uint8_t x,
    uint8_t y)
{
    const uint16_t levels =
        (1U << HardwareConfiguration::BIT_DEPTH) - 1U;
    const uint32_t denominator = 255U * 255U;
    const uint32_t scaled =
        static_cast<uint32_t>(channel) * brightness * levels;
    uint32_t level = scaled / denominator;
    const uint32_t remainder = scaled % denominator;
    const uint8_t threshold =
        BAYER_8X8[(y % BAYER_SIZE) * BAYER_SIZE + (x % BAYER_SIZE)];

    if (level < levels &&
        remainder * (BAYER_AREA * 2U) >
            static_cast<uint32_t>(threshold * 2U + 1U) * denominator)
    {
        ++level;
    }
    return expandLevel(level, levels);
}
}

Hub75Display::Hub75Display()
    : _matrix(
          HardwareConfiguration::PANEL_WIDTH,
          HardwareConfiguration::BIT_DEPTH,
          1,
          HardwareConfiguration::RGB_PINS,
          5,
          HardwareConfiguration::ADDRESS_PINS,
          HardwareConfiguration::CLOCK_PIN,
          HardwareConfiguration::LATCH_PIN,
          HardwareConfiguration::OUTPUT_ENABLE_PIN,
          HardwareConfiguration::DOUBLE_BUFFER)
{
}

bool Hub75Display::initialize()
{
    const ProtomatterStatus status = _matrix.begin();
    if (status != PROTOMATTER_OK)
    {
        Serial.printf("Hub75Display: Initialization failed with status %d\n", status);
        return false;
    }

    _matrix.setTextWrap(false);
    _matrix.setTextSize(1);
    _currentFlightIndex = 0;
    _lastCycleMs = millis();
    clear();
    return true;
}

void Hub75Display::clear()
{
    _matrix.fillScreen(0);
    showFrame();
}

uint16_t Hub75Display::textColor()
{
    const uint16_t brightness = UserConfiguration::DISPLAY_BRIGHTNESS;
    const uint8_t red =
        scaleTextChannel(UserConfiguration::TEXT_COLOR_R, brightness);
    const uint8_t green =
        scaleTextChannel(UserConfiguration::TEXT_COLOR_G, brightness);
    const uint8_t blue =
        scaleTextChannel(UserConfiguration::TEXT_COLOR_B, brightness);
    return _matrix.color565(red, green, blue);
}

void Hub75Display::drawAirlineLogo(
    const FlightInfo &flight,
    int16_t x,
    int16_t y)
{
    const uint8_t *logo =
        AirlineLogoLibrary::findByIata(flight.operator_iata);
    if (logo == nullptr)
    {
        return;
    }

    const uint16_t brightness = UserConfiguration::DISPLAY_BRIGHTNESS;
    for (uint8_t row = 0; row < AirlineLogoLibrary::LOGO_HEIGHT; ++row)
    {
        for (uint8_t column = 0;
             column < AirlineLogoLibrary::LOGO_WIDTH;
             ++column)
        {
            const size_t pixelIndex =
                row * AirlineLogoLibrary::LOGO_WIDTH + column;
            const size_t byteIndex = pixelIndex * 2;
            const uint16_t pixel =
                static_cast<uint16_t>(logo[byteIndex]) |
                (static_cast<uint16_t>(logo[byteIndex + 1]) << 8);
            if (pixel == 0)
            {
                continue;
            }

            const uint8_t red = ditherLogoChannel(
                static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255U / 31U),
                brightness,
                column,
                row);
            const uint8_t green = ditherLogoChannel(
                static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255U / 63U),
                brightness,
                column,
                row);
            const uint8_t blue = ditherLogoChannel(
                static_cast<uint8_t>((pixel & 0x1F) * 255U / 31U),
                brightness,
                column,
                row);
            const uint16_t scaledPixel = _matrix.color565(red, green, blue);
            _matrix.drawPixel(
                x + column,
                y + row,
                scaledPixel);
        }
    }
}

void Hub75Display::drawTextLine(
    int16_t x,
    int16_t y,
    const String &text,
    uint16_t color)
{
    _matrix.setCursor(x, y);
    _matrix.setTextColor(color);
    _matrix.print(text);
}

void Hub75Display::displaySingleFlightCard(const FlightInfo &flight)
{
    const uint16_t color = textColor();
    const int topColumns =
        (HardwareConfiguration::PANEL_WIDTH - TOP_TEXT_X) /
        CHARACTER_WIDTH;
    const int fullColumns =
        HardwareConfiguration::PANEL_WIDTH / CHARACTER_WIDTH;

    drawAirlineLogo(flight, LOGO_X, LOGO_Y);

    String identity = flight.ident.length() > 0
                          ? flight.ident
                          : flight.ident_icao;
    const String altitude = FlightText::formatAltitude(flight);
    if (!altitude.isEmpty())
    {
        identity += " ";
        identity += altitude;
    }

    const String aircraft = !flight.aircraft_display_name_short.isEmpty()
                                ? flight.aircraft_display_name_short
                                : flight.aircraft_code;

    drawTextLine(
        TOP_TEXT_X,
        0,
        FlightText::cleanCut(FlightText::airlineLabel(flight), topColumns),
        color);
    drawTextLine(
        TOP_TEXT_X,
        9,
        FlightText::truncateToColumns(FlightText::formatRoute(flight), topColumns),
        color);
    drawTextLine(
        TOP_TEXT_X,
        18,
        FlightText::truncateToColumns(aircraft, topColumns),
        color);

    // Bottom block: two rotating message lines (BOTTOM_TEXT_Y, y=40) + the
    // identity anchor (y=53). BOTTOM_TEXT_Y is intentionally left as the
    // topmost bottom line -- WS4's logo-height ceiling depends on it
    // staying put, and the static_assert above turns a future violation
    // into a build failure instead of a silent overlap.
    const std::vector<FlightMessage> messages = FlightMessages::build(flight);
    const bool emergencyActive =
        !messages.empty() && strcmp(messages[0].type, "emergency") == 0;

    String slotA;
    String slotB;
    if (!messages.empty())
    {
        if (emergencyActive)
        {
            // Emergency always wins: slots are pinned to the emergency pair,
            // never rotated away regardless of _messageIndex.
            slotA = messages[0].text;
            slotB = messages.size() > 1 ? messages[1].text : String();
        }
        else
        {
            slotA = messages[_messageIndex % messages.size()].text;
            slotB = messages.size() > 1
                        ? messages[(_messageIndex + 1) % messages.size()].text
                        : String();
        }
    }

    drawTextLine(2, BOTTOM_TEXT_Y, FlightText::cleanCut(slotA, fullColumns), color);
    drawTextLine(2, 40, FlightText::cleanCut(slotB, fullColumns), color);
    drawTextLine(
        2,
        53,
        FlightText::truncateToColumns(identity, fullColumns),
        color);
}

void Hub75Display::displayFlights(const std::vector<FlightInfo> &flights)
{
    _matrix.fillScreen(0);

    if (flights.empty())
    {
        displayLoadingScreen();
        return;
    }

    const unsigned long now = millis();
    const unsigned long intervalMs =
        TimingConfiguration::DISPLAY_CYCLE_SECONDS * 1000UL;
    // The message index advances on this tick regardless of flight count --
    // a single tracked flight still rotates its bottom-message pair. Flight
    // cycling itself is unchanged: it only advances (and only exists) when
    // there is more than one flight to cycle through.
    if (now - _lastCycleMs >= intervalMs)
    {
        _lastCycleMs = now;
        ++_messageIndex;
        if (flights.size() > 1)
        {
            _currentFlightIndex = (_currentFlightIndex + 1) % flights.size();
        }
    }
    if (flights.size() == 1)
    {
        _currentFlightIndex = 0;
    }

    displaySingleFlightCard(flights[_currentFlightIndex % flights.size()]);
    showFrame();
}

void Hub75Display::displayLoadingScreen()
{
    _matrix.fillScreen(0);
    const uint16_t color = textColor();

    const String message = "...";
    const int16_t x =
        (HardwareConfiguration::PANEL_WIDTH -
         static_cast<int>(message.length()) * CHARACTER_WIDTH) /
        2;
    const int16_t y =
        (HardwareConfiguration::PANEL_HEIGHT - CHARACTER_HEIGHT) / 2;
    drawTextLine(x, y, message, color);
    showFrame();
}

void Hub75Display::displayMessage(const String &message)
{
    _matrix.fillScreen(0);
    const int maxColumns =
        HardwareConfiguration::PANEL_WIDTH / CHARACTER_WIDTH;
    const String line = FlightText::truncateToColumns(message, maxColumns);
    const int16_t y =
        (HardwareConfiguration::PANEL_HEIGHT - CHARACTER_HEIGHT) / 2;
    drawTextLine(0, y, line, textColor());
    showFrame();
}

void Hub75Display::showLoading()
{
    displayLoadingScreen();
}

void Hub75Display::showFrame()
{
    _matrix.show();
    vTaskDelay(1);
}
