#!/usr/bin/env python3
"""Validate AFMS screen manifests for the Delta DOP-107CV."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

SCREEN_WIDTH = 800
SCREEN_HEIGHT = 480
REQUIRED_FIELDS = ("id", "type", "x", "y", "width", "height")


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"Manifest not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON at line {exc.lineno}, column {exc.colno}: {exc.msg}") from exc


def validate(manifest: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    target = manifest.get("target", {})

    if target.get("model") != "DOP-107CV":
        errors.append("target.model must be 'DOP-107CV'")
    if target.get("width") != SCREEN_WIDTH or target.get("height") != SCREEN_HEIGHT:
        errors.append(f"target resolution must be {SCREEN_WIDTH}x{SCREEN_HEIGHT}")

    screen = manifest.get("screen")
    if not isinstance(screen, dict):
        return errors + ["screen must be an object"]

    objects = screen.get("objects")
    if not isinstance(objects, list):
        return errors + ["screen.objects must be an array"]

    seen_ids: set[str] = set()
    for index, obj in enumerate(objects):
        prefix = f"screen.objects[{index}]"
        if not isinstance(obj, dict):
            errors.append(f"{prefix} must be an object")
            continue

        for field in REQUIRED_FIELDS:
            if field not in obj:
                errors.append(f"{prefix}.{field} is required")

        object_id = obj.get("id")
        if isinstance(object_id, str):
            if object_id in seen_ids:
                errors.append(f"{prefix}.id duplicates '{object_id}'")
            seen_ids.add(object_id)
        else:
            errors.append(f"{prefix}.id must be a string")

        geometry = {name: obj.get(name) for name in ("x", "y", "width", "height")}
        if not all(isinstance(value, int) for value in geometry.values()):
            errors.append(f"{prefix} geometry values must be integers")
            continue

        x = geometry["x"]
        y = geometry["y"]
        width = geometry["width"]
        height = geometry["height"]

        if x < 0 or y < 0:
            errors.append(f"{prefix} x and y must be non-negative")
        if width <= 0 or height <= 0:
            errors.append(f"{prefix} width and height must be greater than zero")
        if x + width > SCREEN_WIDTH:
            errors.append(f"{prefix} exceeds screen width: x + width = {x + width}")
        if y + height > SCREEN_HEIGHT:
            errors.append(f"{prefix} exceeds screen height: y + height = {y + height}")

    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: validate_layout.py <manifest.json>", file=sys.stderr)
        return 2

    try:
        manifest = load_manifest(Path(sys.argv[1]))
        errors = validate(manifest)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print("Layout is valid for Delta DOP-107CV (800x480).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
