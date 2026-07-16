#!/usr/bin/env python3
"""Scan all confirmed DOPSoft object geometry records in a DPA archive.

The DOP V1010 samples store each editable object geometry as a stable marker
cluster containing X, Y, width and height scalar values. Each scalar uses the
confirmed decimal encoding implemented here:

    0x59 + one byte per decimal digit, where encoded_digit = digit XOR 0x54

This scanner is read-only and reports every unambiguous geometry cluster.
"""
from __future__ import annotations

import argparse
import gzip
import json
from dataclasses import asdict, dataclass
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"
X_MARKER = b"in<"
Y_MARKER = b"in="
W_CONTEXT = b"in3\r\x00\x10\x0c"
H_CONTEXT = b"in,\x01\r\x03\x0c\x10"


@dataclass(frozen=True)
class GeometryRecord:
    index: int
    payload_offset: int
    x: int
    y: int
    width: int
    height: int


def extract_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    offset = raw.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"{path}: gzip payload not found")
    return gzip.decompress(raw[offset:])


def decode_scalar(payload: bytes, start: int) -> tuple[int, int]:
    if start >= len(payload) or payload[start] != 0x59:
        raise ValueError(f"expected scalar marker at {start:#x}")
    digits: list[str] = []
    pos = start + 1
    while pos < len(payload):
        value = payload[pos] ^ 0x54
        if not 0 <= value <= 9:
            break
        digits.append(str(value))
        pos += 1
    if not digits:
        raise ValueError(f"no encoded digits at {start:#x}")
    return int("".join(digits)), pos


def scan_records(payload: bytes) -> list[GeometryRecord]:
    records: list[GeometryRecord] = []
    search_from = 0

    while True:
        x_marker = payload.find(X_MARKER, search_from)
        if x_marker < 0:
            break
        search_from = x_marker + 1

        try:
            x, x_end = decode_scalar(payload, x_marker + len(X_MARKER))

            y_marker = payload.find(Y_MARKER, x_end, x_end + 24)
            if y_marker < 0:
                continue
            y, y_end = decode_scalar(payload, y_marker + len(Y_MARKER))

            w_marker = payload.find(W_CONTEXT, y_end, y_end + 40)
            if w_marker < 0:
                continue
            width, w_end = decode_scalar(payload, w_marker + len(W_CONTEXT))

            h_marker = payload.find(H_CONTEXT, w_end, w_end + 40)
            if h_marker < 0:
                continue
            height, _ = decode_scalar(payload, h_marker + len(H_CONTEXT))
        except ValueError:
            continue

        records.append(
            GeometryRecord(
                index=len(records),
                payload_offset=x_marker,
                x=x,
                y=y,
                width=width,
                height=height,
            )
        )

    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    records = scan_records(extract_payload(args.archive))
    if not records:
        raise SystemExit("No confirmed geometry records found")

    if args.json:
        print(json.dumps([asdict(record) for record in records], indent=2))
    else:
        print(f"{args.archive}: {len(records)} geometry records")
        for record in records:
            print(
                f"  [{record.index}] offset=0x{record.payload_offset:X} "
                f"x={record.x} y={record.y} w={record.width} h={record.height}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
