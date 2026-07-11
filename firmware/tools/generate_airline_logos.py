"""Generate the bundled RGB565 airline logo library.

Run from the firmware directory:
    uv run --with cairosvg --with pillow python tools/generate_airline_logos.py
"""

from __future__ import annotations

import io
import json
import struct
import time
import urllib.error
import urllib.request
from pathlib import Path

import cairosvg
from PIL import Image, ImageDraw, ImageFont


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
EXPECTED_LOGO_COUNT = 502
RASTER_SCALE = 4.0
DISPLAY_BRIGHTNESS = 32
MIN_RUNTIME_VISIBLE_PIXELS = 12
VISIBILITY_BRIGHTNESS_FLOOR = 160

# These entries exist in the curated manifest but do not have a curated SVG
# at the pinned revision. NZ is also explicit here because its curated icon
# is black-on-transparent and cannot be represented when RGB565 zero is the
# firmware's transparency key. Keeping this list explicit means a transient
# curated download failure aborts generation instead of silently changing
# the selected artwork.
PRIMARY_SOURCE_IATAS = frozenset(
    {
        "AK",
        "AS",
        "B6",
        "BI",
        "BT",
        "EY",
        "MM",
        "NH",
        "NZ",
        "QF",
        "SK",
        "TR",
        "TW",
        "VJ",
        "W6",
        "ZD",
    }
)

# Wide "emblem + wordmark" lockups whose full art content-fits to an unreadable
# horizontal sliver. For these, crop to the leading square block (side = content
# height) -- the brand emblem (roundel / bird / temple / crest / checkerboard) --
# so it fills the 40x30 cell instead of a 3px line. Each entry was verified
# against its rendered emblem, not guessed -- see
# audit/display-iteration/07-phase3b-logo-fixes.md. OU is the checkerboard
# (82 px/40x3 -> 493 px/33x30); the rest are CI plum-blossom, FG Ariana bird,
# GV compass-star, 8U Afriqiyah chevrons, HX orchid, IE Solomon crest, K6/KR
# temples, OQ swirl, PB chevron, 6Y star, LN ring, CL Lufthansa crane, N4 swoosh.
LEADING_EMBLEM_CROP_IATAS = frozenset({
    "OU", "CI", "FG", "GV", "8U", "HX", "IE", "K6", "KR", "OQ", "PB", "6Y", "LN", "CL", "N4",
    # micro-glyph marks whose leading-square crop is the real brand symbol:
    "AE",  # Mandarin Airlines stylized mark (98 -> 529 px)
    "DJ",  # Star Air flying-bird mark (98 -> 419 px)
    "CV",  # Air Chathams three-shape mark (129 -> 514 px)
})

# Faint/dark source art that content-fits to too few visible pixels to read at
# the device's brightness-32 decode. Unlike the near-blank auto-rescue (which
# triggers only below MIN_RUNTIME_VISIBLE_PIXELS), these render 40-230 px but are
# still unreadable; force the hue-preserving brightness floor. Each was verified
# to become legible once lifted (see audit/display-iteration/07-phase3b-logo-fixes.md).
# CL/LN are in both sets: emblem-crop then brighten the dark emblem.
BRIGHTNESS_BOOST_IATAS = frozenset({
    "AA", "5A", "8P", "F8", "EZ", "NK", "NZ", "UU", "J3", "ZG", "ZH", "UR",
    "HD", "SF", "YX", "B9", "CJ", "K4", "5R", "D2", "CL", "LN",
})

