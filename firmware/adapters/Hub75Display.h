#pragma once

#include <Adafruit_Protomatter.h>
#include "interfaces/BaseDisplay.h"

class Hub75Display : public BaseDisplay
{
public:
    Hub75Display();

    bool initialize() override;
    void clear() override;
    void displayFlights(const std::vector<FlightInfo> &flights) override;
    void displayMessage(const String &message);
    void showLoading();

private:
    Adafruit_Protomatter _matrix;
    size_t _currentFlightIndex = 0;
    unsigned long _lastCycleMs = 0;

    uint16_t textColor();
    String truncateToColumns(const String &text, int maxColumns);
    String formatAltitude(const FlightInfo &flight);
    String formatRoute(const FlightInfo &flight);
    String formatAirportName(const AirportInfo &airport);
    String formatContextLabel(const FlightInfo &flight);
    String formatContextAirport(const FlightInfo &flight);
    double distanceFromCenterSquared(const AirportInfo &airport);
    void drawAirlineLogo(const FlightInfo &flight, int16_t x, int16_t y);
    void drawTextLine(int16_t x, int16_t y, const String &text, uint16_t color);
    void displaySingleFlightCard(const FlightInfo &flight);
    void displayLoadingScreen();
};
