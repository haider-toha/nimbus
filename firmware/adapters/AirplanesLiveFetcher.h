#pragma once

#include <Arduino.h>
#include "interfaces/BaseStateVectorFetcher.h"

class AirplanesLiveFetcher : public BaseStateVectorFetcher
{
public:
    bool fetchStateVectors(double centerLat,
                           double centerLon,
                           double radiusKm,
                           std::vector<StateVector> &outStateVectors) override;

private:
    unsigned long _lastRequestMs = 0;
    bool _hasRequested = false;
};
