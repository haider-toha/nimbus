#pragma once

#include <vector>
#include <Arduino.h>
#include "models/FlightInfo.h"

// Rotating bottom-message catalog for the flight card (WS3 step 3b).
//
// Pure text-derivation, no GFX/hardware dependency -- mirrors FlightText.h's
// design so this is unit-testable and reusable by the host simulator's QA
// harness with zero drift from what Hub75Display actually renders (the
// harness calls FlightMessages::build() directly, exactly like it already
// does for FlightText::airlineLabel / formatContextAirport).
struct FlightMessage
{
    // Stable, machine-readable tag (e.g. "altitude", "emergency") -- never
    // shown on the display. Used by the QA harness/metrics and by
    // Hub75Display to detect the emergency pair. Compare with strcmp (or
    // build a std::string/String from it), not `==` -- it is a `const char*`
    // and may point at a different translation unit's string literal.
    const char *type = nullptr;
    // Already FlightText::asciiFold()-ed and already <=21 columns -- callers
    // (Hub75Display) do not need to re-clean-cut this text, though doing so
    // anyway is harmless defense-in-depth.
    String text;

    // Explicit constructors: a default member initializer (`type = nullptr`)
    // makes this a non-aggregate under -std=gnu++11 (the ESP32 Arduino core's
    // default), so `FlightMessage{type, text}` brace-init can't select an
    // aggregate and needs a real 2-arg constructor. (The host simulator builds
    // at -std=c++17, where relaxed aggregate rules let it compile without this
    // -- caught only by the on-device build.)
    FlightMessage() = default;
    FlightMessage(const char *messageType, const String &messageText)
        : type(messageType), text(messageText) {}
};

namespace FlightMessages
{
    // Builds every AVAILABLE rotating message for `flight`: a message whose
    // source field(s) are absent is skipped (never rendered blank/wrong),
    // and a candidate that would exceed 21 columns is either replaced with a
    // documented shorter fallback or dropped -- never truncated/ellipsized.
    //
    // If an emergency is active (squawk 7500/7600/7700, or a non-empty,
    // non-"none" `emergency` field), the "emergency" message is index 0 and
    // "emergency_detail" is index 1, ahead of every other message. Callers
    // that need to special-case the emergency pair should check
    // `strcmp(result[0].type, "emergency") == 0` rather than re-deriving the
    // guard from FlightInfo.
    std::vector<FlightMessage> build(const FlightInfo &flight);
}
