#!/usr/bin/env python3
"""LZ77 in the GBA BIOS format (SWI 0x11 LZ77UnCompWram / 0x12 LZ77UnCompVram).

Stream: a u32 header (0x10 | size << 8), then groups of one flag byte (MSB first; 1 = match)
and up to eight items. A literal is one byte; a match is two bytes, ((len - 3) << 12) |
(disp - 1) stored high byte first, copying len (3..18) bytes from disp (1..4096) bytes back.
The stream is padded to a multiple of four bytes.

compress(data, vram=False) -> bytes; vram=True avoids disp 1 (the VRAM variant writes 16 bits
at a time, so it cannot copy the byte it has just written). decompress() is the reference
decoder used by the self-test (python3 tools/lz77.py)."""

MIN, MAX, WIN = 3, 18, 4096


def compress(data, vram=False):
    data = bytes(data)
    n = len(data)
    assert n < 1 << 24
    out = bytearray([0x10, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
    heads = {}          # 3-byte key -> recent positions (newest last)
    mind = 2 if vram else 1
    i = 0
    flag_at, bit, items = -1, 0, 0

    def best_match(i):
        if i + MIN > n:
            return 0, 0
        cand = heads.get(data[i:i + MIN])
        if not cand:
            return 0, 0
        bl, bd = 0, 0
        lim = min(MAX, n - i)
        for j in reversed(cand[-48:]):
            d = i - j
            if d > WIN:
                break
            if d < mind:
                continue
            l = MIN
            while l < lim and data[j + l] == data[i + l]:
                l += 1
            if l > bl:
                bl, bd = l, d
                if l == lim:
                    break
        return bl, bd

    def insert(i):
        if i + MIN <= n:
            heads.setdefault(data[i:i + MIN], []).append(i)

    while i < n:
        if items == 0:
            flag_at = len(out)
            out.append(0)
            bit = 0x80
        l, d = best_match(i)
        if l >= MIN:
            # lazy matching: a literal now may allow a longer match next
            l2, _ = best_match(i + 1) if l < MAX else (0, 0)
            if l2 > l + 1:
                l = 0
        if l >= MIN:
            out[flag_at] |= bit
            v = ((l - MIN) << 12) | (d - 1)
            out += bytes([v >> 8, v & 0xFF])
            for k in range(l):
                insert(i + k)
            i += l
        else:
            out.append(data[i])
            insert(i)
            i += 1
        bit >>= 1
        items = (items + 1) & 7
    while len(out) & 3:
        out.append(0)
    return bytes(out)


def decompress(src):
    src = bytes(src)
    assert src[0] == 0x10
    n = src[1] | (src[2] << 8) | (src[3] << 16)
    out = bytearray()
    i = 4
    while len(out) < n:
        flags = src[i]
        i += 1
        for b in range(8):
            if len(out) >= n:
                break
            if flags & (0x80 >> b):
                v = (src[i] << 8) | src[i + 1]
                i += 2
                l, d = (v >> 12) + MIN, (v & 0xFFF) + 1
                for _ in range(l):
                    out.append(out[-d])
            else:
                out.append(src[i])
                i += 1
    return bytes(out[:n])


if __name__ == '__main__':
    import random
    rng = random.Random(1)
    for t in range(200):
        n = rng.randrange(0, 3000)
        alpha = rng.choice([2, 4, 16, 256])
        d = bytes(rng.randrange(alpha) for _ in range(n))
        if t % 3 == 0:
            d = bytes(n)
        for vram in (False, True):
            c = compress(d, vram)
            assert decompress(c) == d, (t, vram)
            assert len(c) % 4 == 0
    print('lz77: ok')
