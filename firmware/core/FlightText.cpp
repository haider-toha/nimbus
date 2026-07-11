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

// Top airline line budget: (PANEL_WIDTH 128 - TOP_TEXT_X 42) / CHAR_WIDTH 6 = 14
// columns (Hub75Display::displaySingleFlightCard). abbreviateAirline fits its
// result to this so the harness measures exactly what the panel shows.
constexpr int AIRLINE_LABEL_COLUMNS = 14;

// Single-word generics/qualifiers, checked word-by-word after the phrase pass.
// "Airlines"/"Airways"/"Airline" are handled separately (see isAirGeneric +
// the Rule-1 keep/drop below), NOT here: dropping them unconditionally left
// bare adjectives ("British Airways" -> "British") -- the seed defect. The
// once-abbreviated qualifiers (Regional/Express/National/Universal) are kept in
// full now; word-boundary truncation shortens on overflow instead of emitting
// cryptic vowel-strips ("Rgnl"/"Exp"). See audit/display-iteration/07-*.
const WordRule AIRLINE_WORD_RULES[] = {
    {"Company", nullptr},
    {"Connect", nullptr},
    {"Aviation", nullptr},
    {"Service", nullptr},
    {"Shuttle", nullptr},
    {"Commuter", nullptr},
    {"International", "Intl"},
};

// Whole (ascii-folded) airline name -> panel label, for carriers where
// algorithmic abbreviation loses or mangles the brand. Matched case-
// insensitively against the folded full name, BEFORE any other rule. Tiny,
// grounded allowlist -- every entry justified in audit/display-iteration/07-*
// (bare-adjective flag carriers -> "<X> Air"; initialisms; rebrands; the
// Virgin pair, which would otherwise both collapse to "Virgin").
struct BrandOverride
{
    const char *nameUpper;
    const char *label;
};

const BrandOverride BRAND_OVERRIDES[] = {
    {"BRITISH AIRWAYS", "British Air"},
    {"TURKISH AIRLINES", "Turkish Air"},
    {"SINGAPORE AIRLINES", "Singapore Air"},
    {"PHILIPPINE AIRLINES", "Philippine Air"},
    {"SCANDINAVIAN AIRLINES", "SAS"},
    {"SOUTH AFRICAN AIRWAYS", "SAA"},
    {"REGIONAL EXPRESS", "Rex"},
    {"THAI AIRWAYS INTERNATIONAL", "Thai Airways"},
    {"JAPAN TRANSOCEAN AIR", "Transocean Air"},
    {"ROYAL AIR MAROC", "Air Maroc"},
    {"CORSAIRFLY", "Corsair"},
    {"AIRLINES PNG", "PNG Air"},
    {"NOUVEL AIR TUNISIE", "Nouvelair"},
    {"BINTER CANARIAS", "Binter"},
    {"VIRGIN AUSTRALIA", "Virgin Aust"},
    {"VIRGIN ATLANTIC", "Virgin Atl"},
    // Brand is the LAST word ("<country> AirAsia") -- trailing-word truncation
    // would keep the country and drop the brand, so pin the brand-forward form.
    {"INDONESIA AIRASIA", "AirAsia Indo"},
    {"PHILIPPINES AIRASIA", "AirAsia PH"},
    // Would otherwise collapse to a bare "Lufthansa" and collide with LH.
    {"LUFTHANSA CITYLINE", "LH CityLine"},
};

// Country/region token -> short form, applied to a whole word or phrase ONLY
// when the label still overflows 14 cols. On-overflow-only means iconic short
// names ("Air France") are never touched; the point is to keep sibling
// subsidiaries distinct (Smartwings PL/SK/HU, TUI fly NL/BE/Germany, the Air
// Arabia / Wizz "Abu Dhabi" variants) instead of collapsing them to a shared
// stem via truncation. Longer entries first so phrases win over their words.
struct RegionAbbrev
{
    const char *fromUpper;
    const char *to;
};

