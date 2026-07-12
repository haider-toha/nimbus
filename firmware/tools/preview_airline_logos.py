"""Decode the shipped airline logo blob into a reviewable preview corpus.

Reads the REAL `firmware/assets/airline_logos.bin` + the `AirlineLogoLibrary.cpp`
index, applies the SAME on-device brightness scaling and dithering the firmware
uses (`generate_airline_logos._runtime_pixel`), and emits, for every
bundled IATA:

* a per-IATA zoomed PNG (device-accurate, black background), and
* objective visual-quality metrics (runtime-visible pixels, cell fill %,
  content bounding-box coverage, distinct colours, luminance) with automatic
  failure flags, written to a machine-readable TSV, plus
* per-QA-batch contact sheets (A-G / H-N / O-T / U-Z+numeric) so a reviewer can
  eyeball a whole range at once.

Nothing here re-derives the logo pipeline: the pixels come straight from the
shipped blob and are scaled by the generator's own runtime function, so the
previews and the `logo_px` column in the C++ QA harness agree by construction.

Run from the firmware directory (cairosvg is only pulled in transitively via
`generate_airline_logos`; this tool itself never rasterises SVG):
    uv run --with cairosvg --with pillow python tools/preview_airline_logos.py
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

from PIL import Image, ImageDraw

# Make `tools` importable whether this file is run as `python tools/preview_...`
# (sys.path[0] == tools/) or `python -m tools.preview_...` -- mirrors how the
# unittest reaches `generate_airline_logos`.
_FIRMWARE_ROOT = Path(__file__).resolve().parents[1]
if str(_FIRMWARE_ROOT) not in sys.path:
    sys.path.insert(0, str(_FIRMWARE_ROOT))

from tools import generate_airline_logos as gen  # noqa: E402

# --- Layout / cell contract (mirrors AirlineLogoLibrary.h + Hub75Display) ---
LOGO_WIDTH = gen.LOGO_WIDTH
LOGO_HEIGHT = gen.LOGO_HEIGHT
PIXELS_PER_LOGO = gen.PIXELS_PER_LOGO
BYTES_PER_LOGO = gen.BYTES_PER_LOGO

# --- Preview rendering knobs ------------------------------------------------
TILE_SCALE = 8  # per-IATA PNG zoom (40x30 -> 320x240)
SHEET_SCALE = 4  # contact-sheet tile zoom (40x30 -> 160x120)
SHEET_COLUMNS = 8
SHEET_ROWS_PER_PAGE = 7  # 8x7 = 56 logos per contact-sheet page
CAPTION_HEIGHT = 22
TILE_BORDER = 1

# --- Objective failure thresholds (brute-force, applied to EVERY logo) ------
# runtime-visible pixels below this read as a near-empty "stub" in the 1200-px
# cell -- the generator's own hard floor is only 12 (MIN_RUNTIME_VISIBLE_PIXELS),
# which passes glyphs that still look empty on the panel.
NEAR_EMPTY_PX = 96  # ~8% of the 1200-px cell
SMALL_BBOX_COVER = 0.22  # content bbox spans <22% of the cell area
# "Dim" is judged on the brightest STORED channel (max of R,G,B), hue-
# independent -- the same peak-channel measure the generator's brightness floor
# uses. Luminance would misfire here: a vivid pure-red logo has luminance 76 and
# pure blue 29, so a luminance threshold would flag saturated brand colours as
# dark. A logo whose brightest channel is still below this is genuinely dark art.
DIM_PEAK_CHANNEL = 110
MONO_DISTINCT_COLORS = 3  # near-single-colour glyph

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = PROJECT_ROOT.parent / "audit" / "display-iteration" / "corpus"
INDEX_SOURCE_PATH = gen.INDEX_SOURCE_PATH
BINARY_PATH = gen.BINARY_PATH

_INDEX_ENTRY = re.compile(r'\{"([A-Z0-9]{2})",\s*(\d+)UL\}')


def batch_of(iata: str) -> str:
    """Map an IATA code to its QA batch by first character.

    A-G / H-N / O-T are letter ranges; U-Z and any digit-led code share the
    'U-Z+numeric' batch, matching the Track 2a-2d split.
    """
    head = iata[0]
    if head.isdigit():
        return "U-Z_numeric"
    if "A" <= head <= "G":
        return "A-G"
    if "H" <= head <= "N":
        return "H-N"
    if "O" <= head <= "T":
        return "O-T"
    return "U-Z_numeric"


@dataclass(frozen=True)
class LogoMetrics:
    """Objective visual-quality facts for one decoded logo."""

    iata: str
    batch: str
    stored_px: int  # non-transparent pixels in the stored blob
    runtime_px: int  # pixels still lit after runtime scaling (device truth)
    fill_pct: float  # runtime_px / 1200
    bbox_w: int
    bbox_h: int
    bbox_cover_pct: float  # bbox area / 1200
    distinct_colors: int
    peak_chan: int  # brightest stored channel (max R/G/B) over visible px
    mean_chan: int  # mean of per-pixel peak channel over visible px
    flags: str  # '|'-joined failure flags, or 'ok'


def parse_index(index_source: str) -> list[tuple[str, int]]:
    """Return ordered (iata, byte-offset) pairs from AirlineLogoLibrary.cpp."""
    return [(iata, int(offset)) for iata, offset in _INDEX_ENTRY.findall(index_source)]


def decode_logo(blob: bytes, offset: int) -> list[int]:
    """Decode one logo's 1200 little-endian RGB565 pixels from the blob."""
    return list(struct.unpack_from(f"<{PIXELS_PER_LOGO}H", blob, offset))


