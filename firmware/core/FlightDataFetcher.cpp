#include "core/FlightDataFetcher.h"

#include <algorithm>
#include <math.h>
#include "assets/AirlineLogoLibrary.h"
#include "config/UserConfiguration.h"

namespace
{
String operatorIcaoFromCallsign(const String &callsign)
{
    String normalized = callsign;
    normalized.trim();
    normalized.toUpperCase();
    if (normalized.length() < 3)
    {
        return String();
    }

    for (size_t index = 0; index < 3; ++index)
    {
        const char character = normalized[index];
        if (character < 'A' || character > 'Z')
        {
            return String();
        }
    }
    return normalized.substring(0, 3);
}
}

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
    size_t collected = 0;

    for (size_t index = 0;
         index < outStates.size() &&
         collected < UserConfiguration::MAX_TRACKED_FLIGHTS;
         ++index)
    {
        const StateVector &s = outStates[index];
        if (s.callsign.length() == 0)
        {
            continue;
        }

        const String mappedOperatorIcao =
            operatorIcaoFromCallsign(s.callsign);
        const String mappedOperatorIata =
            AirlineLogoLibrary::findIataByIcao(mappedOperatorIcao);
        if (UserConfiguration::REQUIRE_AIRLINE_LOGO &&
            mappedOperatorIata.isEmpty())
        {
            continue;
        }

        FlightInfo info;
        info.ident = s.callsign;
        info.operator_icao = mappedOperatorIcao;
        info.operator_iata = mappedOperatorIata;
        info.aircraft_code = s.aircraft_code;
        info.altitude_ft = s.baro_altitude_ft;
        info.on_ground = s.on_ground;

        const bool wasEnriched =
            _flightFetcher->fetchFlightInfo(s.callsign, s.icao24, info);
        if (!AirlineLogoLibrary::hasIata(info.operator_iata))
        {
            info.operator_iata = mappedOperatorIata;
        }
        if (UserConfiguration::REQUIRE_AIRLINE_LOGO &&
            !AirlineLogoLibrary::hasIata(info.operator_iata))
        {
            continue;
        }
        if (wasEnriched)
        {
            enriched++;
        }

        // Live ADS-B fields have no adsbdb equivalent: always copy from the
        // state vector so the display can render them.
        info.ground_speed_kt = s.ground_speed_kt;
        info.vertical_rate_fpm = s.vertical_rate_fpm;
        info.track_deg = s.track_deg;
        info.squawk = s.squawk;
        info.emergency = s.emergency;
        info.category = s.category;
        info.distance_km = s.distance_km;
        info.bearing_deg = s.bearing_deg;

        // registration/aircraft_full_type can come from either source: adsbdb
        // enrichment wins when present, the live vector fills the gap when
        // adsbdb didn't populate it (e.g. aircraft lookup miss).
        if (info.registration.isEmpty())
        {
            info.registration = s.registration;
        }
        if (info.aircraft_full_type.isEmpty())
        {
            info.aircraft_full_type = s.aircraft_full_type;
        }

        outFlights.push_back(info);
        ++collected;
    }

    return enriched;
}
