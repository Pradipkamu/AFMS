#!/usr/bin/env python3
"""Unit tests for the structural DPA codec."""
from __future__ import annotations

import gzip
import io
import struct
import unittest

from dpa_codec import DOP_SIGNATURE, DpaParts, bmp_dimensions, pack_dpa, split_dpa


def make_preview(width: int = 164, height: int = 98) -> bytes:
    row_size = ((width * 3 + 3) // 4) * 4
    pixel_size = row_size * height
    file_size = 54 + pixel_size
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, file_size)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<ii", header, 18, width, height)
    struct.pack_into("<H", header, 26, 1)
    struct.pack_into("<H", header, 28, 24)
    struct.pack_into("<I", header, 34, pixel_size)
    return bytes(header) + bytes(pixel_size)


class DpaCodecTests(unittest.TestCase):
    def test_roundtrip_preserves_structural_parts(self) -> None:
        preview = make_preview()
        payload = DOP_SIGNATURE + b"V1010\t" + bytes(range(128))
        archive = pack_dpa(DpaParts(preview, payload, len(preview)))
        decoded = split_dpa(archive)
        self.assertEqual(decoded.preview, preview)
        self.assertEqual(decoded.payload, payload)
        self.assertEqual(decoded.gzip_offset, len(preview))

    def test_preview_dimensions(self) -> None:
        self.assertEqual(bmp_dimensions(make_preview()), (164, 98, 24))

    def test_rejects_non_dpa(self) -> None:
        with self.assertRaises(ValueError):
            split_dpa(b"not a DPA")

    def test_rejects_wrong_payload_signature(self) -> None:
        preview = make_preview()
        output = io.BytesIO()
        with gzip.GzipFile(fileobj=output, mode="wb", mtime=0) as gz:
            gz.write(b"wrong payload")
        with self.assertRaises(ValueError):
            split_dpa(preview + output.getvalue())


if __name__ == "__main__":
    unittest.main()