# Reassigned-IATA carriers whose upstream art is a DIFFERENT (usually defunct)
# airline, and for which no free source carries the correct current art (checked:
# the alternate IATA datasets ship the same stale art or a placeholder). Rather
# than show the wrong brand, render the carrier's name set in its approximate
# brand colour -- a legible, non-infringing identification tile (plain text, NOT
# a reproduction of the airline's logo artwork), matching what the card labels
# the flight. Value is (text, RGB); "\n" splits lines. Each verified to render
# legibly at 40x30. See audit/display-iteration/07-phase3c-wrong-brand-logos.md.
MANUAL_LOGO_LABELS = {
    "OV": ("salam", (0, 166, 166)),       # SalamAir (was Estonian Air)
    "JA": ("Jet\nSMART", (228, 0, 43)),   # JetSMART (was BH Airlines)
    "9P": ("Fly\nJinnah", (200, 16, 46)), # Fly Jinnah (was Air Arabia art)
    "ZT": ("TITAN", (40, 60, 120)),       # Titan Airways (was Eastar Jet)
    "AN": ("ADV\nAIR", (43, 108, 176)),   # Advanced Air (was HOP! art)
    "CO": ("Cont'l", (16, 54, 125)),      # Continental (upstream name; was Cobalt art)
    "G7": ("GoJet", (31, 78, 156)),       # GoJet Airlines (was KAPO art)
    "EG": ("Aer\nLingus", (0, 132, 61)),  # Aer Lingus UK (was Ellinair art)
    "AL": ("TAE", (150, 158, 166)),       # TAE Avia (was Malta Air art)
    "BF": ("Aero", (150, 158, 166)),      # Aero-Service (was frenchbee art)
    "FN": ("RAL", (150, 158, 166)),       # Regional Air Lines (was fastjet art)
    # micro-glyph carriers whose source mark won't isolate to a clean emblem:
    "JY": ("inter\nCarib", (0, 150, 160)),  # InterCaribbean Airways
    "TJ": ("Trade\nwind", (30, 60, 110)),   # Tradewind Aviation
    # faint wordmarks brightness can't rescue and with no emblem to crop:
    "4B": ("Boutiq\nAir", (40, 80, 150)),   # Boutique Air
    "7G": ("STAR\nFLYER", (220, 220, 225)), # Star Flyer (real logo is black -> unusable on a black panel)
}

# Bold-font search path for the manual text tiles, first hit wins; falls back to
# Pillow's built-in bitmap font so generation never hard-depends on a system font.
FONT_CANDIDATES = (
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/Library/Fonts/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
)

PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRECTORY = PROJECT_ROOT / "assets"
BINARY_PATH = ASSET_DIRECTORY / "airline_logos.bin"
INDEX_HEADER_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.h"
INDEX_SOURCE_PATH = ASSET_DIRECTORY / "AirlineLogoLibrary.cpp"


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "Nimbus-logo-builder"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.read()
        except (OSError, urllib.error.URLError):
            if attempt == 3:
                raise
            time.sleep(2**attempt)
    raise RuntimeError("unreachable download retry state")


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


def _crop_leading_square(image: Image.Image) -> Image.Image:
    """Crop a wide emblem+wordmark lockup to its leading square (the emblem).

    Used only for LEADING_EMBLEM_CROP_IATAS: the source art is a compact brand
    emblem followed by a long wordmark, so fitting the whole thing yields an
    unreadable sliver. The emblem is the leading square block (side = content
    height); isolating it lets the emblem fill the cell. Assumes the image has
    already been cropped to content.
    """
    width, height = image.size
    side = min(width, height)
    return image.crop((0, 0, side, height))


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


def _fit_to_logo_cell(image: Image.Image) -> Image.Image:
    """Resize content to fill one cell while preserving its aspect ratio."""
    width, height = image.size
    if width == 0 or height == 0:
        return image
    scale = min(LOGO_WIDTH / width, LOGO_HEIGHT / height)
    target_size = (
        max(1, round(width * scale)),
        max(1, round(height * scale)),
    )
    return image.resize(target_size, Image.Resampling.LANCZOS)


