#pragma once

#include <Arduino.h>
#include "models/FlightInfo.h"

// Pure text-derivation helpers for the flight card display.
//
// No GFX/hardware dependency (no Adafruit_Protomatter, no _matrix) -- these
// take only FlightInfo/AirportInfo, config, and math, so they are
// unit-testable and reusable by the host simulator's QA harness. The harness
// reuses them indirectly: Hub75Display calls into FlightText, and the
// harness renders through the real Hub75Display, so its metrics reflect
// this code with zero drift.
namespace FlightText
{
    String truncateToColumns(const String &text, int maxColumns);
    // Hard-cuts at maxColumns with NO ellipsis -- distinct from
    // truncateToColumns, which reserves 3 columns for "...". Used where the
    // abbreviation dictionary is expected to already have done the shortening
    // and any residual overflow should just be cut clean.
    String cleanCut(const String &text, int maxColumns);
    // Folds accented Latin (UTF-8 Latin-1 Supplement) to plain ASCII at the
    // byte level so the classic bitmap font can render it legibly. Any other
    // non-ASCII byte sequence is dropped rather than left as a raw high byte.
    String asciiFold(const String &text);
    // Applies the airline abbreviation dictionary to an ALREADY ascii-folded
    // name: drops generic phrases/words (Airlines, Airways, Company, ...),
    // abbreviates long qualifiers (International -> Intl, ...), and falls
    // back to the original first word if every word was dropped.
    String abbreviateAirline(const String &name);
    // Single source of truth for the top airline line: selects the name
    // (airline_display_name_full, else operator_iata, else operator_icao),
    // ascii-folds it, and abbreviates it. NOT yet cut to display width --
    // callers (display + QA harness) both derive from this so metrics match
    // the real render with zero drift.
    String airlineLabel(const FlightInfo &flight);
    String formatAltitude(const FlightInfo &flight);
    String formatRoute(const FlightInfo &flight);
    String formatAirportName(const AirportInfo &airport);
    String formatContextLabel(const FlightInfo &flight);
    // City-first context-airport label: municipality, else formatAirportName
    // (name with Intl abbreviation), else the IATA code -- ascii-folded.
    String formatContextAirport(const FlightInfo &flight);
    double distanceFromCenterSquared(const AirportInfo &airport);
}
