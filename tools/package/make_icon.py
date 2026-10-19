#!/usr/bin/env python3
"""Dibuja el icono de MC-WEB (un cubo isométrico en pixel art, propio) en tools/package/mcweb.png. Solo usa la biblioteca estándar."""
import struct
import sys
import zlib
from pathlib import Path

N = 16        # el dibujo mide 16x16 "píxeles grandes"
SCALE = 16    # cada uno se amplía a 16x16 (el icono final es de 256x256)


def noise(x, y, k):
    h = (x * 374761393 + y * 668265263 + k * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 255


def shade(c, d):
    return tuple(max(0, min(255, v + d)) for v in c) + (255,)


GRASS, DIRT_L, DIRT_R = (88, 170, 60), (139, 92, 52), (101, 66, 36)
img = [[(0, 0, 0, 0)] * N for _ in range(N)]

# Cara de arriba: un rombo de 16 de ancho (las filas 3 y 4 son las más anchas)
for r in range(8):
    half = (r + 1) * 2 if r <= 3 else (8 - r) * 2
    for x in range(8 - half, 8 + half):
        img[r][x] = shade(GRASS, noise(x, r, 1) % 21 - 10)
# Caras laterales: 8 de alto, bajando hacia el centro; los dos primeros píxeles de arriba son hierba que cae
for x in range(N):
    left = x < 8
    top_y = 5 + (x // 2 if left else (15 - x) // 2)
    for k in range(8):
        y = top_y + k
        if k < 2:
            base = (70, 140, 48) if not left else (82, 160, 56)
        else:
            base = DIRT_L if left else DIRT_R
        img[y][x] = shade(base, noise(x, k, 2 if left else 3) % 25 - 14)


def write_png(path, rows):
    raw = bytearray()
    for row in rows:
        line = b"".join(bytes(px) * SCALE for px in row)
        raw += (b"\x00" + line) * SCALE

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", N * SCALE, N * SCALE, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    Path(path).write_bytes(out)


dest = Path(__file__).with_name("mcweb.png")
write_png(dest, img)
print("icono escrito en", dest, file=sys.stderr)