def _composite_logo(logo: Image.Image) -> Image.Image:
    canvas = Image.new("RGBA", (LOGO_WIDTH, LOGO_HEIGHT), (0, 0, 0, 255))
    position = ((LOGO_WIDTH - logo.width) // 2, (LOGO_HEIGHT - logo.height) // 2)
    canvas.alpha_composite(logo, position)
    return canvas.convert("RGB")


def _scale_rgb565_channel(channel: int, brightness: int) -> int:
    return (channel * brightness + 127) // 255


def _runtime_pixel(pixel: int) -> int:
    red = _scale_rgb565_channel((pixel >> 11) & 0x1F, DISPLAY_BRIGHTNESS)
    green = _scale_rgb565_channel((pixel >> 5) & 0x3F, DISPLAY_BRIGHTNESS)
    blue = _scale_rgb565_channel(pixel & 0x1F, DISPLAY_BRIGHTNESS)
    return (red << 11) | (green << 5) | blue


def _runtime_visible_pixels(image: Image.Image) -> int:
    encoded = rgb565_bytes(image)
    pixels = struct.iter_unpack("<H", encoded)
    return sum(_runtime_pixel(pixel[0]) != 0 for pixel in pixels)


def _load_bold_font(size: int) -> ImageFont.ImageFont:
    """First available bold font at `size`, else Pillow's built-in bitmap font."""
    for path in FONT_CANDIDATES:
        if Path(path).exists():
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def render_text_logo(text: str, color: tuple[int, int, int]) -> tuple[Image.Image, bool]:
    """Render a plain-text brand wordmark identification tile to a logo cell.

    For MANUAL_LOGO_LABELS carriers only: draws the airline's name in its
    approximate brand colour, centred and scaled to fill the 40x30 cell. This is
    a legible identifier, not a reproduction of the airline's logo artwork.
    Newlines split the text into stacked, centred lines.
    """
    lines = text.split("\n")
    font = _load_bold_font(120)
    canvas = Image.new("RGBA", (1200, 600), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)

    heights: list[int] = []
    for line in lines:
        box = draw.textbbox((0, 0), line, font=font)
        heights.append(box[3] - box[1])
    line_gap = 20
    total_height = sum(heights) + line_gap * (len(lines) - 1)

    top = (canvas.height - total_height) // 2
    for line, height in zip(lines, heights):
        box = draw.textbbox((0, 0), line, font=font)
        width = box[2] - box[0]
        draw.text(
            ((canvas.width - width) // 2 - box[0], top - box[1]),
            line,
            font=font,
            fill=color + (255,),
        )
        top += height + line_gap

    logo = _fit_to_logo_cell(_crop_to_content(canvas))
    composited = _composite_logo(logo)
    if _runtime_visible_pixels(composited) >= MIN_RUNTIME_VISIBLE_PIXELS:
        return composited, False
    lifted = _apply_brightness_floor(logo.copy(), VISIBILITY_BRIGHTNESS_FLOOR)
    return _composite_logo(lifted), True


def render_logo(
    svg: bytes,
    leading_emblem_crop: bool = False,
    brightness_boost: bool = False,
) -> tuple[Image.Image, bool]:
    """Render one SVG and selectively rescue content invisible at runtime.

    `brightness_boost` forces the brightness floor even when the logo is above
    the near-blank threshold -- for faint/dark art that is technically visible
    but unreadable on the panel.
    """
    png = cairosvg.svg2png(
        bytestring=svg,
        scale=RASTER_SCALE,
        unsafe=True,
    )
    logo = Image.open(io.BytesIO(png)).convert("RGBA")
    logo = _crop_to_content(logo)
    if leading_emblem_crop:
        logo = _crop_to_content(_crop_leading_square(logo))
    logo = _fit_to_logo_cell(logo)

    canvas = _composite_logo(logo)
    if not brightness_boost and _runtime_visible_pixels(canvas) >= MIN_RUNTIME_VISIBLE_PIXELS:
        return canvas, False

    lifted_logo = _apply_brightness_floor(
        logo.copy(),
        VISIBILITY_BRIGHTNESS_FLOOR,
    )
    return _composite_logo(lifted_logo), True


def rgb565_bytes(image: Image.Image) -> bytes:
    encoded = bytearray()
    pixels = image.load()
    width, height = image.size
    for y in range(height):
        for x in range(width):
            red, green, blue = pixels[x, y]
            pixel = (
                ((red & 0xF8) << 8) |
                ((green & 0xFC) << 3) |
                (blue >> 3)
            )
            encoded.extend(struct.pack("<H", pixel))
    return bytes(encoded)


def write_index(entries: list[tuple[str, str, int]]) -> None:
    INDEX_HEADER_PATH.write_text(
        f"""#pragma once

#include <Arduino.h>

namespace AirlineLogoLibrary
{{
    static constexpr uint8_t LOGO_WIDTH = {LOGO_WIDTH};
    static constexpr uint8_t LOGO_HEIGHT = {LOGO_HEIGHT};
    static constexpr size_t PIXELS_PER_LOGO = {PIXELS_PER_LOGO};
    static constexpr size_t BYTES_PER_LOGO = {BYTES_PER_LOGO};

    const uint8_t *findByIata(const String &iata);
    bool hasIata(const String &iata);
    String findIataByIcao(const String &icao);
}}
""",
        encoding="utf-8",
    )

    iata_rows = "\n".join(
        f'    {{"{iata}", {offset}UL}},' for iata, _, offset in entries
    )
    icao_rows = "\n".join(
        f'    {{"{icao}", "{iata}"}},'
        for iata, icao, _ in sorted(entries, key=lambda entry: entry[1])
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
{iata_rows}
}};

struct AirlineCodeEntry
{{
    const char *icao;
    const char *iata;
}};

const AirlineCodeEntry AIRLINE_CODE_INDEX[] = {{
{icao_rows}
}};

extern const uint8_t LOGO_DATA_START[]
    asm("_binary_assets_airline_logos_bin_start");
extern const uint8_t LOGO_DATA_END[]
    asm("_binary_assets_airline_logos_bin_end");

String normalizedCode(const String &value)
{{
    String normalized = value;
    normalized.trim();
    normalized.toUpperCase();
    return normalized;
}}

const uint8_t *logoAtOffset(uint32_t offset)
{{
    const uintptr_t dataStart =
        reinterpret_cast<uintptr_t>(LOGO_DATA_START);
    const uintptr_t dataEnd =
        reinterpret_cast<uintptr_t>(LOGO_DATA_END);
    if (dataEnd < dataStart)
    {{
        return nullptr;
    }}
    const size_t dataSize = static_cast<size_t>(dataEnd - dataStart);
    if (offset > dataSize ||
        AirlineLogoLibrary::BYTES_PER_LOGO > dataSize - offset)
    {{
        return nullptr;
    }}
    return reinterpret_cast<const uint8_t *>(dataStart + offset);
}}
}}

