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

    // QA/test introspection only: the rotation counter the most recent
    // displayFlights() used to pick the bottom-message pair (see
    // FlightMessages::build()). Lets the host QA harness prove message
    // rotation by reading real state instead of re-deriving the tick
    // arithmetic -- the same "inspect the real object" philosophy as
    // Adafruit_Protomatter::nimbusLastInstance() in the fake runtime.
    size_t currentMessageIndex() const { return _messageIndex; }

private:
    Adafruit_Protomatter _matrix;
    size_t _currentFlightIndex = 0;
    size_t _messageIndex = 0;
    unsigned long _lastCycleMs = 0;

    uint16_t textColor();
    void drawAirlineLogo(const FlightInfo &flight, int16_t x, int16_t y);
    void drawTextLine(int16_t x, int16_t y, const String &text, uint16_t color);
    void displaySingleFlightCard(const FlightInfo &flight);
    void displayLoadingScreen();
    void showFrame();
};
