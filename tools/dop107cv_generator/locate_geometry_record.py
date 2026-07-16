#!/usr/bin/env python3
"""Locate the authoritative geometry field cluster in a Delta DOPSoft .dpa payload.

The DOP107CV samples show four adjacent geometry properties stored under stable
field markers. This tool extracts the gzip payload, searches for the marker
cluster, and reports the raw encoded values for X, Y, width, and height.

It intentionally does not rewrite values yet. The encoding is still being
mapped, and unsafe mutation could corrupt the project.
"""

from __future__ import annotations

import argparse
import gzip
import json
from dataclasses import dataclass, asdict
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"

# Stable marker cluster observed around payload offset 0x4220 in the supplied
# DOP V1010 samples. Each field begins with 0x59 and ends before the next 0x69
# ('i') marker in this serialized stream.
X_MARKER = bytes.fromhex("696e3c")
Y_MARKER = bytes.fromhex("696e3d")
W_CONTEXT = bytes.fromhex("696e330d00100c")
H_CONTEXT = bytes.fromhex("696e2c010d030c10")


@dataclass
class GeometryRecord:
    payload_offset: int
    x_encoded_hex: str
    y_encoded_hex: str
    width_encoded_hex: str
    height_encoded_hex: str


def extract_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    offset = raw.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"No gzip payload found in {path}")
    return gzip.decompress(raw[offset:])


def _read_encoded_value(payload: bytes, start: int, max_len: int = 8) -> bytes:
    """Read one short encoded scalar beginning with 0x59.

    In the known samples all geometry values begin with 0x59 and are followed
    by 2-3 encoded bytes. The next field marker begins with 0x69. We therefore
    stop at 0x69 or after max_len bytes.
    """
    if start >= len(payload) or payload[start] != 0x59:
        raise ValueError(f"Expected encoded scalar at payload offset {start:#x}")
    end = start + 1
    while end < len(payload) and end - start < max_len and payload[end] != 0x69:
        end += 1
    return payload[start:end]


def locate_geometry_record(payload: bytes) -> GeometryRecord:
    x_marker = payload.find(X_MARKER)
    if x_marker < 0:
        raise ValueError("X geometry marker was not found")

    x_start = x_marker + len(X_MARKER)
    x_value = _read_encoded_value(payload, x_start)

    y_marker = payload.find(Y_MARKER, x_start)
    if y_marker < 0:
        raise ValueError("Y geometry marker was not found")
    y_start = y_marker + len(Y_MARKER)
    y_value = _read_encoded_value(payload, y_start)

    w_context = payload.find(W_CONTEXT, y_start)
    if w_context < 0:
        raise ValueError("Width geometry context was not found")
    w_start = w_context + len(W_CONTEXT)
    w_value = _read_encoded_value(payload, w_start)

    h_context = payload.find(H_CONTEXT, w_start)
    if h_context < 0:
        raise ValueError("Height geometry context was not found")
    h_start = h_context + len(H_CONTEXT)
    h_value = _read_encoded_value(payload, h_start)

    return GeometryRecord(
        payload_offset=x_marker,
        x_encoded_hex=x_value.hex(),
        y_encoded_hex=y_value.hex(),
        width_encoded_hex=w_value.hex(),
        height_encoded_hex=h_value.hex(),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    results = []
    for path in args.files:
        record = locate_geometry_record(extract_payload(path))
        item = {"file": str(path), **asdict(record)}
        results.append(item)

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for item in results:
            print(item["file"])
            print(f"  record offset : 0x{item['payload_offset']:X}")
            print(f"  X encoded     : {item['x_encoded_hex']}")
            print(f"  Y encoded     : {item['y_encoded_hex']}")
            print(f"  Width encoded : {item['width_encoded_hex']}")
            print(f"  Height encoded: {item['height_encoded_hex']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
