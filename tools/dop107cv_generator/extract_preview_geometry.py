#!/usr/bin/env python3
"""Extract coarse object geometry from a Delta DOPSoft .dpa preview bitmap.

The DOPSoft .dpa files examined for the DOP-107CV contain a 164x98 BMP
preview at the beginning of the file, followed by the compressed editable
project payload. This helper detects connected non-background regions in that
preview so geometry samples can be checked before payload comparisons.

The preview is only a scaled rendering, so reported coordinates are preview
pixels rather than authoritative 800x480 HMI coordinates. Its main purpose is
to verify which object changed and whether a sample set is independent or a
sequential edit chain.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import deque
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


BMP_SIGNATURE = b"BM"
DOP107CV_WIDTH = 800
DOP107CV_HEIGHT = 480


@dataclass(frozen=True)
class Component:
    x: int
    y: int
    width: int
    height: int
    pixels: int

    @property
    def right(self) -> int:
        return self.x + self.width

    @property
    def bottom(self) -> int:
        return self.y + self.height


def read_bmp_prefix(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != BMP_SIGNATURE:
        raise ValueError(f"{path}: file does not begin with a BMP header")
    if len(data) < 14:
        raise ValueError(f"{path}: truncated BMP header")
    file_size = struct.unpack_from("<I", data, 2)[0]
    if file_size <= 0 or file_size > len(data):
        raise ValueError(f"{path}: invalid BMP size {file_size}")
    return data[:file_size]


def decode_bmp24(bmp: bytes) -> tuple[int, int, list[list[tuple[int, int, int]]]]:
    if bmp[:2] != BMP_SIGNATURE:
        raise ValueError("not a BMP")
    pixel_offset = struct.unpack_from("<I", bmp, 10)[0]
    dib_size = struct.unpack_from("<I", bmp, 14)[0]
    if dib_size < 40:
        raise ValueError(f"unsupported DIB header size {dib_size}")
    width = struct.unpack_from("<i", bmp, 18)[0]
    signed_height = struct.unpack_from("<i", bmp, 22)[0]
    planes = struct.unpack_from("<H", bmp, 26)[0]
    bpp = struct.unpack_from("<H", bmp, 28)[0]
    compression = struct.unpack_from("<I", bmp, 30)[0]
    if planes != 1 or bpp != 24 or compression != 0:
        raise ValueError(
            f"unsupported BMP format: planes={planes}, bpp={bpp}, compression={compression}"
        )
    top_down = signed_height < 0
    height = abs(signed_height)
    if width <= 0 or height <= 0:
        raise ValueError("invalid BMP dimensions")
    stride = ((width * 3 + 3) // 4) * 4
    pixels: list[list[tuple[int, int, int]]] = []
    for display_y in range(height):
        source_y = display_y if top_down else height - 1 - display_y
        row_start = pixel_offset + source_y * stride
        row: list[tuple[int, int, int]] = []
        for x in range(width):
            b, g, r = bmp[row_start + x * 3 : row_start + x * 3 + 3]
            row.append((r, g, b))
        pixels.append(row)
    return width, height, pixels


def foreground_mask(
    pixels: list[list[tuple[int, int, int]]], threshold: int = 245
) -> list[list[bool]]:
    return [
        [min(r, g, b) < threshold for r, g, b in row]
        for row in pixels
    ]


def connected_components(
    mask: list[list[bool]], min_pixels: int = 20
) -> list[Component]:
    height = len(mask)
    width = len(mask[0]) if height else 0
    seen = [[False] * width for _ in range(height)]
    components: list[Component] = []

    for y in range(height):
        for x in range(width):
            if not mask[y][x] or seen[y][x]:
                continue
            queue: deque[tuple[int, int]] = deque([(x, y)])
            seen[y][x] = True
            xs: list[int] = []
            ys: list[int] = []
            while queue:
                cx, cy = queue.popleft()
                xs.append(cx)
                ys.append(cy)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = cx + dx, cy + dy
                        if (
                            0 <= nx < width
                            and 0 <= ny < height
                            and mask[ny][nx]
                            and not seen[ny][nx]
                        ):
                            seen[ny][nx] = True
                            queue.append((nx, ny))
            if len(xs) >= min_pixels:
                components.append(
                    Component(
                        x=min(xs),
                        y=min(ys),
                        width=max(xs) - min(xs) + 1,
                        height=max(ys) - min(ys) + 1,
                        pixels=len(xs),
                    )
                )
    return sorted(components, key=lambda c: (c.y, c.x, c.width, c.height))


def scaled_estimate(component: Component, preview_width: int, preview_height: int) -> dict[str, float]:
    return {
        "x": component.x * DOP107CV_WIDTH / preview_width,
        "y": component.y * DOP107CV_HEIGHT / preview_height,
        "width": component.width * DOP107CV_WIDTH / preview_width,
        "height": component.height * DOP107CV_HEIGHT / preview_height,
    }


def analyse(path: Path, threshold: int, min_pixels: int) -> dict[str, object]:
    bmp = read_bmp_prefix(path)
    width, height, pixels = decode_bmp24(bmp)
    components = connected_components(foreground_mask(pixels, threshold), min_pixels)
    return {
        "file": str(path),
        "preview": {"width": width, "height": height, "bmp_bytes": len(bmp)},
        "components": [
            {
                **asdict(component),
                "estimated_hmi_geometry": scaled_estimate(component, width, height),
            }
            for component in components
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--threshold", type=int, default=245)
    parser.add_argument("--min-pixels", type=int, default=20)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    reports = [analyse(path, args.threshold, args.min_pixels) for path in args.files]
    if args.json:
        print(json.dumps(reports, indent=2))
        return 0

    for report in reports:
        preview = report["preview"]
        print(f"{report['file']}: {preview['width']}x{preview['height']} preview")
        for index, component in enumerate(report["components"], start=1):
            print(
                f"  {index}: x={component['x']} y={component['y']} "
                f"w={component['width']} h={component['height']} "
                f"pixels={component['pixels']}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
