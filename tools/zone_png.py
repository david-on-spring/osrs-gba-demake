#!/usr/bin/env python3
"""Render whole zones from the generated metatiles (ground + overlay, roofs shown) to PNGs, for
reviewing a town at a glance. usage: zone_png.py OUT_DIR [ZONE ...] [--half]"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import world_gen as wg  # noqa: E402
from sheet import write_png  # noqa: E402


def c8(c):
    return bytes((c[0] << 3 | c[0] >> 2, c[1] << 3 | c[1] >> 2, c[2] << 3 | c[2] >> 2))


def render(z, half):
    w, h = (z.X1 - z.X0 + 1) * 16, (z.Y1 - z.Y0 + 1) * 16
    rows = [bytearray(w * 3) for _ in range(h)]
    for y in range(z.Y0, z.Y1 + 1):
        for x in range(z.X0, z.X1 + 1):
            g, o = wg.meta_imgs[wg.map_meta[y][x]], wg.meta_imgs[wg.map_over[y][x]]
            for py in range(16):
                row = rows[(y - z.Y0) * 16 + py]
                for px in range(16):
                    c = o[py][px] if o[py][px] is not None else g[py][px]
                    i = ((x - z.X0) * 16 + px) * 3
                    row[i:i + 3] = c8(c) if c is not None else b'\0\0\0'
    if half:
        rows = [bytes(r[i] for j in range(0, w, 2) for i in range(j * 3, j * 3 + 3)) for r in rows[::2]]
        w, h = w // 2, h // 2
    return w, h, [bytes(r) for r in rows]


if __name__ == '__main__':
    a = [v for v in sys.argv[1:] if not v.startswith('--')]
    out, names = a[0], a[1:] or [z[0] for z in wg.ZONES[1:]]
    os.makedirs(out, exist_ok=True)
    for (n, z, ps, pl) in wg.ZONES:
        if n in names:
            write_png(os.path.join(out, n.lower() + '.png'), *render(z, '--half' in sys.argv))
            print('wrote', n)
