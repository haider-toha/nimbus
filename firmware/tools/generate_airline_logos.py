"""Generate the bundled RGB565 airline logo library.

Run from the firmware directory:
    uv run --with cairosvg --with pillow python tools/generate_airline_logos.py
"""

from __future__ import annotations

import io
import json
import struct
import urllib.request
from pathlib import Path

import cairosvg
from PIL import Image


SOURCE_REPOSITORY = "amannuhman/airlines-and-logos"
SOURCE_REVISION = "f90a5a8e1481dd0e0379936e4a5b477f4cf9f60e"
SOURCE_ROOT = (
    f"https://raw.githubusercontent.com/{SOURCE_REPOSITORY}/"
    f"{SOURCE_REVISION}/data"
)
CURATED_SOURCE_ROOT = (
    "https://raw.githubusercontent.com/anhthang/soaring-symbols/"
    "2e97859ed3e7aba051d5696ff59c97c3de36823b"
)

LOGO_WIDTH = 36
LOGO_HEIGHT = 24
PIXELS_PER_LOGO = LOGO_WIDTH * LOGO_HEIGHT
BYTES_PER_LOGO = PIXELS_PER_LOGO * 2

PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRECTORY = PROJECT_ROOT / "assets"
BINARY_PATH = ASSET_DIRECTORY / "airline_logos.bin"
INDEX_HEADER_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.h"
INDEX_SOURCE_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.cpp"


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Nimbus-logo-builder"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def render_logo(svg: bytes) -> Image.Image:
    png = cairosvg.svg2png(bytestring=svg, unsafe=True)
    logo = Image.open(io.BytesIO(png)).convert("RGBA")
    logo.thumbnail((LOGO_WIDTH, LOGO_HEIGHT), Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (LOGO_WIDTH, LOGO_HEIGHT), (0, 0, 0, 255))
    position = ((LOGO_WIDTH - logo.width) // 2, (LOGO_HEIGHT - logo.height) // 2)
    canvas.alpha_composite(logo, position)
    return canvas.convert("RGB")


def rgb565_bytes(image: Image.Image) -> bytes:
    encoded = bytearray()
    for red, green, blue in image.getdata():
        pixel = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        encoded.extend(struct.pack("<H", pixel))
    return bytes(encoded)


def write_index(entries: list[tuple[str, int]]) -> None:
    INDEX_HEADER_PATH.write_text(
        f"""#pragma once

#include <Arduino.h>

namespace AirlineLogoLibrary
{{
    static constexpr uint8_t LOGO_WIDTH = {LOGO_WIDTH};
    static constexpr uint8_t LOGO_HEIGHT = {LOGO_HEIGHT};

    const uint16_t *findByIata(const String &iata);
}}
""",
        encoding="utf-8",
    )

    rows = "\n".join(
        f'    {{"{iata}", {offset}UL}},' for iata, offset in entries
    )
    INDEX_SOURCE_PATH.write_text(
        f"""#include "assets/AirlineLogoLibrary.h"

#include <string.h>

namespace
{{
struct LogoIndexEntry
{{
    const char *iata;
    uint32_t offset;
}};

const LogoIndexEntry LOGO_INDEX[] = {{
{rows}
}};

extern const uint8_t LOGO_DATA_START[]
    asm("_binary_assets_airline_logos_bin_start");
}}

const uint16_t *AirlineLogoLibrary::findByIata(const String &iata)
{{
    if (iata.length() != 2)
    {{
        return nullptr;
    }}

    String normalized = iata;
    normalized.toUpperCase();

    int left = 0;
    int right = static_cast<int>(
        sizeof(LOGO_INDEX) / sizeof(LOGO_INDEX[0])) - 1;
    while (left <= right)
    {{
        const int middle = left + (right - left) / 2;
        const int comparison = strcmp(
            normalized.c_str(),
            LOGO_INDEX[middle].iata);
        if (comparison == 0)
        {{
            return reinterpret_cast<const uint16_t *>(
                LOGO_DATA_START + LOGO_INDEX[middle].offset);
        }}
        if (comparison < 0)
        {{
            right = middle - 1;
        }}
        else
        {{
            left = middle + 1;
        }}
    }}
    return nullptr;
}}
""",
        encoding="utf-8",
    )


def main() -> None:
    ASSET_DIRECTORY.mkdir(parents=True, exist_ok=True)
    airlines = json.loads(download(f"{SOURCE_ROOT}/airlines.json"))
    curated_airlines = json.loads(
        download(f"{CURATED_SOURCE_ROOT}/airlines.json")
    )
    curated_slugs = {
        airline["iata"].upper(): airline["slug"]
        for airline in curated_airlines
        if isinstance(airline.get("iata"), str)
        and len(airline["iata"]) == 2
        and isinstance(airline.get("slug"), str)
    }
    airlines_by_iata = {
        airline["iata"].upper(): airline
        for airline in airlines
        if isinstance(airline.get("iata"), str) and len(airline["iata"]) == 2
    }

    entries: list[tuple[str, int]] = []
    logo_data = bytearray()
    failures: list[str] = []
    for iata in sorted(airlines_by_iata):
        try:
            svg = None
            if iata in curated_slugs:
                try:
                    svg = download(
                        f"{CURATED_SOURCE_ROOT}/assets/"
                        f"{curated_slugs[iata]}/icon.svg"
                    )
                except Exception:
                    pass
            if svg is None:
                svg = download(f"{SOURCE_ROOT}/logos/{iata}.svg")
            encoded = rgb565_bytes(render_logo(svg))
            if len(encoded) != BYTES_PER_LOGO:
                raise ValueError(f"unexpected encoded size {len(encoded)}")
            entries.append((iata, len(logo_data)))
            logo_data.extend(encoded)
            print(f"generated {iata}")
        except Exception as error:
            failures.append(f"{iata}: {error}")

    BINARY_PATH.write_bytes(logo_data)
    write_index(entries)

    print(
        f"wrote {len(entries)} logos, {len(logo_data)} bytes "
        f"to {BINARY_PATH.relative_to(PROJECT_ROOT)}"
    )
    if failures:
        print(f"skipped {len(failures)} logos:")
        print("\n".join(failures))


if __name__ == "__main__":
    main()
