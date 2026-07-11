#include "core/FlightText.h"

#include <ctype.h>
#include <math.h>
#include <string.h>
#include "config/UserConfiguration.h"

namespace
{
constexpr double DEGREES_TO_RADIANS = 0.017453292519943295;

// --- asciiFold -------------------------------------------------------------
//
// UTF-8 encodes U+00C0-U+00FF (the Latin-1 Supplement letters/ligatures) as
// lead byte 0xC3 followed by a continuation byte in 0x80-0xBF. This table is
// indexed by (continuationByte - 0x80). nullptr entries are the block's two
// non-letter symbols (U+00D7 multiplication sign, U+00F7 division sign) --
// asciiFold drops them like any other unmapped byte.
const char *const LATIN1_SUPPLEMENT_FOLD[64] = {
    // 0xC0-0xC7: A  A  A  A  A  A  AE  C
    "A", "A", "A", "A", "A", "A", "AE", "C",
    // 0xC8-0xCF: E  E  E  E  I  I  I  I
    "E", "E", "E", "E", "I", "I", "I", "I",
    // 0xD0-0xD7: D  N  O  O  O  O  O  (x, none)
    "D", "N", "O", "O", "O", "O", "O", nullptr,
    // 0xD8-0xDF: O  U  U  U  U  Y  Th  ss
    "O", "U", "U", "U", "U", "Y", "th", "ss",
    // 0xE0-0xE7: a  a  a  a  a  a  ae  c
    "a", "a", "a", "a", "a", "a", "ae", "c",
    // 0xE8-0xEF: e  e  e  e  i  i  i  i
    "e", "e", "e", "e", "i", "i", "i", "i",
    // 0xF0-0xF7: d  n  o  o  o  o  o  (division sign, none)
    "d", "n", "o", "o", "o", "o", "o", nullptr,
    // 0xF8-0xFF: o  u  u  u  u  y  th  y
    "o", "u", "u", "u", "u", "y", "th", "y",
};

constexpr size_t ASCII_FOLD_BUFFER_CAPACITY = 128;

// UTF-8 sequence length (lead byte + continuation bytes) decoded from the
// lead byte's high bits. Returns 1 for anything that isn't a recognized
// multi-byte lead (ASCII, or a stray/invalid byte -- either way, consume
// just the one byte).
size_t utf8SequenceLength(unsigned char lead)
{
    if ((lead & 0xE0) == 0xC0)
    {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0)
    {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0)
    {
        return 4;
    }
    return 1;
}

// --- abbreviateAirline -------------------------------------------------

struct WordRule
{
    const char *word;        // matched case-insensitively, whole word only
    const char *replacement; // nullptr => drop the word entirely
};

// Multi-word generic phrases dropped as a unit before the word-wise pass
// below, longest first. Matched case-insensitively against an ascii-folded
// name. Standalone "Air" is deliberately never in this list -- it is
// meaningful on its own (Air France, Air Canada, ...).
const char *const AIRLINE_DROP_PHRASES_UPPER[] = {
    " AIR LINES COMPANY",
    " AIR LINES",
    " TRANSPORTES AEREOS",
    " LINHAS AEREAS",
    " LINEAS AEREAS",
    " DE AVIACION",
};

// Single-word generics/qualifiers, checked word-by-word after the phrase
// pass. Tuned against all 502 fixture airline names -- see
// audit/display-iteration/DECISIONS.md.
const WordRule AIRLINE_WORD_RULES[] = {
    {"Airlines", nullptr},
    {"Airways", nullptr},
    {"Airline", nullptr},
    {"Company", nullptr},
    {"Connect", nullptr},
    {"Aviation", nullptr},
    {"Service", nullptr},
    {"Shuttle", nullptr},
    {"Commuter", nullptr},
    {"International", "Intl"},
    {"Regional", "Rgnl"},
    {"Express", "Exp"},
    {"National", "Natl"},
    {"Universal", "Univ"},
};

bool equalsIgnoreCaseAscii(const char *a, size_t aLength, const char *b, size_t bLength)
{
    if (aLength != bLength)
    {
        return false;
    }
    for (size_t i = 0; i < aLength; ++i)
    {
        if (tolower(static_cast<unsigned char>(a[i])) !=
            tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

// Case-insensitive whole-word lookup in AIRLINE_WORD_RULES, or nullptr if
// `word` isn't a recognized generic/qualifier.
const WordRule *matchWordRule(const char *word, size_t wordLength)
{
    for (const WordRule &rule : AIRLINE_WORD_RULES)
    {
        if (equalsIgnoreCaseAscii(word, wordLength, rule.word, strlen(rule.word)))
        {
            return &rule;
        }
    }
    return nullptr;
}

// Removes every case-insensitive occurrence of `phraseUpper` (itself an
// uppercase C string) from `text`. Matching is done against a throwaway
// uppercased copy so the casing of whatever `text` keeps is untouched.
void dropPhraseCaseInsensitive(String &text, const char *phraseUpper)
{
    const size_t phraseLength = strlen(phraseUpper);
    for (;;)
    {
        String upper = text;
        upper.toUpperCase();
        const char *hit = strstr(upper.c_str(), phraseUpper);
        if (hit == nullptr)
        {
            return;
        }
        const size_t offset = static_cast<size_t>(hit - upper.c_str());
        text = text.substring(0, offset) + text.substring(offset + phraseLength);
    }
}

// First whitespace-delimited word of `text` -- the fallback identity when
// abbreviation would otherwise drop every word in the name.
String firstWordOf(const String &text)
{
    const char *bytes = text.c_str();
    const size_t length = text.length();
    size_t start = 0;
    while (start < length && bytes[start] == ' ')
    {
        ++start;
    }
    size_t end = start;
    while (end < length && bytes[end] != ' ')
    {
        ++end;
    }
    return text.substring(start, end);
}

// --- formatContextAirport (city-first) --------------------------------

String cityFirstAirportLabel(const AirportInfo &airport)
{
    String label = airport.municipality;
    if (label.isEmpty())
    {
        label = FlightText::formatAirportName(airport);
    }
    if (label.isEmpty())
    {
        label = airport.code_iata;
    }
    return FlightText::asciiFold(label);
}
}

String FlightText::truncateToColumns(const String &text, int maxColumns)
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

String FlightText::cleanCut(const String &text, int maxColumns)
{
    if (static_cast<int>(text.length()) <= maxColumns)
    {
        return text;
    }
    return text.substring(0, maxColumns);
}

String FlightText::asciiFold(const String &text)
{
    char buffer[ASCII_FOLD_BUFFER_CAPACITY];
    size_t outLength = 0;
    const char *bytes = text.c_str();
    const size_t inLength = text.length();

    size_t i = 0;
    while (i < inLength && outLength + 1 < ASCII_FOLD_BUFFER_CAPACITY)
    {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        if (lead < 0x80)
        {
            buffer[outLength++] = static_cast<char>(lead);
            ++i;
            continue;
        }

        if (lead == 0xC3 && i + 1 < inLength)
        {
            const unsigned char continuation = static_cast<unsigned char>(bytes[i + 1]);
            if (continuation >= 0x80 && continuation <= 0xBF)
            {
                for (const char *p = LATIN1_SUPPLEMENT_FOLD[continuation - 0x80];
                     p != nullptr && *p != '\0' && outLength + 1 < ASCII_FOLD_BUFFER_CAPACITY;
                     ++p)
                {
                    buffer[outLength++] = *p;
                }
                i += 2;
                continue;
            }
        }

        // Any other non-ASCII lead byte: swallow its whole UTF-8 sequence
        // (lead + continuation bytes) as one unit and emit nothing for it,
        // rather than leaking stray high bytes one at a time.
        const size_t sequenceLength = utf8SequenceLength(lead);
        size_t consumed = 1;
        while (consumed < sequenceLength && i + consumed < inLength &&
               (static_cast<unsigned char>(bytes[i + consumed]) & 0xC0) == 0x80)
        {
            ++consumed;
        }
        i += consumed;
    }

    buffer[outLength] = '\0';
    return String(buffer);
}

String FlightText::abbreviateAirline(const String &name)
{
    String working = name;
    for (const char *phrase : AIRLINE_DROP_PHRASES_UPPER)
    {
        dropPhraseCaseInsensitive(working, phrase);
    }

    const char *bytes = working.c_str();
    const size_t length = working.length();
    String result;
    size_t i = 0;
    while (i < length)
    {
        while (i < length && bytes[i] == ' ')
        {
            ++i;
        }
        const size_t wordStart = i;
        while (i < length && bytes[i] != ' ')
        {
            ++i;
        }
        const size_t wordLength = i - wordStart;
        if (wordLength == 0)
        {
            continue;
        }

        const WordRule *rule = matchWordRule(bytes + wordStart, wordLength);
        const bool drop = (rule != nullptr && rule->replacement == nullptr);
        if (drop)
        {
            continue;
        }

        if (!result.isEmpty())
        {
            result += " ";
        }
        result += (rule != nullptr) ? String(rule->replacement) : working.substring(wordStart, i);
    }

    if (result.isEmpty())
    {
        return firstWordOf(name);
    }
    return result;
}

String FlightText::airlineLabel(const FlightInfo &flight)
{
    String name = flight.airline_display_name_full;
    if (name.isEmpty())
    {
        name = !flight.operator_iata.isEmpty()
                   ? flight.operator_iata
                   : flight.operator_icao;
    }
    return abbreviateAirline(asciiFold(name));
}

String FlightText::formatAltitude(const FlightInfo &flight)
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

String FlightText::formatRoute(const FlightInfo &flight)
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

String FlightText::formatAirportName(const AirportInfo &airport)
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

double FlightText::distanceFromCenterSquared(const AirportInfo &airport)
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

String FlightText::formatContextLabel(const FlightInfo &flight)
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

String FlightText::formatContextAirport(const FlightInfo &flight)
{
    const double originDistance = distanceFromCenterSquared(flight.origin);
    const double destinationDistance =
        distanceFromCenterSquared(flight.destination);
    if (!isnan(originDistance) && !isnan(destinationDistance))
    {
        return destinationDistance < originDistance
                   ? cityFirstAirportLabel(flight.origin)
                   : cityFirstAirportLabel(flight.destination);
    }
    if (!flight.origin.name.isEmpty())
    {
        return cityFirstAirportLabel(flight.origin);
    }
    return cityFirstAirportLabel(flight.destination);
}
