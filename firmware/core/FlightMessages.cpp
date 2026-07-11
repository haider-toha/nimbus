#include "core/FlightMessages.h"

#include <ctype.h>
#include <math.h>
#include "core/FlightText.h"

namespace
{
constexpr int FULL_COLUMNS = 21;
constexpr const char *ROUTE_ARROW = " > ";

// Appends {type, text} iff text is non-empty and fits FULL_COLUMNS -- the
// one guard every message in this catalog funnels through, so "skip when
// the source field is absent" and "never overflow 21" are each enforced in
// exactly one place. Returns whether it was appended, so callers with a
// documented shorter fallback (direction, operated_by) can try that next.
bool appendIfFits(std::vector<FlightMessage> &out, const char *type, const String &text)
{
    if (text.isEmpty() || static_cast<int>(text.length()) > FULL_COLUMNS)
    {
        return false;
    }
    out.push_back(FlightMessage{type, text});
    return true;
}

// --- emergency -----------------------------------------------------------

bool isEmergencySquawk(const String &squawk)
{
    return squawk == "7500" || squawk == "7600" || squawk == "7700";
}

bool isEmergencyActive(const FlightInfo &flight)
{
    if (isEmergencySquawk(flight.squawk))
    {
        return true;
    }
    return !flight.emergency.isEmpty() && flight.emergency != "none";
}

// Maps the airplanes.live `emergency` enum (none|general|lifeguard|minfuel|
// nordo|unlawful|downed|reserved) to a <=21-char detail label. Falls back to
// the emergency SQUAWK (7500 hijack / 7600 radio failure) when `emergency`
// itself isn't one of the specific enum values (e.g. "reserved", or absent
// while the squawk alone tripped isEmergencyActive) -- "Emergency" is the
// safe default for 7700 / anything unrecognized.
String emergencyDetailText(const FlightInfo &flight)
{
    if (flight.emergency == "unlawful")
    {
        return "Hijack";
    }
    if (flight.emergency == "nordo")
    {
        return "Radio fail";
    }
    if (flight.emergency == "minfuel")
    {
        return "Min fuel";
    }
    if (flight.emergency == "lifeguard")
    {
        return "Lifeguard";
    }
    if (flight.emergency == "downed")
    {
        return "Downed";
    }
    if (flight.emergency == "general")
    {
        return "Emergency";
    }
    if (flight.squawk == "7500")
    {
        return "Hijack";
    }
    if (flight.squawk == "7600")
    {
        return "Radio fail";
    }
    return "Emergency";
}

// --- aircraft_class --------------------------------------------------------

struct CategoryRule
{
    const char *code;
    const char *label;
};

// DO-260B emitter category -> human label (web-confirmed against
// adsbexchange.com / skyglass.io; see audit/display-iteration/DECISIONS.md).
const CategoryRule CATEGORY_RULES[] = {
    {"A1", "Light aircraft"},
    {"A2", "Small aircraft"},
    {"A3", "Large jet"},
    {"A4", "Large jet"},
    {"A5", "Heavy jet"},
    {"A6", "High performance"},
    {"A7", "Helicopter"},
    {"B1", "Glider"},
    {"B2", "Balloon"},
    {"B4", "Ultralight"},
    {"B6", "Drone"},
    {"B7", "Spacecraft"},
};

// A0/empty/unrecognized (B3, B5, D*, ...) return "" so the caller skips the
// message rather than showing a meaningless raw code.
String categoryLabel(const String &category)
{
    if (category.isEmpty())
    {
        return String();
    }
    if (category[0] == 'C')
    {
        return "Ground vehicle";
    }
    for (const CategoryRule &rule : CATEGORY_RULES)
    {
        if (category == rule.code)
        {
            return rule.label;
        }
    }
    return String();
}

// --- distance --------------------------------------------------------------

const char *compass8(double bearingDeg)
{
    static const char *const DIRECTIONS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    double normalized = fmod(bearingDeg, 360.0);
    if (normalized < 0.0)
    {
        normalized += 360.0;
    }
    const int index = static_cast<int>(round(normalized / 45.0)) % 8;
    return DIRECTIONS[index];
}

// --- callsign ----------------------------------------------------------

// Digits parsed out of the flight ident ("BA117" -> "117") for the callsign
// message ("SPEEDBIRD 117"). Empty if ident has no digits.
String digitsOf(const String &text)
{
    String digits;
    const char *bytes = text.c_str();
    for (size_t i = 0; i < text.length(); ++i)
    {
        if (isdigit(static_cast<unsigned char>(bytes[i])))
        {
            digits += bytes[i];
        }
    }
    return digits;
}

// --- one builder function per catalog entry ---------------------------

void addRouteCity(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    const String origin = FlightText::asciiFold(flight.origin.municipality);
    const String destination = FlightText::asciiFold(flight.destination.municipality);
    if (origin.isEmpty() || destination.isEmpty())
    {
        return;
    }
    // No fallback here by design -- route_iata covers the overflow case.
    appendIfFits(out, "route_city", origin + ROUTE_ARROW + destination);
}

void addRouteIata(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.origin.code_iata.isEmpty() || flight.destination.code_iata.isEmpty())
    {
        return;
    }
    appendIfFits(out, "route_iata", flight.origin.code_iata + ROUTE_ARROW + flight.destination.code_iata);
}

