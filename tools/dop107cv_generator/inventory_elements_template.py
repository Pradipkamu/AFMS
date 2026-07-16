#!/usr/bin/env python3
"""Inventory geometry-anchored objects in a DOPSoft Elements template."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"
DIGIT_XOR = 0x54
GEOMETRY_MARKER = b"in<"
WIDTH_CONTEXT = bytes.fromhex("696e330d00100c")
HEIGHT_CONTEXT = bytes.fromhex("696e2c010d030c10")


def decode_scalar(data: bytes, start: int) -> tuple[int, int]:
    if data[start] != 0x59:
        raise ValueError("missing geometry scalar marker")
    position = start + 1
    digits: list[str] = []
    while position < len(data):
        digit = data[position] ^ DIGIT_XOR
        if 0 <= digit <= 9:
            digits.append(str(digit))
            position += 1
            continue
        break
    if not digits:
        raise ValueError("geometry scalar has no digits")
    return int("".join(digits)), position


def geometry_records(payload: bytes) -> list[tuple[int, int, int, int, int]]:
    records: list[tuple[int, int, int, int, int]] = []
    position = 0
    while True:
        position = payload.find(GEOMETRY_MARKER, position)
        if position < 0:
            break
        try:
            x, x_end = decode_scalar(payload, position + 3)
            y_marker = payload.find(b"in=", x_end, x_end + 100)
            y, y_end = decode_scalar(payload, y_marker + 3)
            width_marker = payload.find(WIDTH_CONTEXT, y_end, y_end + 200)
            width, width_end = decode_scalar(payload, width_marker + len(WIDTH_CONTEXT))
            height_marker = payload.find(HEIGHT_CONTEXT, width_end, width_end + 200)
            height, _ = decode_scalar(payload, height_marker + len(HEIGHT_CONTEXT))
            records.append((position, x, y, width, height))
        except Exception:
            pass
        position += 1
    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--context", type=int, default=256)
    args = parser.parse_args()

    archive = args.archive.read_bytes()
    gzip_offset = archive.find(GZIP_MAGIC)
    if gzip_offset < 0:
        raise ValueError("gzip payload not found")
    payload = gzip.decompress(archive[gzip_offset:])
    records = geometry_records(payload)

    objects = []
    for index, (offset, x, y, width, height) in enumerate(records):
        context = payload[offset : offset + args.context]
        objects.append(
            {
                "index": index,
                "payload_offset": offset,
                "payload_offset_hex": f"0x{offset:X}",
                "geometry": {"x": x, "y": y, "width": width, "height": height},
                "context_sha256": hashlib.sha256(context).hexdigest(),
                "context_preview_hex": context[:64].hex(),
            }
        )

    report = {
        "source": args.archive.name,
        "archive_size": len(archive),
        "gzip_offset": gzip_offset,
        "payload_size": len(payload),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "object_count": len(objects),
        "objects": objects,
    }
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
