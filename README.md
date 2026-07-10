# Nimbus

A LED flight tracker for your wall. Shows live info for planes
overhead on a single 128×64 HUB75 panel driven by an Adafruit MatrixPortal S3.

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

1. airplanes.live for position, callsign and altitude data
2. adsbdb for route, airline and human-readable aircraft metadata

No API keys, accounts or payment cards are required.
Nimbus bundles an optimized color logo library for 500+ airlines, so logos
do not require another API or CDN at runtime.

Each flight card shows the airline logo and name, IATA route, aircraft type,
London-aware arrival/departure context, airport name, callsign and altitude.

## Configuration

Copy `firmware/config/WiFiSecrets.example.h` to `WiFiSecrets.h` and enter the
2.4 GHz network locally. The secrets file is gitignored. ESP32-S3 cannot
connect to a 5 GHz-only hotspot.

Update [UserConfiguration.h](firmware/config/UserConfiguration.h):

- `CENTER_LAT`: latitude of the tracking center
- `CENTER_LON`: longitude of the tracking center
- `RADIUS_KM`: search radius in kilometers
- `MAX_TRACKED_FLIGHTS`: maximum nearest aircraft to enrich and display
- `DISPLAY_BRIGHTNESS`: use 20–40 for a low-brightness bench test

## Build and upload

1. Install the PlatformIO extension in Cursor or VS Code.
2. Open the `firmware` directory as the PlatformIO project.
3. Connect the MatrixPortal S3 over USB-C.
4. Build or upload the `adafruit_matrixportal_esp32s3` environment.
5. Open the serial monitor at 115200 baud for diagnostics.

See [firmware/README.md](firmware/README.md) for firmware details.

## Credits

Nimbus began as a heavily modified derivative of
[TheFlightWall OSS](https://github.com/AxisNimble/TheFlightWall_OSS)
(Apache-2.0), rebuilt for HUB75 / MatrixPortal S3 hardware and a free,
no-account data stack.

See [third-party notices](THIRD_PARTY_NOTICES.md) for airline-logo provenance.
