#include "adapters/Hub75Display.h"

#include <math.h>
#include "assets/AirlineLogoLibrary.h"
#include "config/HardwareConfiguration.h"
#include "config/TimingConfiguration.h"
#include "config/UserConfiguration.h"

namespace
{
constexpr int CHARACTER_WIDTH = 6;
constexpr int CHARACTER_HEIGHT = 8;
constexpr int LOGO_X = 2;
constexpr int LOGO_Y = 1;
constexpr int TOP_TEXT_X = 42;
constexpr double DEGREES_TO_RADIANS = 0.017453292519943295;
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
    _matrix.show();
}

uint16_t Hub75Display::textColor()
{
    const uint16_t brightness = UserConfiguration::DISPLAY_BRIGHTNESS;
    const uint8_t red = UserConfiguration::TEXT_COLOR_R * brightness / 255;
    const uint8_t green = UserConfiguration::TEXT_COLOR_G * brightness / 255;
    const uint8_t blue = UserConfiguration::TEXT_COLOR_B * brightness / 255;
    return _matrix.color565(red, green, blue);
}

String Hub75Display::truncateToColumns(const String &text, int maxColumns)
{
    if (static_cast<int>(text.length()) <= maxColumns)
    {
        return text;
    }
    if (maxColumns <= 3)
    {
        return text.substring(0, maxColumns);
    }
    return text.substring(0, maxColumns - 3) + "...";
}

String Hub75Display::formatAltitude(const FlightInfo &flight)
{
    if (flight.on_ground)
    {
        return "GND";
    }
    if (isnan(flight.altitude_ft))
    {
        return "";
    }
    return String(static_cast<long>(round(flight.altitude_ft))) + "FT";
}

String Hub75Display::formatRoute(const FlightInfo &flight)
{
    const String origin = !flight.origin.code_iata.isEmpty()
                              ? flight.origin.code_iata
                              : flight.origin.code_icao;
    const String destination = !flight.destination.code_iata.isEmpty()
                                   ? flight.destination.code_iata
                                   : flight.destination.code_icao;
    if (origin.isEmpty())
    {
        return destination;
    }
    if (destination.isEmpty())
    {
        return origin;
    }
    return origin + "-" + destination;
}

String Hub75Display::formatAirportName(const AirportInfo &airport)
{
    String name = airport.name;
    if (name.isEmpty())
    {
        name = airport.municipality;
    }
    name.replace(" International Airport", " Intl");
    name.replace(" International", " Intl");
    name.replace(" Airport", "");
    return name;
}

double Hub75Display::distanceFromCenterSquared(const AirportInfo &airport)
{
    if (isnan(airport.latitude) || isnan(airport.longitude))
    {
        return NAN;
    }

    const double latitudeDelta =
        airport.latitude - UserConfiguration::CENTER_LAT;
    const double longitudeScale =
        cos(UserConfiguration::CENTER_LAT * DEGREES_TO_RADIANS);
    const double longitudeDelta =
        (airport.longitude - UserConfiguration::CENTER_LON) *
        longitudeScale;
    return latitudeDelta * latitudeDelta +
           longitudeDelta * longitudeDelta;
}

String Hub75Display::formatContextLabel(const FlightInfo &flight)
{
    const double originDistance = distanceFromCenterSquared(flight.origin);
    const double destinationDistance =
        distanceFromCenterSquared(flight.destination);
    if (!isnan(originDistance) && !isnan(destinationDistance))
    {
        return destinationDistance < originDistance
                   ? "Arriving from"
                   : "Departing to";
    }
    if (!flight.origin.name.isEmpty())
    {
        return "Arriving from";
    }
    if (!flight.destination.name.isEmpty())
    {
        return "Departing to";
    }
    return "";
}

String Hub75Display::formatContextAirport(const FlightInfo &flight)
{
    const double originDistance = distanceFromCenterSquared(flight.origin);
    const double destinationDistance =
        distanceFromCenterSquared(flight.destination);
    if (!isnan(originDistance) && !isnan(destinationDistance))
    {
        return destinationDistance < originDistance
                   ? formatAirportName(flight.origin)
                   : formatAirportName(flight.destination);
    }
    if (!flight.origin.name.isEmpty())
    {
        return formatAirportName(flight.origin);
    }
    return formatAirportName(flight.destination);
}

void Hub75Display::drawAirlineLogo(
    const FlightInfo &flight,
    int16_t x,
    int16_t y)
{
    const uint16_t *logo =
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
            const uint16_t pixel =
                logo[row * AirlineLogoLibrary::LOGO_WIDTH + column];
            if (pixel == 0)
            {
                continue;
            }

            const uint8_t red =
                (((pixel >> 11) & 0x1F) * 255 / 31) *
                brightness / 255;
            const uint8_t green =
                (((pixel >> 5) & 0x3F) * 255 / 63) *
                brightness / 255;
            const uint8_t blue =
                ((pixel & 0x1F) * 255 / 31) *
                brightness / 255;
            _matrix.drawPixel(
                x + column,
                y + row,
                _matrix.color565(red, green, blue));
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
    const String altitude = formatAltitude(flight);
    if (!altitude.isEmpty())
    {
        identity += " ";
        identity += altitude;
    }

    String airline = flight.airline_display_name_full;
    if (airline.isEmpty())
    {
        airline = !flight.operator_iata.isEmpty()
                      ? flight.operator_iata
                      : flight.operator_icao;
    }

    const String aircraft = !flight.aircraft_display_name_short.isEmpty()
                                ? flight.aircraft_display_name_short
                                : flight.aircraft_code;

    drawTextLine(
        TOP_TEXT_X,
        0,
        truncateToColumns(airline, topColumns),
        color);
    drawTextLine(
        TOP_TEXT_X,
        9,
        truncateToColumns(formatRoute(flight), topColumns),
        color);
    drawTextLine(
        TOP_TEXT_X,
        18,
        truncateToColumns(aircraft, topColumns),
        color);
    drawTextLine(
        2,
        31,
        truncateToColumns(formatContextLabel(flight), fullColumns),
        color);
    drawTextLine(
        2,
        40,
        truncateToColumns(formatContextAirport(flight), fullColumns),
        color);
    drawTextLine(
        2,
        53,
        truncateToColumns(identity, fullColumns),
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
    if (flights.size() == 1)
    {
        _currentFlightIndex = 0;
    }
    else if (now - _lastCycleMs >= intervalMs)
    {
        _lastCycleMs = now;
        _currentFlightIndex = (_currentFlightIndex + 1) % flights.size();
    }

    displaySingleFlightCard(flights[_currentFlightIndex % flights.size()]);
    _matrix.show();
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
    _matrix.show();
}

void Hub75Display::displayMessage(const String &message)
{
    _matrix.fillScreen(0);
    const int maxColumns =
        HardwareConfiguration::PANEL_WIDTH / CHARACTER_WIDTH;
    const String line = truncateToColumns(message, maxColumns);
    const int16_t y =
        (HardwareConfiguration::PANEL_HEIGHT - CHARACTER_HEIGHT) / 2;
    drawTextLine(0, y, line, textColor());
    _matrix.show();
}

void Hub75Display::showLoading()
{
    displayLoadingScreen();
}