const RegionAbbrev REGION_ABBREVIATIONS[] = {
    {"ABU DHABI", "AUH"},
    {"SAINT", "St"},
    {"NETHERLANDS", "NL"},
    {"DEUTSCHLAND", "DE"},
    {"SWITZERLAND", "CH"},
    {"SLOVAKIA", "SK"},
    {"HOLLAND", "NL"},
    {"HUNGARY", "HU"},
    {"POLAND", "PL"},
    {"BELGIUM", "BE"},
    {"ECUADOR", "EC"},
    {"SWEDEN", "SE"},
    {"FRANCE", "FR"},
    {"MAROC", "MA"},
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

// "Airlines"/"Airways"/"Airline" -- the generic dropped conditionally by
// Rule 1 (kept when the untrimmed name fits, so bare adjectives like "British"
// don't survive alone).
bool isAirGeneric(const char *word, size_t wordLength)
{
    return equalsIgnoreCaseAscii(word, wordLength, "Airlines", 8) ||
           equalsIgnoreCaseAscii(word, wordLength, "Airways", 7) ||
           equalsIgnoreCaseAscii(word, wordLength, "Airline", 7);
}

// Whole-name brand override lookup (case-insensitive). Returns true and sets
// `label` when the ascii-folded `name` exactly matches a BRAND_OVERRIDES key.
bool matchBrandOverride(const String &name, String &label)
{
    String upper = name;
    upper.trim();
    upper.toUpperCase();
    for (const BrandOverride &entry : BRAND_OVERRIDES)
    {
        if (upper == entry.nameUpper)
        {
            label = entry.label;
            return true;
        }
    }
    return false;
}

// Replace every WHOLE-WORD/phrase (case-insensitive) occurrence of `fromUpper`
// in `text` with `to`, preserving the surrounding text's casing. Boundary-
// checked so "FRANCE" never matches inside another token.
void replaceRegionToken(String &text, const char *fromUpper, const char *to)
{
    const size_t fromLength = strlen(fromUpper);
    size_t searchFrom = 0;
    for (;;)
    {
        String upper = text;
        upper.toUpperCase();
        const char *hit = strstr(upper.c_str() + searchFrom, fromUpper);
        if (hit == nullptr)
        {
            return;
        }
        const size_t offset = static_cast<size_t>(hit - upper.c_str());
        const bool atStart = (offset == 0) || (text[offset - 1] == ' ');
        const size_t afterIndex = offset + fromLength;
        const bool atEnd =
            (afterIndex >= text.length()) || (text[afterIndex] == ' ');
        if (atStart && atEnd)
        {
            text = text.substring(0, offset) + to + text.substring(afterIndex);
            searchFrom = offset + strlen(to);
        }
        else
        {
            searchFrom = offset + 1;
        }
    }
}

// Trailing "function words" a label must never END on after truncation, so
// "Sun Air of Scandinavia" fits as "Sun Air", not "Sun Air of".
bool isTrailingFunctionWord(const char *word, size_t wordLength)
{
    return equalsIgnoreCaseAscii(word, wordLength, "of", 2) ||
           equalsIgnoreCaseAscii(word, wordLength, "the", 3) ||
           equalsIgnoreCaseAscii(word, wordLength, "de", 2) ||
           equalsIgnoreCaseAscii(word, wordLength, "da", 2) ||
           equalsIgnoreCaseAscii(word, wordLength, "du", 2) ||
           equalsIgnoreCaseAscii(word, wordLength, "and", 3);
}

// A single leading generic ("Air", "Royal", "Airlines"...) is meaningless as
// the whole label. If dropping trailing words reduces to just this, the
// identifying word was at the END ("Air Mediterranean", "Royal Jordanian") --
// hard-cut the original instead so that identity survives.
bool isBareLeadingGeneric(const String &text)
{
    const char *bytes = text.c_str();
    const size_t length = text.length();
    for (size_t i = 0; i < length; ++i)
    {
        if (bytes[i] == ' ')
        {
            return false;
        }
    }
    return equalsIgnoreCaseAscii(bytes, length, "Air", 3) ||
           equalsIgnoreCaseAscii(bytes, length, "Royal", 5) ||
           isAirGeneric(bytes, length);
}

// Fit `text` to `maxColumns` by dropping whole trailing words until it fits,
// so the panel never shows a mid-word fragment ("Thai Vietjet A"). Then trim a
// dangling function word, and if that reduced the label to a bare leading
// generic, hard-cut the original instead (identity is at the end there). A
// single leading word longer than the budget is hard-cut as a last resort.
String wordBoundaryTruncate(const String &text, int maxColumns)
{
    if (static_cast<int>(text.length()) <= maxColumns)
    {
        return text;
    }
    String fitted = text;
    while (static_cast<int>(fitted.length()) > maxColumns)
    {
        const char *bytes = fitted.c_str();
        int lastSpace = -1;
        for (int index = static_cast<int>(fitted.length()) - 1; index > 0; --index)
        {
            if (bytes[index] == ' ')
            {
                lastSpace = index;
                break;
            }
        }
        if (lastSpace <= 0)
        {
            break;
        }
        fitted = fitted.substring(0, static_cast<unsigned int>(lastSpace));
        fitted.trim();
    }

    for (;;)
    {
        const char *bytes = fitted.c_str();
        int wordStart = static_cast<int>(fitted.length());
        while (wordStart > 0 && bytes[wordStart - 1] != ' ')
        {
            --wordStart;
        }
        if (wordStart <= 0 ||
            !isTrailingFunctionWord(
                bytes + wordStart,
                fitted.length() - static_cast<size_t>(wordStart)))
        {
            break;
        }
        fitted = fitted.substring(0, static_cast<unsigned int>(wordStart));
        fitted.trim();
    }

    if (fitted.isEmpty() || isBareLeadingGeneric(fitted))
    {
        return text.substring(0, static_cast<unsigned int>(maxColumns));
    }
    if (static_cast<int>(fitted.length()) > maxColumns)
    {
        fitted = fitted.substring(0, static_cast<unsigned int>(maxColumns));
    }
    return fitted;
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
    String overrideLabel;
    if (matchBrandOverride(name, overrideLabel))
    {
        return overrideLabel;
    }

    String working = name;
    for (const char *phrase : AIRLINE_DROP_PHRASES_UPPER)
    {
        dropPhraseCaseInsensitive(working, phrase);
    }

    // Build two forms in one pass: `full` keeps the Air-generic word(s),
    // `stem` omits them. Rule 1 then keeps `full` when it fits (so "Iraqi
    // Airways" survives whole) and falls to `stem` only when keeping the
    // generic would overflow (so "United Airlines" -> "United", never a bare
    // "British"). Pure generics are dropped and "International" -> "Intl" as
    // before.
    const char *bytes = working.c_str();
    const size_t length = working.length();
    String full;
    String stem;
    bool hasAirGeneric = false;
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

        if (isAirGeneric(bytes + wordStart, wordLength))
        {
            hasAirGeneric = true;
            if (!full.isEmpty())
            {
                full += " ";
            }
            full += working.substring(wordStart, i);
            continue;
        }

        const WordRule *rule = matchWordRule(bytes + wordStart, wordLength);
        if (rule != nullptr && rule->replacement == nullptr)
        {
            continue;
        }
        const String token =
            (rule != nullptr) ? String(rule->replacement) : working.substring(wordStart, i);
        if (!full.isEmpty())
        {
            full += " ";
        }
        full += token;
        if (!stem.isEmpty())
        {
            stem += " ";
        }
        stem += token;
    }

    String result =
        (hasAirGeneric && static_cast<int>(full.length()) > AIRLINE_LABEL_COLUMNS)
            ? stem
            : full;

    // Only shrink a country/region qualifier when the label still overflows,
    // then drop whole trailing words as a last resort -- never a mid-word cut.
    if (static_cast<int>(result.length()) > AIRLINE_LABEL_COLUMNS)
    {
        for (const RegionAbbrev &region : REGION_ABBREVIATIONS)
        {
            replaceRegionToken(result, region.fromUpper, region.to);
        }
    }
    result = wordBoundaryTruncate(result, AIRLINE_LABEL_COLUMNS);

    if (result.isEmpty())
    {
        return wordBoundaryTruncate(firstWordOf(name), AIRLINE_LABEL_COLUMNS);
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
