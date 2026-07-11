# Nimbus Firmware

Firmware for Nimbus — a 128×64 HUB75 flight display driven by an Adafruit
MatrixPortal S3.

## Architecture

```
main.cpp
  └─ FlightDataFetcher
       ├─ AirplanesLiveFetcher   (live ADS-B state vectors)
       └─ AdsbdbFetcher          (route, airline, aircraft enrichment)
  └─ Hub75Display
       ├─ FlightText             (formatting, abbreviations, route labels)
       └─ FlightMessages         (rotating bottom-message catalog)
       └─ AirlineLogoLibrary     (embedded RGB565 logos)
```

`FlightText` and `FlightMessages` are hardware-independent: they take only
`FlightInfo` and config, so display logic stays testable without the panel.

## Data sources

- [airplanes.live](https://airplanes.live/) supplies nearby aircraft
  positions, callsigns, altitude, ground speed, vertical rate, track,
  squawk, emergency state, emitter category, registration, and aircraft type.
- [adsbdb](https://www.adsbdb.com/) supplies routes, airline details,
  airport metadata, manufacturer, registered owner, and human-readable
  aircraft names.

None of these services requires an account or API key. Color airline logos
(40×30, 502 airlines) are bundled into the firmware and looked up by IATA
code. They require no runtime logo service.

## Display

Each flight card:

- **Logo cell** (left, 40×30): airline logo by IATA code
- **Top right** (3 lines): airline name, IATA route, aircraft type
- **Bottom** (3 lines): two rotating contextual messages + fixed
  callsign/altitude

Messages rotate every `DISPLAY_CYCLE_SECONDS`. Emergency squawks pin an
`EMERGENCY` pair and stop rotation. When multiple flights are tracked, the
display cycles between them on the same interval.

## Configuration

- Copy `config/WiFiSecrets.example.h` to `config/WiFiSecrets.h`, then enter
  the 2.4 GHz network. The secrets file is gitignored.
- Set latitude, longitude, search radius, brightness, commercial-only filter,
  and maximum displayed flights in `config/UserConfiguration.h`.
- Polling and display-cycle intervals are in `config/TimingConfiguration.h`.
- HUB75 dimensions and MatrixPortal S3 pins are in
  `config/HardwareConfiguration.h`.

Keep `DISPLAY_BRIGHTNESS` low while the panel is powered only over USB.
Use the panel's 5 V supply for normal operation.

## Build and upload

1. Open this `firmware` directory as a PlatformIO project.
2. Connect the MatrixPortal S3 over USB-C.
3. Build or run **Upload** for the `adafruit_matrixportal_esp32s3`
   environment.
4. Open the serial monitor at 115200 baud for network and API diagnostics.

The 64-row panel needs the HUB75 E address line. Set the MatrixPortal S3
E-line jumper to the connector pin used by the panel if only half the rows
appear.

### Partition table

The bundled logo library pushed the firmware past the stock 2 MB app slot.
`partitions_custom.csv` merges the two OTA slots into a single 4 MB app
partition while keeping the TinyUF2 `uf2` and `ffat` partitions at their
original offsets so drag-and-drop UF2 flashing still works. OTA updates are
not used — the board is flashed over USB.

## Regenerating airline logos

The generated RGB565 logo library lives in `assets/`. To rebuild it from the
pinned, licensed sources:

```bash
uv run --with cairosvg --with pillow python tools/generate_airline_logos.py
```