def _expand565(pixel565: int) -> tuple[int, int, int]:
    """RGB565 -> 8-bit RGB (no brightness scaling)."""
    red = ((pixel565 >> 11) & 0x1F) * 255 // 31
    green = ((pixel565 >> 5) & 0x3F) * 255 // 63
    blue = (pixel565 & 0x1F) * 255 // 31
    return red, green, blue


def runtime_rgb(pixel565: int, x: int, y: int) -> tuple[int, int, int]:
    """Runtime RGB565 pixel -> 8-bit RGB, exactly as the panel shows it."""
    return _expand565(gen._runtime_pixel(pixel565, x, y))


def _peak_channel(red: int, green: int, blue: int) -> int:
    """Brightest single channel -- hue-independent 'how bright is this pixel'."""
    return max(red, green, blue)


def runtime_image(pixels565: list[int]) -> Image.Image:
    """Build the device-accurate 40x30 RGB image on a black background."""
    image = Image.new("RGB", (LOGO_WIDTH, LOGO_HEIGHT), (0, 0, 0))
    out = image.load()
    for index, pixel in enumerate(pixels565):
        x = index % LOGO_WIDTH
        y = index // LOGO_WIDTH
        if gen._runtime_pixel(pixel, x, y) == 0:
            continue
        out[x, y] = runtime_rgb(pixel, x, y)
    return image


def measure(iata: str, pixels565: list[int]) -> LogoMetrics:
    """Compute objective quality metrics + failure flags for one logo."""
    stored_px = sum(1 for pixel in pixels565 if pixel != 0)

    # Visibility is judged on the runtime render (a stored pixel that
    # rounds to 0 on the panel is genuinely invisible). Brand-colour quality
    # (distinct colours, luminance) is judged on the STORED full-brightness
    # colour: the panel dims every logo by the same 32/255, so runtime
    # luminance caps near 32 for even a pure-white pixel and cannot tell a
    # vivid logo from a washed-out one -- the stored colour can.
    visible: list[tuple[int, int]] = []  # (x, y)
    colors: set[int] = set()
    chans: list[int] = []
    for index, pixel in enumerate(pixels565):
        x = index % LOGO_WIDTH
        y = index // LOGO_WIDTH
        if gen._runtime_pixel(pixel, x, y) == 0:
            continue
        visible.append((x, y))
        colors.add(pixel)
        chans.append(_peak_channel(*_expand565(pixel)))

    runtime_px = len(visible)
    if visible:
        xs = [x for x, _ in visible]
        ys = [y for _, y in visible]
        bbox_w = max(xs) - min(xs) + 1
        bbox_h = max(ys) - min(ys) + 1
        peak_chan = max(chans)
        mean_chan = round(sum(chans) / len(chans))
    else:
        bbox_w = bbox_h = peak_chan = mean_chan = 0

    bbox_cover = (bbox_w * bbox_h) / PIXELS_PER_LOGO
    fill_pct = runtime_px / PIXELS_PER_LOGO

    flags: list[str] = []
    if runtime_px < NEAR_EMPTY_PX:
        flags.append("NEAR_EMPTY")
    if bbox_cover < SMALL_BBOX_COVER:
        flags.append("SMALL_BBOX")
    if peak_chan < DIM_PEAK_CHANNEL:
        flags.append("DIM")
    if len(colors) < MONO_DISTINCT_COLORS:
        flags.append("MONO")

    return LogoMetrics(
        iata=iata,
        batch=batch_of(iata),
        stored_px=stored_px,
        runtime_px=runtime_px,
        fill_pct=round(fill_pct, 4),
        bbox_w=bbox_w,
        bbox_h=bbox_h,
        bbox_cover_pct=round(bbox_cover, 4),
        distinct_colors=len(colors),
        peak_chan=peak_chan,
        mean_chan=mean_chan,
        flags="|".join(flags) if flags else "ok",
    )


def _draw_caption(
    draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, ok: bool
) -> None:
    draw.text(xy, text, fill=(200, 255, 200) if ok else (255, 170, 170))


