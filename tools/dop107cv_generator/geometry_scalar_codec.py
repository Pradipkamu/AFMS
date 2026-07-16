#!/usr/bin/env python3
"""Decode and encode DOPSoft decimal geometry scalars.

Confirmed from the DOP-107CV sample chain:
- Every scalar starts with marker byte 0x59.
- Each following byte encodes one decimal digit as: encoded = digit ^ 0x54.

Examples:
    59 53 54       -> 70
    59 5d 54       -> 90
    59 55 54 54    -> 100
    59 5d 51       -> 95
"""

from __future__ import annotations

MARKER = 0x59
DIGIT_XOR = 0x54


def decode_scalar(data: bytes) -> int:
    if not data or data[0] != MARKER:
        raise ValueError("geometry scalar must start with 0x59")

    digits: list[str] = []
    for byte in data[1:]:
        value = byte ^ DIGIT_XOR
        if not 0 <= value <= 9:
            raise ValueError(f"invalid encoded digit byte 0x{byte:02x}")
        digits.append(str(value))

    if not digits:
        raise ValueError("geometry scalar contains no digits")

    return int("".join(digits))


def encode_scalar(value: int) -> bytes:
    if value < 0:
        raise ValueError("negative geometry values are not supported")

    digits = str(value)
    return bytes([MARKER, *(int(ch) ^ DIGIT_XOR for ch in digits)])


def self_test() -> None:
    vectors = {
        bytes.fromhex("59 51 5c"): 58,
        bytes.fromhex("59 53 5c"): 78,
        bytes.fromhex("59 5c 53"): 87,
        bytes.fromhex("59 5c 50"): 84,
        bytes.fromhex("59 53 54"): 70,
        bytes.fromhex("59 5d 54"): 90,
        bytes.fromhex("59 55 54 54"): 100,
        bytes.fromhex("59 5d 51"): 95,
    }

    for encoded, expected in vectors.items():
        decoded = decode_scalar(encoded)
        assert decoded == expected, (encoded.hex(), decoded, expected)
        assert encode_scalar(expected) == encoded


if __name__ == "__main__":
    self_test()
    print("geometry scalar codec: all confirmed vectors passed")
