# Nimbus

A DIY LED flight tracker for your wall. Live aircraft overhead on a single
128×64 HUB75 panel driven by an Adafruit MatrixPortal S3 — free data, no
accounts, near-zero soldering.

**Status:** first light works on hardware. Nearby airplanes.live traffic
renders on the panel. Route/operator enrichment, flicker diagnosis, adaptive
sparse-card layout, desktop simulator, and enclosure CAD are still open.
See [plan/flight-tracker-handoff-v3.md](plan/flight-tracker-handoff-v3.md) for
the running handoff.

## What it shows

Each flight card:

- **Left:** a 40×30 color airline logo (502 airlines bundled in firmware,
  looked up by IATA code)
- **Top right:** airline name, IATA route (e.g. `LHR-JFK`), and aircraft type
- **Bottom:** two message lines that rotate every few seconds, plus a fixed
  callsign/altitude line at the very bottom

Rotating messages use whatever fields are available — route cities,
arrival/departure context, altitude, climb/descent, speed, distance and
bearing, registration, callsign, squawk, and more. Emergency squawks
(7500/7600/7700) pin an `EMERGENCY` pair and stop rotation.

When several aircraft are in range, the display cycles between them on the
same interval. With `REQUIRE_AIRLINE_LOGO` enabled (the default), only
callsigns whose airline prefix maps to a bundled logo are tracked.

## Components

- One 128×64 HUB75E RGB panel with an FM6124 driver (e.g. K580-32S-128×64)
- Adafruit MatrixPortal S3
- Regulated 5 V / 8 A panel supply (DC5525 → panel power header)
- USB-C cable for programming
- [airplanes.live](https://airplanes.live/) for nearby aircraft positions
- [adsbdb](https://www.adsbdb.com/) for route, airline and aircraft data

## Hardware

Connect the MatrixPortal S3 to the panel's HUB75 **input** (J1). Connect the
5 V supply directly to the panel: **red → +5 V, black → GND**. Tie panel and
MatrixPortal grounds together. Do not feed external power into the
MatrixPortal screw terminals, and do not run a bright panel on USB alone —
USB bench tests need low brightness (~20–40 of 255).

The 64-row panel needs the HUB75 E address line. If only half the rows
appear, check the MatrixPortal S3 E-line jumper and ribbon orientation.

## Data

No API keys, accounts, or payment cards:

1. **airplanes.live** — position, callsign, altitude, ground speed, vertical
   rate, track, squawk, emergency, emitter category, registration, type,
   distance and bearing (≤1 req/sec; radius converted km→nm internally)
2. **adsbdb** — route, airline, airports, manufacturer, owner, and
   human-readable aircraft names (404-safe when unknown)

Color airline logos are bundled into the firmware — no runtime CDN.

WiFi reconnects before each fetch. If the network is down, the device skips
the fetch and keeps showing the last known flights.

## Configuration

Copy `firmware/config/WiFiSecrets.example.h` to `WiFiSecrets.h` and enter a
**2.4 GHz** network locally (ESP32-S3 cannot see 5 GHz-only hotspots). The
secrets file is gitignored.

Update [UserConfiguration.h](firmware/config/UserConfiguration.h):

| Setting | Purpose |
| --- | --- |
| `CENTER_LAT` / `CENTER_LON` | Tracking center (defaults to central London) |
| `RADIUS_KM` | Search radius in kilometers (defaults to 100 km) |
| `MAX_TRACKED_FLIGHTS` | Nearest aircraft to enrich and display |
| `REQUIRE_AIRLINE_LOGO` | Show only callsigns mapped to a bundled airline logo |
| `DISPLAY_BRIGHTNESS` | Panel brightness 0–255; use 20–40 for USB bench tests |
| `TEXT_COLOR_R/G/B` | Text color on the LED matrix |

Polling and display-cycle intervals are in
[TimingConfiguration.h](firmware/config/TimingConfiguration.h) (default: fetch
every 30 s, rotate messages every 3 s).

## Project layout

```
firmware/
  src/main.cpp              # setup/loop, WiFi, fetch and display orchestration
  adapters/                 # airplanes.live, adsbdb, and HUB75 display drivers
  core/                     # FlightDataFetcher, FlightText, FlightMessages
  models/                   # FlightInfo, StateVector, AirportInfo
  assets/                   # bundled airline logo library (RGB565)
  config/                   # WiFi, location, timing, and hardware settings
  tools/                    # logo generation + host integrity tests
plan/
  flight-tracker-handoff-v3.md   # running status and next actions
```

## Build and upload

1. Install the PlatformIO extension in Cursor or VS Code.
2. Open the `firmware` directory as the PlatformIO project.
3. Connect the MatrixPortal S3 over USB-C.
4. Build or upload the `adafruit_matrixportal_esp32s3` environment.
5. Open the serial monitor at 115200 baud for diagnostics.

The firmware image uses a custom 4 MB app partition to fit the bundled logo
library while keeping TinyUF2 drag-and-drop flashing. See
[firmware/README.md](firmware/README.md) for firmware details, partition
layout, and logo regeneration.

## What's next

Tracked in the [handoff plan](plan/flight-tracker-handoff-v3.md):

- Confirm adsbdb enrichment + logo on a known commercial flight
- Diagnose flicker (power vs camera vs Protomatter refresh)
- Adaptive layout when route/logo data is missing
- Desktop 128×64 simulator and fixture screenshots
- Enclosure CAD after measuring the panel once

## Credits

Nimbus began as a heavily modified derivative of
[TheFlightWall OSS](https://github.com/AxisNimble/TheFlightWall_OSS)
(Apache-2.0), rebuilt for HUB75 / MatrixPortal S3 hardware and a free,
no-account data stack.

See [third-party notices](THIRD_PARTY_NOTICES.md) for airline-logo provenance.