const uint8_t *AirlineLogoLibrary::findByIata(const String &iata)
{{
    const String normalized = normalizedCode(iata);
    if (normalized.length() != 2)
    {{
        return nullptr;
    }}

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
            return logoAtOffset(LOGO_INDEX[middle].offset);
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

bool AirlineLogoLibrary::hasIata(const String &iata)
{{
    return findByIata(iata) != nullptr;
}}

String AirlineLogoLibrary::findIataByIcao(const String &icao)
{{
    const String normalized = normalizedCode(icao);
    if (normalized.length() != 3)
    {{
        return String();
    }}

    int left = 0;
    int right = static_cast<int>(
        sizeof(AIRLINE_CODE_INDEX) / sizeof(AIRLINE_CODE_INDEX[0])) - 1;
    while (left <= right)
    {{
        const int middle = left + (right - left) / 2;
        const int comparison = strcmp(
            normalized.c_str(),
            AIRLINE_CODE_INDEX[middle].icao);
        if (comparison == 0)
        {{
            return String(AIRLINE_CODE_INDEX[middle].iata);
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
    return String();
}}
""",
        encoding="utf-8",
    )


def _airlines_by_iata(airlines: list[dict[str, object]]) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    icao_codes: set[str] = set()
    for airline in airlines:
        iata_value = airline.get("iata")
        icao_value = airline.get("icao")
        if not isinstance(iata_value, str) or len(iata_value) != 2:
            raise ValueError(f"invalid IATA code: {iata_value!r}")
        if not isinstance(icao_value, str) or len(icao_value) != 3:
            raise ValueError(f"invalid ICAO code for {iata_value}: {icao_value!r}")

        iata = iata_value.upper()
        icao = icao_value.upper()
        if iata in result:
            raise ValueError(f"duplicate IATA code: {iata}")
        if icao in icao_codes:
            raise ValueError(f"duplicate ICAO code: {icao}")
        result[iata] = airline
        icao_codes.add(icao)

    if len(result) != EXPECTED_LOGO_COUNT:
        raise ValueError(
            f"expected {EXPECTED_LOGO_COUNT} airlines, found {len(result)}"
        )
    return result


def _download_logo(
    iata: str,
    curated_slugs: dict[str, str],
) -> tuple[bytes, str]:
    if iata in curated_slugs and iata not in PRIMARY_SOURCE_IATAS:
        slug = curated_slugs[iata]
        return (
            download(f"{CURATED_SOURCE_ROOT}/assets/{slug}/icon.svg"),
            "curated",
        )
    return download(f"{SOURCE_ROOT}/logos/{iata}.svg"), "primary"


def _validate_logo(iata: str, canvas: Image.Image) -> None:
    if canvas.size != (LOGO_WIDTH, LOGO_HEIGHT):
        raise ValueError(f"unexpected canvas size {canvas.size}")
    bbox = canvas.getbbox()
    if bbox is None:
        raise ValueError("logo has no representable RGB content")

    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    if width < 4 or height < 3:
        raise ValueError(f"logo has too little representable content: bbox={bbox}")

    visible_pixels = _runtime_visible_pixels(canvas)
    if visible_pixels < MIN_RUNTIME_VISIBLE_PIXELS:
        raise ValueError(
            f"only {visible_pixels} pixels survive runtime brightness"
        )

    pixels = canvas.load()
    colors = [
        pixels[x, y]
        for y in range(LOGO_HEIGHT)
        for x in range(LOGO_WIDTH)
        if pixels[x, y] != (0, 0, 0)
    ]
    if iata == "QR" and not any(
        red >= 80 and red > blue and blue > green
        for red, green, blue in colors
    ):
        raise ValueError("Qatar logo is missing its burgundy color family")
    if iata == "VS" and not any(
        red >= 180 and red > green * 3 and red > blue * 2
        for red, green, blue in colors
    ):
        raise ValueError("Virgin logo is missing its red color family")


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
    airlines_by_iata = _airlines_by_iata(airlines)

    entries: list[tuple[str, str, int]] = []
    logo_data = bytearray()
    failures: list[str] = []
    for iata in sorted(airlines_by_iata):
        try:
            airline = airlines_by_iata[iata]
            icao = str(airline["icao"]).upper()
            if iata in MANUAL_LOGO_LABELS:
                text, color = MANUAL_LOGO_LABELS[iata]
                canvas, brightness_lifted = render_text_logo(text, color)
                source = "manual"
            else:
                svg, source = _download_logo(iata, curated_slugs)
                canvas, brightness_lifted = render_logo(
                    svg,
                    leading_emblem_crop=iata in LEADING_EMBLEM_CROP_IATAS,
                    brightness_boost=iata in BRIGHTNESS_BOOST_IATAS,
                )
            _validate_logo(iata, canvas)

            encoded = rgb565_bytes(canvas)
            if len(encoded) != BYTES_PER_LOGO:
                raise ValueError(f"unexpected encoded size {len(encoded)}")
            entries.append((iata, icao, len(logo_data)))
            logo_data.extend(encoded)
            lift_label = ", visibility-lifted" if brightness_lifted else ""
            print(f"generated {iata} from {source}{lift_label}")
        except Exception as error:
            failures.append(f"{iata}: {error}")

    if failures:
        raise RuntimeError(
            f"failed to generate {len(failures)} logos:\n"
            + "\n".join(failures)
        )
    if len(logo_data) != EXPECTED_LOGO_COUNT * BYTES_PER_LOGO:
        raise RuntimeError(f"unexpected library size {len(logo_data)}")

    BINARY_PATH.write_bytes(logo_data)
    write_index(entries)
    print(
        f"wrote {len(entries)} logos, {len(logo_data)} bytes "
        f"to {BINARY_PATH.relative_to(PROJECT_ROOT)}"
    )


if __name__ == "__main__":
    main()
