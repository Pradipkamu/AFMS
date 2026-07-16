#!/usr/bin/env python3
"""Analyze one controlled DOPSoft object-property edit.

The input pair must differ by exactly one user-visible property, such as:
- caption text
- Modbus register address
- button action
- target screen

The tool extracts the embedded gzip payload, computes aligned change runs,
searches for repeated before/after signatures, and emits a JSON report. It is
read-only and intentionally does not patch undocumented fields.
"""
from __future__ import annotations

import argparse
import difflib
import gzip
import hashlib
import json
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"


def extract_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    offset = raw.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"{path}: gzip payload not found")
    return gzip.decompress(raw[offset:])


def printable(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b <= 126 else "." for b in data)


def operations(before: bytes, after: bytes) -> list[dict[str, object]]:
    matcher = difflib.SequenceMatcher(None, before, after, autojunk=False)
    result: list[dict[str, object]] = []
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            continue
        old = before[i1:i2]
        new = after[j1:j2]
        result.append(
            {
                "type": tag,
                "before_start": i1,
                "before_end": i2,
                "after_start": j1,
                "after_end": j2,
                "before_length": len(old),
                "after_length": len(new),
                "before_hex": old.hex(),
                "after_hex": new.hex(),
                "before_ascii": printable(old),
                "after_ascii": printable(new),
            }
        )
    return result


def repeated_changes(before: bytes, after: bytes, ops: list[dict[str, object]]) -> list[dict[str, object]]:
    repeated: list[dict[str, object]] = []
    for op in ops:
        old = bytes.fromhex(str(op["before_hex"]))
        new = bytes.fromhex(str(op["after_hex"]))
        if not old or len(old) > 64:
            continue
        old_hits = []
        new_hits = []
        start = 0
        while True:
            pos = before.find(old, start)
            if pos < 0:
                break
            old_hits.append(pos)
            start = pos + 1
        start = 0
        while True:
            pos = after.find(new, start)
            if pos < 0:
                break
            new_hits.append(pos)
            start = pos + 1
        if len(old_hits) > 1 or len(new_hits) > 1:
            repeated.append(
                {
                    "before_hex": old.hex(),
                    "after_hex": new.hex(),
                    "before_offsets": old_hits,
                    "after_offsets": new_hits,
                }
            )
    return repeated


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--property", required=True)
    parser.add_argument("--object-record", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    before = extract_payload(args.before)
    after = extract_payload(args.after)
    ops = operations(before, after)
    report = {
        "property": args.property,
        "object_record": args.object_record,
        "before_file": str(args.before),
        "after_file": str(args.after),
        "before_size": len(before),
        "after_size": len(after),
        "size_delta": len(after) - len(before),
        "before_sha256": hashlib.sha256(before).hexdigest(),
        "after_sha256": hashlib.sha256(after).hexdigest(),
        "operation_count": len(ops),
        "operations": ops,
        "repeated_changes": repeated_changes(before, after, ops),
    }

    encoded = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
        print(f"Wrote {args.output}")
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