def write_tile(image: Image.Image, metrics: LogoMetrics, path: Path) -> None:
    """Write one zoomed, captioned per-IATA preview PNG."""
    zoomed = image.resize(
        (LOGO_WIDTH * TILE_SCALE, LOGO_HEIGHT * TILE_SCALE), Image.NEAREST
    )
    canvas = Image.new(
        "RGB", (zoomed.width, zoomed.height + CAPTION_HEIGHT), (24, 24, 24)
    )
    canvas.paste(zoomed, (0, 0))
    draw = ImageDraw.Draw(canvas)
    caption = (
        f"{metrics.iata} px{metrics.runtime_px} {metrics.fill_pct:.0%} {metrics.flags}"
    )
    _draw_caption(draw, (3, zoomed.height + 6), caption, metrics.flags == "ok")
    canvas.save(path)


def write_contact_sheet(
    tiles: list[tuple[Image.Image, LogoMetrics]],
    batch: str,
    page: int,
    out_dir: Path,
) -> Path:
    """Tile up to SHEET_ROWS_PER_PAGE*SHEET_COLUMNS logos into one sheet page."""
    tile_w = LOGO_WIDTH * SHEET_SCALE + 2 * TILE_BORDER
    tile_h = LOGO_HEIGHT * SHEET_SCALE + CAPTION_HEIGHT + 2 * TILE_BORDER
    sheet_w = SHEET_COLUMNS * tile_w
    sheet_h = SHEET_ROWS_PER_PAGE * tile_h
    sheet = Image.new("RGB", (sheet_w, sheet_h), (48, 48, 48))
    draw = ImageDraw.Draw(sheet)

    for position, (image, metrics) in enumerate(tiles):
        row, col = divmod(position, SHEET_COLUMNS)
        ox, oy = col * tile_w + TILE_BORDER, row * tile_h + TILE_BORDER
        zoomed = image.resize(
            (LOGO_WIDTH * SHEET_SCALE, LOGO_HEIGHT * SHEET_SCALE), Image.NEAREST
        )
        sheet.paste(zoomed, (ox, oy))
        caption = f"{metrics.iata} {metrics.runtime_px}px {metrics.flags}"
        _draw_caption(
            draw, (ox + 2, oy + zoomed.height + 5), caption, metrics.flags == "ok"
        )

    path = out_dir / f"sheet_{batch}_p{page}.png"
    sheet.save(path)
    return path


def write_metrics_tsv(rows: list[LogoMetrics], path: Path) -> None:
    header = list(asdict(rows[0]).keys())
    lines = ["\t".join(header)]
    lines.extend(
        "\t".join(str(value) for value in asdict(row).values()) for row in rows
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_corpus(out_dir: Path) -> list[LogoMetrics]:
    """Decode every bundled logo, writing previews + metrics under out_dir."""
    index = parse_index(INDEX_SOURCE_PATH.read_text(encoding="utf-8"))
    blob = BINARY_PATH.read_bytes()

    logos_dir = out_dir / "logos"
    sheets_dir = out_dir / "sheets"
    logos_dir.mkdir(parents=True, exist_ok=True)
    sheets_dir.mkdir(parents=True, exist_ok=True)

    rows: list[LogoMetrics] = []
    by_batch: dict[str, list[tuple[Image.Image, LogoMetrics]]] = {}
    for iata, offset in index:
        pixels = decode_logo(blob, offset)
        image = runtime_image(pixels)
        metrics = measure(iata, pixels)
        rows.append(metrics)
        write_tile(image, metrics, logos_dir / f"{iata}.png")
        by_batch.setdefault(metrics.batch, []).append((image, metrics))

    for batch in sorted(by_batch):
        tiles = sorted(by_batch[batch], key=lambda pair: pair[1].iata)
        page_size = SHEET_COLUMNS * SHEET_ROWS_PER_PAGE
        for page, start in enumerate(range(0, len(tiles), page_size), start=1):
            write_contact_sheet(
                tiles[start : start + page_size], batch, page, sheets_dir
            )

    rows.sort(key=lambda row: row.iata)
    write_metrics_tsv(rows, out_dir / "logo_metrics.tsv")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help=f"corpus output directory (default: {DEFAULT_OUT})",
    )
    args = parser.parse_args()

    rows = build_corpus(args.out)
    flagged = [row for row in rows if row.flags != "ok"]
    print(f"decoded {len(rows)} logos -> {args.out}")
    print(
        f"flagged {len(flagged)} logos: "
        + ", ".join(f"{row.iata}({row.flags})" for row in flagged[:40])
        + (" ..." if len(flagged) > 40 else "")
    )
    for seed in ("BA", "OU", "SK"):
        match = next((row for row in rows if row.iata == seed), None)
        if match is not None:
            print(
                f"  seed {seed}: runtime_px={match.runtime_px} fill={match.fill_pct:.1%} "
                f"bbox={match.bbox_w}x{match.bbox_h} colors={match.distinct_colors} "
                f"peak_chan={match.peak_chan} flags={match.flags}"
            )


if __name__ == "__main__":
    main()
