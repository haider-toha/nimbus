#pragma once

#include <Arduino.h>
#include "models/FlightInfo.h"

class BaseFlightFetcher
{
public:
    virtual ~BaseFlightFetcher() = default;
    virtual bool fetchFlightInfo(const String &flightIdent,
                                 const String &icao24Hex,
                                 FlightInfo &outInfo) = 0;
};
