#!/usr/bin/env python3
"""Extract enlarged preview crops for each decoded DOPSoft geometry record."""
from __future__ import annotations

import argparse
import gzip
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw

GZIP_MAGIC = b"\x1f\x8b\x08"
SCREEN_W = 800
SCREEN_H = 480


def read_preview(path: Path) -> Image.Image:
    raw = path.read_bytes()
    if raw[:2] != b"BM":
        raise ValueError("DPA does not begin with BMP preview")
    bmp_size = struct.unpack_from("<I", raw, 2)[0]
    tmp = path.with_suffix(path.suffix + ".preview.bmp")
    tmp.write_bytes(raw[:bmp_size])
    try:
        return Image.open(tmp).convert("RGB")
    finally:
        tmp.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scale", type=int, default=8)
    parser.add_argument("--margin", type=int, default=3)
    args = parser.parse_args()

    preview = read_preview(args.archive)
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    records = inventory.get("records", [])
    crops: list[Image.Image] = []

    for record in records:
        x, y = record["x"], record["y"]
        width, height = record["width"], record["height"]
        px = round(x * preview.width / SCREEN_W)
        py = round(y * preview.height / SCREEN_H)
        pw = max(1, round(width * preview.width / SCREEN_W))
        ph = max(1, round(height * preview.height / SCREEN_H))
        box = (
            max(0, px - args.margin),
            max(0, py - args.margin),
            min(preview.width, px + pw + args.margin),
            min(preview.height, py + ph + args.margin),
        )
        crop = preview.crop(box)
        crop = crop.resize((crop.width * args.scale, crop.height * args.scale))
        canvas = Image.new("RGB", (max(220, crop.width), crop.height + 30), "white")
        canvas.paste(crop, ((canvas.width - crop.width) // 2, 30))
        draw = ImageDraw.Draw(canvas)
        draw.text((5, 5), f"Record {record['index']}: {x},{y},{width},{height}", fill="black")
        crops.append(canvas)

    if not crops:
        raise ValueError("No records found in inventory")

    out_w = max(image.width for image in crops)
    out_h = sum(image.height for image in crops)
    output = Image.new("RGB", (out_w, out_h), "white")
    y_cursor = 0
    for crop in crops:
        output.paste(crop, (0, y_cursor))
        y_cursor += crop.height
    args.output.parent.mkdir(parents=True, exist_ok=True)
    output.save(args.output)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
