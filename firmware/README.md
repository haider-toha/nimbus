# Nimbus Firmware

Firmware for Nimbus — a 128×64 HUB75 flight display driven by an Adafruit
MatrixPortal S3.

## Data sources

- [airplanes.live](https://airplanes.live/) supplies nearby aircraft
  positions, callsigns, altitude, speed and heading.
- [adsbdb](https://www.adsbdb.com/) supplies routes, airline details and
  human-readable aircraft metadata.

None of these services requires an account or API key.
Color airline logos are bundled into the firmware and looked up by the
airline's IATA code. They require no runtime logo service.

## Configuration

- Copy `config/WiFiSecrets.example.h` to `config/WiFiSecrets.h`, then enter
  the 2.4 GHz network. The secrets file is gitignored.
- Set latitude, longitude, search radius, brightness and maximum displayed
  flights in `config/UserConfiguration.h`.
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

## Regenerating airline logos

The generated RGB565 logo library lives in `assets/`. To rebuild it from the
pinned, licensed sources:

```powershell
uv run --with cairosvg --with pillow python tools/generate_airline_logos.py
```
