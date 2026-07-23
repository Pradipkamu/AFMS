#!/usr/bin/env python3
"""Analyze sequential DOPSoft geometry edits.

This tool compares an ordered chain of .dpa files where each file was saved
from the previous one after changing exactly one geometry field. It avoids the
incorrect assumption that every sample was derived independently from the base.

Example:
    python analyze_sequential_geometry.py \
      DOP107CV_Base.dop.dpa \
      DOP107CV_X70.dpa \
      DOP107CV_Y90.dpa \
      DOP107CV_W100.dpa \
      DOP107CV_H95.dpa
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


@dataclass
class DiffRun:
    start: int
    end: int
    length: int
    before_hex: str
    after_hex: str


@dataclass
class PairReport:
    before: str
    after: str
    before_size: int
    after_size: int
    size_delta: int
    common_prefix: int
    common_suffix: int
    changed_bytes_same_length: int | None
    diff_run_count: int | None
    diff_runs: list[DiffRun]
    repeated_run_signatures: dict[str, list[int]]


def extract_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    offset = raw.find(GZIP_MAGIC)
    if offset < 0:
        raise ValueError(f"No GZIP payload found in {path}")
    return gzip.decompress(raw[offset:])


def common_prefix(a: bytes, b: bytes) -> int:
    limit = min(len(a), len(b))
    index = 0
    while index < limit and a[index] == b[index]:
        index += 1
    return index


def common_suffix(a: bytes, b: bytes, prefix: int) -> int:
    limit = min(len(a), len(b)) - prefix
    count = 0
    while count < limit and a[-1 - count] == b[-1 - count]:
        count += 1
    return count


def equal_length_diff_runs(a: bytes, b: bytes) -> list[DiffRun]:
    if len(a) != len(b):
        return []

    runs: list[DiffRun] = []
    start: int | None = None

    for index, (before, after) in enumerate(zip(a, b)):
        if before != after and start is None:
            start = index
        elif before == after and start is not None:
            runs.append(
                DiffRun(
                    start=start,
                    end=index,
                    length=index - start,
                    before_hex=a[start:index].hex(),
                    after_hex=b[start:index].hex(),
                )
            )
            start = None

    if start is not None:
        runs.append(
            DiffRun(
                start=start,
                end=len(a),
                length=len(a) - start,
                before_hex=a[start:].hex(),
                after_hex=b[start:].hex(),
            )
        )

    return runs


def repeated_signatures(runs: Iterable[DiffRun], minimum_length: int = 4) -> dict[str, list[int]]:
    positions: dict[str, list[int]] = {}
    for run in runs:
        if run.length < minimum_length:
            continue
        signature = f"{run.before_hex}->{run.after_hex}"
        positions.setdefault(signature, []).append(run.start)
    return {key: value for key, value in positions.items() if len(value) > 1}


def compare_pair(before_path: Path, after_path: Path) -> PairReport:
    before = extract_payload(before_path)
    after = extract_payload(after_path)
    prefix = common_prefix(before, after)
    suffix = common_suffix(before, after, prefix)
    runs = equal_length_diff_runs(before, after)

    changed = None
    run_count = None
    if len(before) == len(after):
        changed = sum(1 for x, y in zip(before, after) if x != y)
        run_count = len(runs)

    return PairReport(
        before=before_path.name,
        after=after_path.name,
        before_size=len(before),
        after_size=len(after),
        size_delta=len(after) - len(before),
        common_prefix=prefix,
        common_suffix=suffix,
        changed_bytes_same_length=changed,
        diff_run_count=run_count,
        diff_runs=runs,
        repeated_run_signatures=repeated_signatures(runs),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path, help="Ordered .dpa edit chain")
    parser.add_argument("--json", type=Path, dest="json_path")
    parser.add_argument("--max-runs", type=int, default=20)
    args = parser.parse_args()

    if len(args.files) < 2:
        parser.error("At least two ordered files are required")

    reports = [compare_pair(a, b) for a, b in zip(args.files, args.files[1:])]

    for report in reports:
        print(f"\n{report.before} -> {report.after}")
        print(f"  payload: {report.before_size} -> {report.after_size} ({report.size_delta:+d})")
        print(f"  common prefix: {report.common_prefix}")
        print(f"  common suffix: {report.common_suffix}")
        if report.changed_bytes_same_length is not None:
            print(f"  changed bytes: {report.changed_bytes_same_length}")
            print(f"  diff runs: {report.diff_run_count}")
            for run in report.diff_runs[: args.max_runs]:
                print(f"    {run.start:6d}-{run.end:6d} len={run.length:3d}")
            print(f"  repeated signatures: {len(report.repeated_run_signatures)}")
        else:
            print("  variable-length transition: use anchor/resynchronization analysis")

    if args.json_path:
        serializable = []
        for report in reports:
            item = asdict(report)
            serializable.append(item)
        args.json_path.write_text(json.dumps(serializable, indent=2), encoding="utf-8")
        print(f"\nWrote {args.json_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
