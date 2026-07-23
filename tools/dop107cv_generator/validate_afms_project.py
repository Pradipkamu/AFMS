#!/usr/bin/env python3
"""Validate AFMS DOP-107CV project, screen, tag, and navigation definitions."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

WIDTH = 800
HEIGHT = 480
GEOMETRY_FIELDS = ("x", "y", "width", "height")
TAG_FIELDS = ("tag", "tag_low", "tag_high")


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc


def collect_tag_names(register_data: Any) -> set[str]:
    names: set[str] = set()
    if isinstance(register_data, dict):
        for key, value in register_data.items():
            if isinstance(value, dict):
                candidate = value.get("name")
                if isinstance(candidate, str):
                    names.add(candidate)
                names.update(collect_tag_names(value))
            elif isinstance(value, list):
                for item in value:
                    names.update(collect_tag_names(item))
            elif key.startswith(("Command", "Status")):
                names.add(key)
    elif isinstance(register_data, list):
        for item in register_data:
            names.update(collect_tag_names(item))
    return names


def collect_screen_numbers(project: dict[str, Any]) -> set[int]:
    numbers: set[int] = set()
    screens = project.get("screens", [])
    if isinstance(screens, list):
        for item in screens:
            if isinstance(item, dict) and isinstance(item.get("number"), int):
                numbers.add(item["number"])
    return numbers


def validate_screen(path: Path, valid_tags: set[str], valid_screens: set[int]) -> list[str]:
    data = load_json(path)
    errors: list[str] = []
    screen = data.get("screen") if isinstance(data, dict) else None
    if not isinstance(screen, dict):
        return [f"{path}: missing screen object"]

    number = screen.get("number")
    if not isinstance(number, int):
        errors.append(f"{path}: screen.number must be an integer")
    elif valid_screens and number not in valid_screens:
        errors.append(f"{path}: screen {number} is not declared by the project")

    objects = screen.get("objects")
    if not isinstance(objects, list):
        return errors + [f"{path}: screen.objects must be an array"]

    seen_ids: set[str] = set()
    for index, obj in enumerate(objects):
        prefix = f"{path}: objects[{index}]"
        if not isinstance(obj, dict):
            errors.append(f"{prefix} must be an object")
            continue

        object_id = obj.get("id")
        if not isinstance(object_id, str) or not object_id:
            errors.append(f"{prefix}.id must be a non-empty string")
        elif object_id in seen_ids:
            errors.append(f"{prefix}.id duplicates {object_id!r}")
        else:
            seen_ids.add(object_id)

        for field in GEOMETRY_FIELDS:
            if not isinstance(obj.get(field), int):
                errors.append(f"{prefix}.{field} must be an integer")
        if all(isinstance(obj.get(field), int) for field in GEOMETRY_FIELDS):
            x, y, width, height = (obj[field] for field in GEOMETRY_FIELDS)
            if x < 0 or y < 0 or width <= 0 or height <= 0:
                errors.append(f"{prefix}: invalid geometry {x},{y},{width},{height}")
            if x + width > WIDTH or y + height > HEIGHT:
                errors.append(f"{prefix}: object exceeds {WIDTH}x{HEIGHT} screen")

        for field in TAG_FIELDS:
            tag = obj.get(field)
            if tag is not None and tag not in valid_tags:
                errors.append(f"{prefix}.{field} references unknown AFMS tag {tag!r}")

        target = obj.get("target_screen")
        if target is not None:
            if not isinstance(target, int):
                errors.append(f"{prefix}.target_screen must be an integer")
            elif valid_screens and target not in valid_screens:
                errors.append(f"{prefix}.target_screen references undeclared screen {target}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--registers", type=Path, required=True)
    parser.add_argument("screens", nargs="+", type=Path)
    args = parser.parse_args()

    try:
        project = load_json(args.project)
        registers = load_json(args.registers)
        if not isinstance(project, dict):
            raise ValueError("project root must be an object")
        valid_tags = collect_tag_names(registers)
        valid_screens = collect_screen_numbers(project)
        errors: list[str] = []
        for screen_path in args.screens:
            errors.extend(validate_screen(screen_path, valid_tags, valid_screens))
    except ValueError as exc:
        print(f"ERROR: {exc}")
        return 2

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print(
        f"AFMS project validation passed: {len(args.screens)} screen(s), "
        f"{len(valid_tags)} tags, {len(valid_screens)} declared screens."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
