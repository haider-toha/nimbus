#pragma once

#include <Arduino.h>
#include "interfaces/BaseFlightFetcher.h"

class AdsbdbFetcher : public BaseFlightFetcher
{
public:
    bool fetchFlightInfo(const String &flightIdent,
                         const String &icao24Hex,
                         FlightInfo &outInfo) override;
};
