# Nimbus

A LED flight tracker for your wall. Shows live info for planes overhead on a
single 128×64 HUB75 panel driven by an Adafruit MatrixPortal S3.

## What it shows

Each flight card is split into a fixed top section and a rotating bottom
section:

- **Left:** a 40×30 color airline logo (502 airlines bundled in firmware,
  looked up by IATA code)
- **Top right:** airline name, IATA route (e.g. `LHR-JFK`), and aircraft type
- **Bottom:** two message lines that rotate every few seconds, plus a fixed
  callsign/altitude line at the very bottom

The rotating messages are built from whatever data is available for that
flight — route cities, arrival/departure context, altitude, climb/descent,
speed, distance and bearing, registration, callsign, squawk, and more.
Emergency squawks (7500/7600/7700) pin an `EMERGENCY` message pair and
override rotation.

When several aircraft are in range, the display cycles between them on the
same interval. With `COMMERCIAL_FLIGHTS_ONLY` enabled (the default), only
large and heavy jets are tracked so the panel focuses on airline traffic.

## Components

- One 128×64 HUB75E RGB panel with an FM6124 driver
- Adafruit MatrixPortal S3
- Regulated 5 V panel power supply
- USB-C cable for programming
- [airplanes.live](https://airplanes.live/) for nearby aircraft positions
- [adsbdb](https://www.adsbdb.com/) for route, airline and aircraft data

## Hardware

Connect the MatrixPortal S3 to the panel's HUB75 **input** connector. Connect
the panel supply directly to the panel: red to +5 V and black to ground.
Do not power a bright panel through USB alone.

The 64-row panel requires the HUB75 E address line. If only half the rows
appear, verify the MatrixPortal S3 E-line jumper position and ribbon-cable
orientation.

## Data

The firmware uses free, no-account endpoints:

1. **airplanes.live** — position, callsign, altitude, ground speed, vertical
   rate, track, squawk, emergency state, emitter category, registration, and
   aircraft type description
2. **adsbdb** — route, airline metadata, airport details, aircraft
   manufacturer, registered owner, and human-readable aircraft names

No API keys, accounts or payment cards are required. Color airline logos are
bundled into the firmware and require no runtime CDN or logo service.

WiFi reconnects automatically before each fetch. If the network is down, the
device skips the fetch and keeps showing the last known flights.

## Configuration

Copy `firmware/config/WiFiSecrets.example.h` to `WiFiSecrets.h` and enter the
2.4 GHz network locally. The secrets file is gitignored. ESP32-S3 cannot
connect to a 5 GHz-only hotspot.

Update [UserConfiguration.h](firmware/config/UserConfiguration.h):

| Setting | Purpose |
| --- | --- |
| `CENTER_LAT` / `CENTER_LON` | Tracking center (defaults to central London) |
| `RADIUS_KM` | Search radius in kilometers |
| `MAX_TRACKED_FLIGHTS` | Nearest aircraft to enrich and display |
| `COMMERCIAL_FLIGHTS_ONLY` | Skip light aircraft, helicopters, drones, etc. |
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
  tools/                    # logo generation script
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

## Credits

Nimbus began as a heavily modified derivative of
[TheFlightWall OSS](https://github.com/AxisNimble/TheFlightWall_OSS)
(Apache-2.0), rebuilt for HUB75 / MatrixPortal S3 hardware and a free,
no-account data stack.

See [third-party notices](THIRD_PARTY_NOTICES.md) for airline-logo provenance.
