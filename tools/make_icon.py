#!/usr/bin/env python3
"""Draws the Gifu icon: a close-up of two diagonal intersections, Black then White over it.

Writes assets/icons/gifu.png (256x256) and packaging/windows/gifu.ico
(16..256, PNG-compressed entries). Standard library only; 4x supersampled.
"""
import math, os, struct, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def render(size):
    """Close-up of two diagonal intersections: Black on the upper-left one,
    White on the lower-right one, drawn over Black (slight overlap, soft shadow).
    Grid lines run off the edges so it reads as a zoomed-in board."""
    ss = 4
    n = size * ss
    px = [[(0, 0, 0, 0)] * n for _ in range(n)]
    radius = n * 0.20                       # rounded board corners
    board = (224, 185, 120)
    line = (62, 47, 31)
    a, b = n * 0.34, n * 0.66               # intersection coordinates
    w = max(1.0, n * 0.02)                  # line half-width (bold: it is a close-up)
    sr = n * 0.25                           # stone radius
    black = (a, a, (24, 22, 20), (70, 66, 60))
    white = (b, b, (250, 247, 240), (176, 168, 154))
    m = n * 0.04
    def inside_round_rect(x, y):
        cx = min(max(x, m + radius), n - m - radius)
        cy = min(max(y, m + radius), n - m - radius)
        return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 and m <= x <= n - m and m <= y <= n - m
    def stone_color(stone, x, y):
        sx, sy, fill, rim = stone
        d = math.hypot(x - sx, y - sy)
        if d > sr:
            return None
        h = max(0.0, 1.0 - math.hypot(x - (sx - sr * 0.35), y - (sy - sr * 0.35)) / (sr * 0.9))
        base = rim if d > sr * 0.93 else fill
        k = 0.28 if fill[0] < 128 else 0.12
        return tuple(min(255, int(base[i] + (255 - base[i]) * h * k)) for i in range(3))
    for y in range(n):
        for x in range(n):
            if not inside_round_rect(x, y):
                continue
            c = board
            if abs(x - a) <= w or abs(x - b) <= w or abs(y - a) <= w or abs(y - b) <= w:
                c = line
            # Black's shadow on the board, then Black.
            if math.hypot(x - (a + sr * 0.08), y - (a + sr * 0.10)) <= sr * 1.02:
                c = tuple(int(v * 0.72) for v in c)
            bc = stone_color(black, x, y)
            if bc: c = bc
            # White's shadow falls on the board and on Black, then White on top.
            if math.hypot(x - (b + sr * 0.08), y - (b + sr * 0.10)) <= sr * 1.02 and not stone_color(white, x, y):
                c = tuple(int(v * 0.65) for v in c)
            wc = stone_color(white, x, y)
            if wc: c = wc
            px[y][x] = c + (255,)
    # downsample
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0, 0, 0, 0]
            for dy in range(ss):
                for dx in range(ss):
                    p = px[y * ss + dy][x * ss + dx]
                    a = p[3]
                    acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a
            a = acc[3]
            row.append((acc[0] // a, acc[1] // a, acc[2] // a, a // (ss * ss)) if a else (0, 0, 0, 0))
        out.append(row)
    return out

def png_bytes(img):
    h, w = len(img), len(img[0])
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in img)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def ico_bytes(pngs):
    header = struct.pack("<HHH", 0, 1, len(pngs))
    entries, data, offset = b"", b"", 6 + 16 * len(pngs)
    for size, png in pngs:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
        data += png
        offset += len(png)
    return header + entries + data

if __name__ == "__main__":
    sizes = [16, 24, 32, 48, 64, 128, 256]
    pngs = [(s, png_bytes(render(s))) for s in sizes]
    open(os.path.join(ROOT, "assets", "icons", "gifu.png"), "wb").write(pngs[-1][1])
    open(os.path.join(ROOT, "packaging", "windows", "gifu.ico"), "wb").write(ico_bytes(pngs))
    print("icons written")
