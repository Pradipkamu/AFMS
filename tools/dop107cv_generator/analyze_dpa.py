#!/usr/bin/env python3
"""Inspect Delta DOPSoft .dpa archives used by the DOP-107CV.

Observed layout for DOPSoft V1010 samples:
  [BMP preview][GZIP-compressed proprietary project payload]

This tool finds the GZIP member rather than relying on a fixed offset.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import struct
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"


def inspect(path: Path, output_dir: Path) -> None:
    data = path.read_bytes()
    gzip_offset = data.find(GZIP_MAGIC)
    if gzip_offset < 0:
        raise ValueError(f"No GZIP project payload found in {path}")

    preview = data[:gzip_offset]
    payload_gz = data[gzip_offset:]
    payload = gzip.decompress(payload_gz)

    if preview[:2] != b"BM":
        raise ValueError("Archive does not start with a BMP preview")

    width, height = struct.unpack_from("<ii", preview, 18)
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = path.name.replace(".", "_")
    preview_path = output_dir / f"{stem}_preview.bmp"
    payload_path = output_dir / f"{stem}_payload.bin"
    preview_path.write_bytes(preview)
    payload_path.write_bytes(payload)

    print(f"file:               {path}")
    print(f"archive bytes:      {len(data)}")
    print(f"preview:            {width} x {height}, {len(preview)} bytes")
    print(f"gzip offset:        {gzip_offset}")
    print(f"compressed payload: {len(payload_gz)} bytes")
    print(f"expanded payload:   {len(payload)} bytes")
    print(f"payload header:     {payload[:40]!r}")
    print(f"archive sha256:     {hashlib.sha256(data).hexdigest()}")
    print(f"preview written:    {preview_path}")
    print(f"payload written:    {payload_path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path("dpa_analysis"))
    args = parser.parse_args()
    inspect(args.archive, args.output_dir)


if __name__ == "__main__":
    main()
