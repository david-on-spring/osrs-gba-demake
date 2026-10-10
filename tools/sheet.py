#!/usr/bin/env python3
"""Tile harness screenshots into one PNG (stdlib only), for before/after reviews.

usage: sheet.py OUT.png COLS IMG [IMG ...]
       sheet.py OUT.png --pairs BEFORE_DIR AFTER_DIR NAME [NAME ...]   (before | after rows)
Images must be 8-bit RGB or RGBA PNGs of equal size (the harness writes 480x320 RGB). A 4-pixel
dark gutter separates the cells."""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert (depth == 8 and ctype in (2, 3, 6)) or (ctype == 3 and depth in (1, 2, 4)), (path, depth, ctype)
            bpp = {2: 3, 3: 1, 6: 4}[ctype]
        elif typ == b'PLTE':
            plte = body
        elif typ == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp if depth == 8 else (w * depth + 7) // 8
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        if bpp == 1:
            if depth < 8:
                per = 8 // depth
                line = [(line[i // per] >> (8 - depth * (i % per + 1))) & ((1 << depth) - 1) for i in range(w)]
            rows.append(b''.join(plte[v * 3:v * 3 + 3] for v in line))
        else:
            rows.append(bytes(line) if bpp == 3 else bytes(v for i, v in enumerate(line) if i % 4 != 3))
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def sheet(out, cols, paths):
    imgs = [read_png(p) for p in paths]
    w, h = imgs[0][0], imgs[0][1]
    g = 4
    nrows = (len(imgs) + cols - 1) // cols
    W, H = cols * w + (cols + 1) * g, nrows * h + (nrows + 1) * g
    canvas = [bytearray(b'\x18\x14\x10' * W) for _ in range(H)]
    for i, (iw, ih, rows) in enumerate(imgs):
        assert (iw, ih) == (w, h), paths[i]
        ox, oy = g + (i % cols) * (w + g), g + (i // cols) * (h + g)
        for y in range(h):
            canvas[oy + y][ox * 3:(ox + w) * 3] = rows[y]
    write_png(out, W, H, [bytes(r) for r in canvas])


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) < 3:
        sys.exit(__doc__)
    if a[1] == '--pairs':
        before, after, names = a[2], a[3], a[4:]
        paths = []
        for n in names:
            paths += ['%s/%s.png' % (before, n), '%s/%s.png' % (after, n)]
        sheet(a[0], 2, paths)
    else:
        sheet(a[0], int(a[1]), a[2:])
