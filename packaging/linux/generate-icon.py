#!/usr/bin/env python3
"""Write a 256x256 PNG icon next to this script (no third-party deps)."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

SIZE = 256


def pixel(x: int, y: int) -> tuple[int, int, int, int]:
    cx, cy, r = 127.5, 127.5, 92
    dx, dy = x - cx, y - cy
    dist = (dx * dx + dy * dy) ** 0.5
    if dist <= 74:
        # inner disc
        wave = 40 * abs(((x + y * 3) % 50) - 25) / 25
        return (26, 18, 12, 255) if abs(y - (128 + wave - 20)) > 6 else (255, 176, 102, 255)
    if dist <= r:
        return (196, 92, 18, 255)
    # rounded square background
    rx, ry = min(x, SIZE - 1 - x), min(y, SIZE - 1 - y)
    if rx < 8 or ry < 8:
        return (0, 0, 0, 0)
    return (26, 18, 12, 255)


def write_png(path: Path) -> None:
    rows = []
    for y in range(SIZE):
        row = b"\x00"
        for x in range(SIZE):
            row += bytes(pixel(x, y))
        rows.append(row)
    raw = b"".join(rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


if __name__ == "__main__":
    out = Path(__file__).with_name("icons")
    out.mkdir(parents=True, exist_ok=True)
    dest = out / "org.ocrcatalog.OCRCatalog.png"
    write_png(dest)
    print(dest, dest.stat().st_size)
