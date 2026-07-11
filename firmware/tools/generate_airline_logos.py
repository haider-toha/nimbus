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

LOGO_WIDTH = 40
LOGO_HEIGHT = 30
PIXELS_PER_LOGO = LOGO_WIDTH * LOGO_HEIGHT
BYTES_PER_LOGO = PIXELS_PER_LOGO * 2

# Hub75Display renders at UserConfiguration::DISPLAY_BRIGHTNESS = 32 (out of
# 255): every stored RGB565 channel is decoded then scaled by ~32/255 before
# it reaches the panel, so a source pixel whose peak channel is much below
# this floor decodes to 0 on real hardware.
#
# 64 (the floor a simplified post-composite probe found sufficient) turns
# out to rescue 7G to only ~2 visible pixels in this alpha-aware
# implementation: 7G's brand color is near-black AND its glyph is thin/
# detailed enough that downsampling to this cell leaves almost no pixel
# fully opaque, so most of its content is doubly dimmed (dark color, low
# alpha) and a low floor barely moves it. Swept 64/96/128/160/200 against
# the broken set (NZ/7G/HD/PQ) and a diverse sample of already-fine, richly
# colored airlines (BA/AA/DL/UA/LH/SQ/EK/QF/AF/KE/JL/CX/QR/EY/TK/LX/BR/VS/
# AC/NH/KL/IB): non_black_px never changes with floor (it only redistributes
# existing content upward, never fabricates new content), and a rendered
# side-by-side of the two most floor-sensitive fine logos (LH's dark-navy
# tail, AA's navy wordmark) is visually indistinguishable across the whole
# range -- the growth in "runtime-visible" count there is anti-aliased edge
# pixels crossing the display's own threshold, not a hue/identity shift.
# 160 sits comfortably below that confirmed-safe range while giving 7G a
# real (not token) rescue: 40/92 px cross the visibility threshold, versus 2
# at floor=64.
CONTENT_BRIGHTNESS_FLOOR = 160

PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRECTORY = PROJECT_ROOT / "assets"
BINARY_PATH = ASSET_DIRECTORY / "airline_logos.bin"
INDEX_HEADER_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.h"
INDEX_SOURCE_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.cpp"


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Nimbus-logo-builder"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def _crop_to_content(image: Image.Image) -> Image.Image:
    """Crop an RGBA image to its opaque content, discarding transparent padding.

    Source SVGs often carry a viewBox with generous whitespace around the
    actual glyph. Thumbnailing the full viewBox (padding included) shrinks
    the effective logo well below the target cell; cropping to content
    first means .thumbnail() scales the real glyph to fill the cell.
    """
    bbox = image.getbbox(alpha_only=True)
    if bbox is None:
        return image
    return image.crop(bbox)


def _apply_brightness_floor(image: Image.Image, floor: int) -> Image.Image:
    """Lift dim-but-opaque pixels so they survive the device's brightness scaling.

    Only visible pixels (alpha > 0) are considered, and only their peak
    channel drives the lift, so hue/ratio is preserved -- this brightens
    genuinely dark brand colors (navy, dark red, ...) without touching
    fully-transparent background pixels or already-bright ones. Pixels
    whose peak channel is exactly 0 (pure black) are left untouched: there
    is no color ratio to scale, and forcing them to grey would mask logos
    that are entirely black-on-transparent at the source (e.g. NZ's curated
    artwork) -- those need main()'s content-validation fallback to a
    different source, not a brightness hack.
    """
    pixels = image.load()
    width, height = image.size
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            if alpha == 0:
                continue
            peak = max(red, green, blue)
            if peak == 0 or peak >= floor:
                continue
            scale = floor / peak
            pixels[x, y] = (
                min(255, round(red * scale)),
                min(255, round(green * scale)),
                min(255, round(blue * scale)),
                alpha,
            )
    return image


def render_logo(svg: bytes) -> Image.Image:
    png = cairosvg.svg2png(bytestring=svg, unsafe=True)
    logo = Image.open(io.BytesIO(png)).convert("RGBA")
    logo = _crop_to_content(logo)
    logo.thumbnail((LOGO_WIDTH, LOGO_HEIGHT), Image.Resampling.LANCZOS)
    logo = _apply_brightness_floor(logo, CONTENT_BRIGHTNESS_FLOOR)

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
            used_curated = False
            if iata in curated_slugs:
                try:
                    svg = download(
                        f"{CURATED_SOURCE_ROOT}/assets/"
                        f"{curated_slugs[iata]}/icon.svg"
                    )
                    used_curated = True
                except Exception:
                    pass
            if svg is None:
                svg = download(f"{SOURCE_ROOT}/logos/{iata}.svg")

            canvas = render_logo(svg)
            if canvas.getbbox() is None and used_curated:
                # Curated source produced no visible content (e.g. a
                # monochrome glyph that composites black-on-black, as with
                # NZ) -- retry with the primary source, which carries
                # independent brand-color artwork and isn't subject to the
                # same failure.
                svg = download(f"{SOURCE_ROOT}/logos/{iata}.svg")
                canvas = render_logo(svg)

            encoded = rgb565_bytes(canvas)
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
