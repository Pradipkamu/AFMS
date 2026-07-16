#!/usr/bin/env python3
"""Lossless structural tools for Delta DOPSoft .dpa archives.

A DPA observed from DOPSoft V1010 consists of a BMP preview followed by a gzip
member containing the editable project payload. This module intentionally does
not interpret or alter the proprietary payload yet.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import struct
from dataclasses import dataclass
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"
DOP_SIGNATURE = b"Delta-HMI Screen Editor DOP "


@dataclass(frozen=True)
class DpaParts:
    preview: bytes
    payload: bytes
    gzip_offset: int


def split_dpa(data: bytes) -> DpaParts:
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("not a DOPSoft DPA with a BMP preview")
    offset = data.find(GZIP_MAGIC, 54)
    if offset < 0:
        raise ValueError("gzip project payload not found")
    payload = gzip.decompress(data[offset:])
    if not payload.startswith(DOP_SIGNATURE):
        raise ValueError("decompressed payload lacks Delta DOP signature")
    return DpaParts(data[:offset], payload, offset)


def pack_dpa(parts: DpaParts, *, mtime: int = 0) -> bytes:
    output = io.BytesIO()
    with gzip.GzipFile(fileobj=output, mode="wb", mtime=mtime) as gz:
        gz.write(parts.payload)
    return parts.preview + output.getvalue()


def bmp_dimensions(preview: bytes) -> tuple[int, int, int]:
    if preview[:2] != b"BM" or len(preview) < 30:
        raise ValueError("invalid BMP preview")
    width, height = struct.unpack_from("<ii", preview, 18)
    bits_per_pixel = struct.unpack_from("<H", preview, 28)[0]
    return width, height, bits_per_pixel


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect(path: Path) -> dict[str, object]:
    archive = path.read_bytes()
    parts = split_dpa(archive)
    width, height, bpp = bmp_dimensions(parts.preview)
    return {
        "file": str(path),
        "archive_size": len(archive),
        "gzip_offset": parts.gzip_offset,
        "preview_size": len(parts.preview),
        "preview_width": width,
        "preview_height": height,
        "preview_bpp": bpp,
        "payload_size": len(parts.payload),
        "archive_sha256": sha256(archive),
        "preview_sha256": sha256(parts.preview),
        "payload_sha256": sha256(parts.payload),
        "payload_signature": parts.payload[:40].decode("ascii", errors="replace"),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    inspect_cmd = commands.add_parser("inspect")
    inspect_cmd.add_argument("input", type=Path)

    unpack_cmd = commands.add_parser("unpack")
    unpack_cmd.add_argument("input", type=Path)
    unpack_cmd.add_argument("output_dir", type=Path)

    repack_cmd = commands.add_parser("repack")
    repack_cmd.add_argument("preview", type=Path)
    repack_cmd.add_argument("payload", type=Path)
    repack_cmd.add_argument("output", type=Path)

    verify_cmd = commands.add_parser("verify-roundtrip")
    verify_cmd.add_argument("input", type=Path)
    verify_cmd.add_argument("output", type=Path)

    args = parser.parse_args()

    if args.command == "inspect":
        print(json.dumps(inspect(args.input), indent=2))
        return 0

    if args.command == "unpack":
        parts = split_dpa(args.input.read_bytes())
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / "preview.bmp").write_bytes(parts.preview)
        (args.output_dir / "project.payload").write_bytes(parts.payload)
        (args.output_dir / "manifest.json").write_text(
            json.dumps(inspect(args.input), indent=2) + "\n", encoding="utf-8"
        )
        return 0

    if args.command == "repack":
        preview = args.preview.read_bytes()
        payload = args.payload.read_bytes()
        if not payload.startswith(DOP_SIGNATURE):
            raise ValueError("invalid DOP payload")
        args.output.write_bytes(pack_dpa(DpaParts(preview, payload, len(preview))))
        return 0

    original = split_dpa(args.input.read_bytes())
    rebuilt = pack_dpa(original)
    args.output.write_bytes(rebuilt)
    checked = split_dpa(rebuilt)
    valid = checked.preview == original.preview and checked.payload == original.payload
    print(
        json.dumps(
            {
                "structural_roundtrip": valid,
                "output": str(args.output),
                "output_sha256": sha256(rebuilt),
            },
            indent=2,
        )
    )
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
