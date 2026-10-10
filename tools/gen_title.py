"""Login screen backdrop, after the OSRS login screen: a stone hall with brick walls, two
pillars framing a misty archway, iron braziers either side (the flames are animated sprites,
see title_flames in src/title.c) and the stone "RuneScape" logo over a red "OLD SCHOOL" banner.

Drawn once at build time into the panel canvas's 4bpp tile layout (30x20 tiles, row-major) with
its own 16-colour palette. The palette keeps the panel colour roles (src/game.h PC_*) so the
login box and buttons drawn on top come out as grey stone."""
import math

W, H = 240, 160

# index -> colour; roles: 1 FILL (login box), 2 WELL, 3 EDGE, 4 EDGE_DIM, 5 HL1 (selected
# button), 6 HL2 (button), 7 HL3, 8 GOLD, 14 WHITE, 15 RED (music toggle); the rest are scenery
PAL = ['#000000', '#5e5e5c', '#2a2a29', '#a6a6a2', '#0c0c0c', '#7e7e7a', '#3e3e3c', '#525250',
       '#e8c040', '#b6bfba', '#8a928e', '#e07818', '#7a1010', '#1a1a19', '#ffffff', '#c42020']
K, FILL, WELL, EDGE, DARK, LITE, BUTTON, MID = 4, 1, 2, 3, 13, 5, 6, 7
FOG, FOGD, GLOW, BANNER_D, BANNER, GOLD, WHITE = 9, 10, 11, 12, 15, 8, 14

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def hsh(*v):
    h = 2166136261
    for x in v:
        h = ((h ^ (x & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return h


def dither(x, y, t, a, b):
    """t in 0..1: 0 -> a, 1 -> b, ordered dither in between."""
    return b if t * 16 > BAYER[y & 3][x & 3] + 0.5 else a


def bricks(img, x0, x1, y0, y1, shade):
    for y in range(y0, y1):
        row = y // 10
        for x in range(x0, x1):
            off = 11 if row & 1 else 0
            bx = (x + off) % 22
            by = y % 10
            if by == 9 or bx == 21:
                img[y][x] = DARK
                continue
            n = hsh(row, (x + off) // 22) % 5
            c = [MID, FILL, MID, BUTTON, FILL][n]
            if by == 0 and hsh(x, y) % 3:
                c = LITE if c in (MID, FILL) else FILL
            if hsh(x * 7, y * 3) % 23 == 0:
                c = DARK
            # walls darken towards the screen edges and the top
            d = shade(x, y)
            if d > 0.55 and c in (MID, FILL, BUTTON):
                c = dither(x, y, (d - 0.55) / 0.45, c, WELL)
            img[y][x] = c


def pillar(img, cx, w, y0, y1):
    x0, x1 = cx - w // 2, cx + w // 2
    ramp = [EDGE, LITE, LITE, FILL, FILL, MID, MID, BUTTON, WELL]
    for y in range(y0, y1):
        for x in range(x0, x1 + 1):
            t = (x - x0) / max(1, x1 - x0)
            c = ramp[min(len(ramp) - 1, int(t * len(ramp)))]
            if x in (x0, x1):
                c = K
            elif (y - y0) % 26 == 25:
                c = DARK
            elif hsh(x, y) % 17 == 0:
                c = MID if c != MID else BUTTON
            img[y][x] = c
    # capital and base: wider blocks
    for (ya, yb) in ((y0, y0 + 6), (y1 - 8, y1)):
        for y in range(ya, yb):
            for x in range(x0 - 4, x1 + 5):
                if 0 <= x < W:
                    edge = x in (x0 - 4, x1 + 4) or y in (ya, yb - 1)
                    img[y][x] = K if edge else (EDGE if y == ya + 1 else FILL if y < yb - 2 else MID)


def corridor(img):
    """The misty archway between the pillars: fog thickening towards the far end."""
    cx, cy, r = 120, 86, 42
    for y in range(30, 146):
        for x in range(78, 163):
            dx, dy = x - cx, y - cy
            inside = (dy >= 0 and abs(dx) <= r) or dx * dx + dy * dy <= r * r
            ring = not inside and (dy >= 0 and abs(dx) <= r + 6 or dx * dx + dy * dy <= (r + 6) ** 2)
            if inside:
                # depth: 0 at the arch's edge, 1 at the vanishing point low in the middle
                fx = 1 - min(1, abs(dx) / r)
                fy = min(1, max(0, (y - 50) / 80))
                t = fx * 0.55 + fy * 0.6
                if t < 0.35:
                    c = dither(x, y, t / 0.35, BUTTON, MID)
                elif t < 0.7:
                    c = dither(x, y, (t - 0.35) / 0.35, MID, FOGD)
                else:
                    c = dither(x, y, min(1, (t - 0.7) / 0.3), FOGD, FOG)
                # receding side walls: faint brick courses
                if fx < 0.45 and y % 8 == 0 and t < 0.6:
                    c = DARK if fx < 0.25 else MID
                img[y][x] = c
            elif ring:
                ang = math.atan2(dy, dx) if dy < 0 else (math.pi if dx < 0 else 0)
                seam = int((ang + math.pi) * 9) % 2
                d = math.hypot(dx, dy) if dy < 0 else r + 3 + (abs(dx) - r - 3)
                c = EDGE if d < r + 2 else LITE if d < r + 4 else FILL
                if seam and hsh(int(ang * 40)) % 3 == 0 and dy < 0:
                    c = DARK
                img[y][x] = c


def floor(img):
    for y in range(146, H):
        for x in range(W):
            n = (x // 30 + y // 7) & 1
            c = MID if n else BUTTON
            if y % 7 == 6 or (x + (y // 7) * 13) % 30 == 0:
                c = DARK
            dx = abs(x - 120) / 120
            if dx < 0.35:
                c = dither(x, y, (0.35 - dx) / 0.35 * 0.8, c, FOGD)
            img[y][x] = c


def brazier(img, cx):
    # bowl
    for y in range(94, 108):
        for x in range(cx - 22, cx + 23):
            if not 0 <= x < W:
                continue
            u, v = (x - cx) / 22, (y - 94) / 13
            if v < 0.25:
                if abs(u) <= 1:
                    img[y][x] = GLOW if v < 0.12 and abs(u) < 0.85 and hsh(x, y) % 3 else K if abs(u) > 0.93 else LITE
            elif u * u + (v - 0.25) ** 2 / 0.6 <= 1:
                c = EDGE if u < -0.55 else LITE if u < -0.2 else MID if u < 0.4 else WELL
                if abs(u) > 0.9:
                    c = K
                img[y][x] = c
    # stem with rings, and a base
    for y in range(108, 152):
        for x in range(cx - 4, cx + 5):
            if 0 <= x < W:
                c = LITE if x < cx - 1 else MID if x < cx + 2 else WELL
                if x in (cx - 4, cx + 4) or (y - 108) % 11 == 10:
                    c = K
                img[y][x] = c
    for y in range(146, 156):
        hw = 7 + (y - 146)
        for x in range(cx - hw, cx + hw + 1):
            if 0 <= x < W:
                img[y][x] = K if x in (cx - hw, cx + hw) or y == 155 else (LITE if x < cx else MID)


def glyph_mask(text, font_w, font_rows, scale):
    w = sum((font_w[ord(ch) - 32] + 1) * scale for ch in text) - scale
    m = [[0] * w for _ in range(9 * scale)]
    x = 0
    for ch in text:
        g = ord(ch) - 32
        for r in range(9):
            bits = font_rows[g][r]
            for b in range(8):
                if bits >> b & 1:
                    for sy in range(scale):
                        for sx in range(scale):
                            m[r * scale + sy][x + b * scale + sx] = 1
        x += (font_w[g] + 1) * scale
    return m


def stamp(img, mask, x0, y0, colf, outline=K, shadow=True):
    h, w = len(mask), len(mask[0])
    on = lambda x, y: 0 <= y < h and 0 <= x < w and mask[y][x]
    for y in range(-2, h + 3):
        for x in range(-2, w + 3):
            X, Y = x0 + x, y0 + y
            if not (0 <= X < W and 0 <= Y < H):
                continue
            if on(x, y):
                img[Y][X] = colf(x, y, w, h)
            elif any(on(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                img[Y][X] = outline
            elif shadow and (on(x - 2, y - 2) or on(x - 1, y - 2)):
                img[Y][X] = DARK


def logo(img, font_w, font_rows):
    # "RuneScape" in chiselled stone
    m = glyph_mask('RuneScape', font_w, font_rows, 3)
    w = len(m[0])

    def stone(x, y, w, h):
        t = y / h
        c = EDGE if t < 0.3 else LITE if t < 0.62 else FILL
        if hsh(x // 2, y // 2) % 9 == 0:
            c = LITE if c == EDGE else MID
        return c
    stamp(img, m, 120 - w // 2, 3, stone)
    # red banner with forked tails, "OLD SCHOOL" in gold
    t = glyph_mask('OLD SCHOOL', font_w, font_rows, 2)
    tw = len(t[0])
    bx0, bx1, by0, by1 = 120 - tw // 2 - 12, 120 + tw // 2 + 12, 32, 52
    for y in range(by0, by1):
        for x in range(bx0 - 10, bx1 + 11):
            inner = bx0 <= x <= bx1
            tail = not inner and abs((y - by0) - (by1 - by0) / 2) < (by1 - by0) / 2 - abs(
                (x - (bx0 if x < bx0 else bx1))) * 0.25 and not (
                abs(y - (by0 + by1) / 2) < 3 and abs(x - (bx0 - 10 if x < bx0 else bx1 + 10)) < 6)
            if inner or tail:
                edge = y in (by0, by1 - 1) or (not inner and (y in (by0 + 1, by1 - 2)))
                img[y][x] = K if edge else (BANNER_D if (not inner or y > by1 - 5) else BANNER)
    stamp(img, t, 120 - tw // 2, by0 + 2, lambda x, y, w, h: GOLD if y < h * 0.6 else GLOW, K, False)


def make(font_w, font_rows):
    img = [[DARK] * W for _ in range(H)]
    edge = lambda x, y: max(abs(x - 120) / 120, 1 - y / 70 if y < 70 else 0) ** 1.4
    bricks(img, 0, W, 0, H, edge)
    corridor(img)
    pillar(img, 64, 24, 36, 150)
    pillar(img, 176, 24, 36, 150)
    floor(img)
    brazier(img, 22)
    brazier(img, 218)
    logo(img, font_w, font_rows)
    words = []
    for ty in range(20):
        for tx in range(30):
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= img[ty * 8 + y][tx * 8 + x] << (4 * x)
                words.append(v)
    return PAL, words
