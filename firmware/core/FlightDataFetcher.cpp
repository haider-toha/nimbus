#include "core/FlightDataFetcher.h"

#include <algorithm>
#include <math.h>
#include "config/UserConfiguration.h"

FlightDataFetcher::FlightDataFetcher(BaseStateVectorFetcher *stateFetcher,
                                     BaseFlightFetcher *flightFetcher)
    : _stateFetcher(stateFetcher), _flightFetcher(flightFetcher) {}

size_t FlightDataFetcher::fetchFlights(std::vector<StateVector> &outStates,
                                       std::vector<FlightInfo> &outFlights)
{
    outStates.clear();
    outFlights.clear();

    bool ok = _stateFetcher->fetchStateVectors(
        UserConfiguration::CENTER_LAT,
        UserConfiguration::CENTER_LON,
        UserConfiguration::RADIUS_KM,
        outStates);
    if (!ok)
        return 0;

    std::sort(
        outStates.begin(),
        outStates.end(),
        [](const StateVector &left, const StateVector &right)
        {
            const bool leftUnknown = isnan(left.distance_km);
            const bool rightUnknown = isnan(right.distance_km);
            if (leftUnknown != rightUnknown)
            {
                return !leftUnknown;
            }
            return left.distance_km < right.distance_km;
        });

    size_t enriched = 0;
    const size_t flightLimit = std::min(
        outStates.size(),
        static_cast<size_t>(UserConfiguration::MAX_TRACKED_FLIGHTS));

    for (size_t index = 0; index < flightLimit; ++index)
    {
        const StateVector &s = outStates[index];
        if (s.callsign.length() == 0)
        {
            continue;
        }

        FlightInfo info;
        info.ident = s.callsign;
        info.aircraft_code = s.aircraft_code;
        info.altitude_ft = s.baro_altitude_ft;
        info.on_ground = s.on_ground;

        if (_flightFetcher->fetchFlightInfo(s.callsign, s.icao24, info))
        {
            enriched++;
        }

        outFlights.push_back(info);
    }

    return enriched;
}
