#!/usr/bin/env python3
"""Confirmed codecs for selected DOPSoft object properties.

Mappings verified from controlled DOP-107CV V1010 samples:

- Decimal address/target digits: encoded_byte = digit ^ 0x54
- Caption characters: encoded_byte = ASCII(character) ^ 0x64
  Caption bytes are stored in alternating positions in the observed record.

These codecs cover the scalar values only. Object-record discovery and cache
updates remain separate concerns.
"""
from __future__ import annotations

DIGIT_XOR = 0x54
TEXT_XOR = 0x64


def encode_decimal_digits(value: int) -> bytes:
    if value < 0:
        raise ValueError("value must be non-negative")
    return bytes((int(ch) ^ DIGIT_XOR) for ch in str(value))


def decode_decimal_digits(data: bytes) -> int:
    if not data:
        raise ValueError("encoded decimal value is empty")
    digits: list[str] = []
    for byte in data:
        digit = byte ^ DIGIT_XOR
        if not 0 <= digit <= 9:
            raise ValueError(f"invalid encoded decimal byte 0x{byte:02x}")
        digits.append(str(digit))
    return int("".join(digits))


def encode_caption(text: str) -> bytes:
    try:
        raw = text.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("current DOPSoft caption codec supports ASCII only") from exc
    return bytes(byte ^ TEXT_XOR for byte in raw)


def decode_caption(data: bytes) -> str:
    return bytes(byte ^ TEXT_XOR for byte in data).decode("ascii")


def encode_interleaved_caption(text: str, separator: int = 0x00) -> bytes:
    encoded = encode_caption(text)
    output = bytearray()
    for byte in encoded:
        output.extend((byte, separator))
    return bytes(output)


def decode_interleaved_caption(data: bytes) -> str:
    if len(data) % 2:
        raise ValueError("interleaved caption must contain an even number of bytes")
    return decode_caption(data[0::2])


def self_test() -> None:
    assert encode_decimal_digits(1) == bytes.fromhex("55")
    assert encode_decimal_digits(2) == bytes.fromhex("56")
    assert encode_decimal_digits(10) == bytes.fromhex("55 54")
    assert encode_decimal_digits(20) == bytes.fromhex("56 54")
    assert decode_decimal_digits(bytes.fromhex("55 54")) == 10
    assert decode_decimal_digits(bytes.fromhex("56 54")) == 20

    assert encode_caption("AFMS") == bytes.fromhex("25 22 29 37")
    assert encode_caption("Test") == bytes.fromhex("30 01 17 10")
    assert decode_caption(bytes.fromhex("25 22 29 37")) == "AFMS"
    assert decode_caption(bytes.fromhex("30 01 17 10")) == "Test"


if __name__ == "__main__":
    self_test()
    print("property scalar codec: all confirmed vectors passed")
