#!/usr/bin/env python3
"""Experimentally rewrite one DOPSoft geometry record in a .dpa archive.

The tool only edits the confirmed scalar cluster and refuses ambiguous files.
It rebuilds the gzip payload while preserving the original preview bytes.

Current safety limitation:
- Only same-digit-count replacements are enabled by default.
- This avoids changing serialized payload length until all dependent length
  fields and caches are fully mapped.
"""
from __future__ import annotations

import argparse
import gzip
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"
GEOMETRY_KEYS = {
    "x": b"in<",
    "y": b"in=",
    "w": b"in3",
    "h": b"in,",
}


def encode_scalar(value: int) -> bytes:
    if value < 0:
        raise ValueError("geometry values must be non-negative")
    return bytes([0x59, *[(ord(ch) - 0x30) ^ 0x54 for ch in str(value)]])


def decode_scalar(payload: bytes, start: int) -> tuple[int, int]:
    if payload[start] != 0x59:
        raise ValueError(f"expected scalar prefix 0x59 at offset {start:#x}")
    pos = start + 1
    digits: list[str] = []
    while pos < len(payload):
        decoded = payload[pos] ^ 0x54
        if 0 <= decoded <= 9:
            digits.append(chr(decoded + 0x30))
            pos += 1
            continue
        break
    if not digits:
        raise ValueError(f"no scalar digits found at offset {start:#x}")
    return int("".join(digits)), pos


def find_scalar(payload: bytes, field: str) -> tuple[int, int, int]:
    marker = GEOMETRY_KEYS[field]
    hits = []
    pos = 0
    while True:
        pos = payload.find(marker, pos)
        if pos < 0:
            break
        scalar_start = pos + len(marker)
        if scalar_start < len(payload) and payload[scalar_start] == 0x59:
            try:
                value, scalar_end = decode_scalar(payload, scalar_start)
                hits.append((scalar_start, scalar_end, value))
            except ValueError:
                pass
        pos += 1
    if len(hits) != 1:
        raise RuntimeError(
            f"expected exactly one confirmed {field!r} scalar, found {len(hits)}"
        )
    return hits[0]


def rewrite_geometry(source: Path, destination: Path, field: str, value: int) -> None:
    raw = source.read_bytes()
    gzip_offset = raw.find(GZIP_MAGIC)
    if gzip_offset < 0:
        raise RuntimeError("gzip payload not found")

    preview = raw[:gzip_offset]
    payload = gzip.decompress(raw[gzip_offset:])
    start, end, old_value = find_scalar(payload, field)
    replacement = encode_scalar(value)

    if len(replacement) != end - start:
        raise RuntimeError(
            "replacement changes serialized length; use a value with the same "
            "number of decimal digits until length-field rewriting is validated"
        )

    rewritten = payload[:start] + replacement + payload[end:]
    if len(rewritten) != len(payload):
        raise AssertionError("same-length rewrite unexpectedly changed payload size")

    rebuilt = preview + gzip.compress(rewritten, compresslevel=9, mtime=0)
    destination.write_bytes(rebuilt)

    check = destination.read_bytes()
    check_offset = check.find(GZIP_MAGIC)
    verified = gzip.decompress(check[check_offset:])
    if verified != rewritten:
        raise RuntimeError("rebuilt archive failed round-trip verification")

    print(f"field: {field}")
    print(f"old value: {old_value}")
    print(f"new value: {value}")
    print(f"payload offset: {start:#x}")
    print(f"output: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("field", choices=sorted(GEOMETRY_KEYS))
    parser.add_argument("value", type=int)
    args = parser.parse_args()
    rewrite_geometry(args.source, args.destination, args.field, args.value)


if __name__ == "__main__":
    main()
