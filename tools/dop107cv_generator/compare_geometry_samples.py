#!/usr/bin/env python3
"""Compare controlled DOPSoft .dpa geometry samples.

The tool extracts each gzip-compressed editor payload, reports stable metadata,
and emits candidate changed regions against a base project. It intentionally
does not patch bytes: DOPSoft rewrites encoded regions when geometry changes,
so candidate regions must be decoded before safe generation is attempted.
"""

from __future__ import annotations

import argparse
import difflib
import gzip
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path

GZIP_MAGIC = b"\x1f\x8b\x08"


@dataclass(frozen=True)
class SampleInfo:
    name: str
    path: str
    archive_size: int
    preview_size: int
    payload_size: int
    payload_sha256: str
    common_prefix_with_base: int
    common_suffix_with_base: int
    changed_operation_count: int


def extract_payload(path: Path) -> tuple[bytes, bytes]:
    data = path.read_bytes()
    offset = data.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"No gzip payload found in {path}")
    preview = data[:offset]
    payload = gzip.decompress(data[offset:])
    return preview, payload


def common_prefix_length(a: bytes, b: bytes) -> int:
    count = 0
    for left, right in zip(a, b):
        if left != right:
            break
        count += 1
    return count


def common_suffix_length(a: bytes, b: bytes) -> int:
    count = 0
    for left, right in zip(reversed(a), reversed(b)):
        if left != right:
            break
        count += 1
    return count


def changed_operations(base: bytes, candidate: bytes) -> list[dict[str, object]]:
    matcher = difflib.SequenceMatcher(None, base, candidate, autojunk=True)
    operations: list[dict[str, object]] = []
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            continue
        operations.append(
            {
                "type": tag,
                "base_start": i1,
                "base_end": i2,
                "candidate_start": j1,
                "candidate_end": j2,
                "base_length": i2 - i1,
                "candidate_length": j2 - j1,
                "base_preview_hex": base[i1 : min(i2, i1 + 24)].hex(),
                "candidate_preview_hex": candidate[j1 : min(j2, j1 + 24)].hex(),
            }
        )
    return operations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("samples", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-operations", type=int, default=30)
    args = parser.parse_args()

    base_preview, base_payload = extract_payload(args.base)
    report: dict[str, object] = {
        "base": {
            "path": str(args.base),
            "archive_size": args.base.stat().st_size,
            "preview_size": len(base_preview),
            "payload_size": len(base_payload),
            "payload_sha256": hashlib.sha256(base_payload).hexdigest(),
        },
        "samples": [],
    }

    for sample_path in args.samples:
        preview, payload = extract_payload(sample_path)
        operations = changed_operations(base_payload, payload)
        info = SampleInfo(
            name=sample_path.stem,
            path=str(sample_path),
            archive_size=sample_path.stat().st_size,
            preview_size=len(preview),
            payload_size=len(payload),
            payload_sha256=hashlib.sha256(payload).hexdigest(),
            common_prefix_with_base=common_prefix_length(base_payload, payload),
            common_suffix_with_base=common_suffix_length(base_payload, payload),
            changed_operation_count=len(operations),
        )
        report["samples"].append(
            {
                **asdict(info),
                "candidate_operations": operations[: args.max_operations],
            }
        )

    encoded = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