void addDirection(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    const String label = FlightText::formatContextLabel(flight);
    const String city = FlightText::formatContextAirport(flight);
    if (label.isEmpty() || city.isEmpty())
    {
        return;
    }
    if (!appendIfFits(out, "direction", label + " " + city))
    {
        // "Arriving from <city>" overflowed 21 -- fall back to just the city.
        appendIfFits(out, "direction", city);
    }
}

void addAirlineType(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.aircraft_code.isEmpty())
    {
        return;
    }
    const String label = FlightText::airlineLabel(flight);
    if (label.isEmpty())
    {
        return;
    }
    appendIfFits(out, "airline_type", label + " " + flight.aircraft_code);
}

void addManufacturer(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.aircraft_manufacturer.isEmpty())
    {
        return;
    }
    String text = FlightText::asciiFold(flight.aircraft_manufacturer);
    if (!flight.aircraft_code.isEmpty())
    {
        text += " ";
        text += flight.aircraft_code;
    }
    appendIfFits(out, "manufacturer", text);
}

void addRegistration(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.registration.isEmpty())
    {
        return;
    }
    appendIfFits(out, "registration", String("Reg ") + flight.registration);
}

void addOperatedBy(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.registered_owner.isEmpty())
    {
        return;
    }
    const String owner = FlightText::asciiFold(flight.registered_owner);
    if (!appendIfFits(out, "operated_by", String("Operated by ") + owner))
    {
        // "Operated by <owner>" overflowed 21 -- fall back to the bare name.
        appendIfFits(out, "operated_by", owner);
    }
}

void addCallsign(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.airline_callsign.isEmpty())
    {
        return;
    }
    String text = FlightText::asciiFold(flight.airline_callsign);
    const String digits = digitsOf(flight.ident);
    if (!digits.isEmpty())
    {
        text += " ";
        text += digits;
    }
    appendIfFits(out, "callsign", text);
}

void addAltitude(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.on_ground)
    {
        appendIfFits(out, "altitude", "On the ground");
        return;
    }
    if (isnan(flight.altitude_ft))
    {
        return;
    }
    String text;
    if (flight.altitude_ft >= 18000.0)
    {
        text = String("Cruising at FL") + String(static_cast<long>(round(flight.altitude_ft / 100.0)));
    }
    else
    {
        text = String("Altitude ") + String(static_cast<long>(round(flight.altitude_ft))) + "ft";
    }
    appendIfFits(out, "altitude", text);
}

void addVertical(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (isnan(flight.vertical_rate_fpm))
    {
        return;
    }
    String text;
    if (flight.vertical_rate_fpm > 100.0)
    {
        text = "Climbing";
    }
    else if (flight.vertical_rate_fpm < -100.0)
    {
        text = "Descending";
    }
    else
    {
        text = "Level flight";
    }
    appendIfFits(out, "vertical", text);
}

void addSpeed(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (isnan(flight.ground_speed_kt))
    {
        return;
    }
    appendIfFits(out, "speed",
                 String("Speed ") + String(static_cast<long>(round(flight.ground_speed_kt))) + " kts");
}

void addDistance(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (isnan(flight.distance_km) || isnan(flight.bearing_deg))
    {
        return;
    }
    appendIfFits(out, "distance",
                 String(static_cast<long>(round(flight.distance_km))) + " km " + compass8(flight.bearing_deg));
}

void addNationality(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.airline_country.isEmpty())
    {
        return;
    }
    const String label = FlightText::airlineLabel(flight);
    if (label.isEmpty())
    {
        return;
    }
    // No fallback by design (per spec) -- airline_country may be long, and
    // there is no shorter form of a country name to fall back to.
    appendIfFits(out, "nationality", label + " " + FlightText::asciiFold(flight.airline_country));
}

void addAircraftClass(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    appendIfFits(out, "aircraft_class", categoryLabel(flight.category));
}

void addSquawk(std::vector<FlightMessage> &out, const FlightInfo &flight)
{
    if (flight.squawk.isEmpty() || isEmergencySquawk(flight.squawk))
    {
        return;
    }
    appendIfFits(out, "squawk", String("Squawk ") + flight.squawk);
}
}

std::vector<FlightMessage> FlightMessages::build(const FlightInfo &flight)
{
    std::vector<FlightMessage> messages;

    if (isEmergencyActive(flight))
    {
        // Always exactly these two, always first -- "emergency" MUST win.
        appendIfFits(messages, "emergency", "EMERGENCY");
        appendIfFits(messages, "emergency_detail", emergencyDetailText(flight));
    }

    // Ordered for variety: route -> direction -> airline/aircraft identity ->
    // ownership -> flight state -> descriptive extras. See DECISIONS.md.
    addRouteCity(messages, flight);
    addRouteIata(messages, flight);
    addDirection(messages, flight);
    addAirlineType(messages, flight);
    addManufacturer(messages, flight);
    addRegistration(messages, flight);
    addAltitude(messages, flight);
    addVertical(messages, flight);
    addSpeed(messages, flight);
    addDistance(messages, flight);
    addOperatedBy(messages, flight);
    addCallsign(messages, flight);
    addNationality(messages, flight);
    addAircraftClass(messages, flight);
    addSquawk(messages, flight);

    return messages;
}
