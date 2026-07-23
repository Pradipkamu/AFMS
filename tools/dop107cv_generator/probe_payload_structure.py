#!/usr/bin/env python3
"""Probe DOPSoft .dpa payload structure using local resynchronization.

This tool is intentionally read-only. It extracts the gzip payload from each
archive, reports stable prefix/suffix regions, and searches for equal anchor
blocks that reappear after insertions/deletions. The output helps identify
candidate record boundaries without assuming unsafe absolute offsets.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable

GZIP_MAGIC = b"\x1f\x8b\x08"


@dataclass(frozen=True)
class Anchor:
    base_offset: int
    sample_offset: int
    length: int
    shift: int
    sha256: str


def extract_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    offset = raw.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"{path}: gzip payload not found")
    return gzip.decompress(raw[offset:])


def common_prefix(a: bytes, b: bytes) -> int:
    limit = min(len(a), len(b))
    i = 0
    while i < limit and a[i] == b[i]:
        i += 1
    return i


def common_suffix(a: bytes, b: bytes, prefix: int = 0) -> int:
    limit = min(len(a), len(b)) - prefix
    i = 0
    while i < limit and a[-1 - i] == b[-1 - i]:
        i += 1
    return i


def anchor_index(data: bytes, block_size: int) -> dict[bytes, list[int]]:
    result: dict[bytes, list[int]] = {}
    for offset in range(0, len(data) - block_size + 1, block_size):
        block = data[offset : offset + block_size]
        result.setdefault(block, []).append(offset)
    return result


def find_anchors(base: bytes, sample: bytes, block_size: int, search_radius: int) -> list[Anchor]:
    index = anchor_index(sample, block_size)
    anchors: list[Anchor] = []
    last_sample = -1

    for base_offset in range(0, len(base) - block_size + 1, block_size):
        block = base[base_offset : base_offset + block_size]
        candidates = index.get(block, [])
        if not candidates:
            continue

        nearby = [
            offset
            for offset in candidates
            if offset > last_sample and abs(offset - base_offset) <= search_radius
        ]
        if not nearby:
            continue

        sample_offset = min(nearby, key=lambda offset: abs(offset - base_offset))
        last_sample = sample_offset
        anchors.append(
            Anchor(
                base_offset=base_offset,
                sample_offset=sample_offset,
                length=block_size,
                shift=sample_offset - base_offset,
                sha256=hashlib.sha256(block).hexdigest(),
            )
        )

    return anchors


def summarize_shifts(anchors: Iterable[Anchor]) -> list[dict[str, int]]:
    runs: list[dict[str, int]] = []
    for anchor in anchors:
        if not runs or runs[-1]["shift"] != anchor.shift:
            runs.append(
                {
                    "shift": anchor.shift,
                    "first_base_offset": anchor.base_offset,
                    "last_base_offset": anchor.base_offset,
                    "anchor_count": 1,
                }
            )
        else:
            runs[-1]["last_base_offset"] = anchor.base_offset
            runs[-1]["anchor_count"] += 1
    return runs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=Path)
    parser.add_argument("samples", nargs="+", type=Path)
    parser.add_argument("--block-size", type=int, default=32)
    parser.add_argument("--search-radius", type=int, default=4096)
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()

    if args.block_size < 8:
        parser.error("--block-size must be at least 8")

    base = extract_payload(args.base)
    report: dict[str, object] = {
        "base": str(args.base),
        "base_size": len(base),
        "base_sha256": hashlib.sha256(base).hexdigest(),
        "block_size": args.block_size,
        "search_radius": args.search_radius,
        "samples": [],
    }

    for sample_path in args.samples:
        sample = extract_payload(sample_path)
        prefix = common_prefix(base, sample)
        suffix = common_suffix(base, sample, prefix)
        anchors = find_anchors(base, sample, args.block_size, args.search_radius)
        item = {
            "path": str(sample_path),
            "size": len(sample),
            "sha256": hashlib.sha256(sample).hexdigest(),
            "common_prefix": prefix,
            "common_suffix": suffix,
            "base_changed_span": len(base) - prefix - suffix,
            "sample_changed_span": len(sample) - prefix - suffix,
            "anchor_count": len(anchors),
            "shift_runs": summarize_shifts(anchors),
            "anchors": [asdict(anchor) for anchor in anchors],
        }
        report["samples"].append(item)  # type: ignore[index]

        print(f"{sample_path.name}:")
        print(f"  payload size: {len(sample)}")
        print(f"  common prefix/suffix: {prefix}/{suffix}")
        print(f"  anchors: {len(anchors)}")
        for run in item["shift_runs"][:12]:
            print(
                "  shift {shift:+d}: base {first_base_offset}..{last_base_offset} "
                "({anchor_count} anchors)".format(**run)
            )

    if args.json_path:
        args.json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote {args.json_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
