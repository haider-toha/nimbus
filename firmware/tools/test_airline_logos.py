"""Host-side integrity tests for the generated airline logo library."""

from __future__ import annotations

import re
import struct
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image

from tools import generate_airline_logos as logos


class AirlineLogoLibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = logos.BINARY_PATH.read_bytes()
        cls.index_source = logos.INDEX_SOURCE_PATH.read_text(encoding="utf-8")
        cls.logo_entries = [
            (iata, int(offset))
            for iata, offset in re.findall(
                r'\{"([A-Z0-9]{2})", ([0-9]+)UL\}',
                cls.index_source,
            )
        ]
        cls.airline_codes = dict(
            re.findall(
                r'\{"([A-Z0-9]{3})", "([A-Z0-9]{2})"\}',
                cls.index_source,
            )
        )

    def test_rgb565_byte_order(self) -> None:
        image = Image.new("RGB", (3, 1))
        image.putdata([(255, 0, 0), (0, 255, 0), (0, 0, 255)])
        self.assertEqual(
            logos.rgb565_bytes(image),
            b"\x00\xf8\xe0\x07\x1f\x00",
        )

    def test_bytewise_decode_works_from_odd_address(self) -> None:
        encoded = b"\xff" + b"\x00\xf8"
        pixel = encoded[1] | (encoded[2] << 8)
        self.assertEqual(pixel, 0xF800)

    def test_library_offsets_and_bounds_are_complete(self) -> None:
        self.assertEqual(len(self.logo_entries), logos.EXPECTED_LOGO_COUNT)
        self.assertEqual(len(self.airline_codes), logos.EXPECTED_LOGO_COUNT)
        self.assertEqual(
            len(self.binary),
            logos.EXPECTED_LOGO_COUNT * logos.BYTES_PER_LOGO,
        )
        for index, (_, offset) in enumerate(self.logo_entries):
            self.assertEqual(offset, index * logos.BYTES_PER_LOGO)
            self.assertLessEqual(
                offset + logos.BYTES_PER_LOGO,
                len(self.binary),
            )

    def test_known_icao_codes_map_to_logo_iata_codes(self) -> None:
        self.assertEqual(self.airline_codes["QTR"], "QR")
        self.assertEqual(self.airline_codes["VIR"], "VS")

    def test_qatar_and_virgin_keep_expected_color_families(self) -> None:
        offsets = dict(self.logo_entries)
        qatar_colors = self._decoded_colors(offsets["QR"])
        virgin_colors = self._decoded_colors(offsets["VS"])

        self.assertTrue(
            any(
                red >= 80 and red > blue and blue > green
                for red, green, blue in qatar_colors
            )
        )
        self.assertTrue(
            any(
                red >= 180 and red > green * 3 and red > blue * 2
                for red, green, blue in virgin_colors
            )
        )

    def test_every_logo_survives_runtime_brightness(self) -> None:
        for iata, offset in self.logo_entries:
            pixels = struct.unpack_from(
                f"<{logos.PIXELS_PER_LOGO}H",
                self.binary,
                offset,
            )
            visible = sum(logos._runtime_pixel(pixel) != 0 for pixel in pixels)
            self.assertGreaterEqual(
                visible,
                logos.MIN_RUNTIME_VISIBLE_PIXELS,
                iata,
            )

    def test_source_selection_never_silently_falls_back(self) -> None:
        curated_slugs = {"QR": "qatar-airways", "NZ": "air-new-zealand"}
        with mock.patch.object(logos, "download", return_value=b"svg") as download:
            _, qatar_source = logos._download_logo("QR", curated_slugs)
            _, new_zealand_source = logos._download_logo("NZ", curated_slugs)

        self.assertEqual(qatar_source, "curated")
        self.assertEqual(new_zealand_source, "primary")
        self.assertIn("/qatar-airways/icon.svg", download.call_args_list[0].args[0])
        self.assertIn("/logos/NZ.svg", download.call_args_list[1].args[0])

        with mock.patch.object(
            logos,
            "download",
            side_effect=OSError("network failure"),
        ):
            with self.assertRaises(OSError):
                logos._download_logo("QR", curated_slugs)

    def _decoded_colors(self, offset: int) -> list[tuple[int, int, int]]:
        pixels = struct.unpack_from(
            f"<{logos.PIXELS_PER_LOGO}H",
            self.binary,
            offset,
        )
        return [
            (
                ((pixel >> 11) & 0x1F) * 255 // 31,
                ((pixel >> 5) & 0x3F) * 255 // 63,
                (pixel & 0x1F) * 255 // 31,
            )
            for pixel in pixels
            if pixel != 0
        ]


if __name__ == "__main__":
    unittest.main()
