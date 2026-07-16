#!/usr/bin/env python3
"""Guarded same-length DOPSoft object-property rewriter.

Supported confirmed property mappings:
- numeric display/input address (record 0)
- four-character ASCII caption (record 1)
- single-digit command address (record 2)
- single-digit screen target, stored twice (record 3)

The tool anchors every write to the selected object's confirmed geometry record
and refuses replacements that change serialized length.
"""
from __future__ import annotations

import argparse
import gzip
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"
DIGIT_XOR = 0x54
TEXT_XOR = 0x64
GEOMETRY_MARKER = b"in<"


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
    width_context = bytes.fromhex("696e330d00100c")
    height_context = bytes.fromhex("696e2c010d030c10")

    while True:
        position = payload.find(GEOMETRY_MARKER, position)
        if position < 0:
            break
        try:
            x, x_end = decode_scalar(payload, position + 3)
            y_marker = payload.find(b"in=", x_end, x_end + 100)
            y, y_end = decode_scalar(payload, y_marker + 3)
            width_marker = payload.find(width_context, y_end, y_end + 200)
            width, width_end = decode_scalar(payload, width_marker + len(width_context))
            height_marker = payload.find(height_context, width_end, width_end + 200)
            height, _ = decode_scalar(payload, height_marker + len(height_context))
            records.append((position, x, y, width, height))
        except Exception:
            pass
        position += 1
    return records


def encode_digits(value: int) -> bytes:
    if value < 0:
        raise ValueError("value must be non-negative")
    return bytes(int(character) ^ DIGIT_XOR for character in str(value))


def encode_caption(value: str) -> bytes:
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("caption must contain ASCII characters only") from exc

    encoded = bytearray()
    for byte in raw:
        encoded.extend((byte ^ TEXT_XOR, 0x64))
    return bytes(encoded)


def rewrite(source: Path, destination: Path, property_type: str, value: str) -> None:
    archive = source.read_bytes()
    gzip_offset = archive.find(GZIP_MAGIC)
    if gzip_offset < 0:
        raise ValueError("gzip payload not found")

    preview = archive[:gzip_offset]
    payload = bytearray(gzip.decompress(archive[gzip_offset:]))
    records = geometry_records(payload)
    record_index = {
        "numeric_address": 0,
        "caption": 1,
        "command_address": 2,
        "screen_target": 3,
    }[property_type]

    if len(records) <= record_index:
        raise ValueError(f"template lacks geometry record {record_index}")
    anchor = records[record_index][0]

    if property_type == "numeric_address":
        start = anchor + 177
        replacement = encode_digits(int(value))
        if len(replacement) != 2:
            raise ValueError("numeric address must remain two digits")
        payload[start : start + 2] = replacement

    elif property_type == "caption":
        start = anchor + 1047
        replacement = encode_caption(value)
        if len(replacement) != 8:
            raise ValueError("caption must be exactly four ASCII characters")
        payload[start : start + 8] = replacement

    elif property_type == "command_address":
        start = anchor + 189
        replacement = encode_digits(int(value))
        if len(replacement) != 1:
            raise ValueError("command address must remain one digit")
        payload[start : start + 1] = replacement

    else:
        replacement = encode_digits(int(value))
        if len(replacement) != 1:
            raise ValueError("screen target must remain one digit")
        for relative_offset in (643, 659):
            payload[anchor + relative_offset : anchor + relative_offset + 1] = replacement

    rewritten_payload = bytes(payload)
    rebuilt = preview + gzip.compress(rewritten_payload, compresslevel=9, mtime=0)
    destination.write_bytes(rebuilt)

    verification = destination.read_bytes()
    verification_offset = verification.find(GZIP_MAGIC)
    if gzip.decompress(verification[verification_offset:]) != rewritten_payload:
        raise RuntimeError("rebuilt archive failed round-trip verification")

    print(f"property: {property_type}")
    print(f"value: {value}")
    print(f"record: {record_index}")
    print(f"output: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "property_type",
        choices=["numeric_address", "caption", "command_address", "screen_target"],
    )
    parser.add_argument("value")
    arguments = parser.parse_args()
    rewrite(
        arguments.source,
        arguments.destination,
        arguments.property_type,
        arguments.value,
    )


if __name__ == "__main__":
    main()
