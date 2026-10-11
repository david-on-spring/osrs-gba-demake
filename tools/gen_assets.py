#!/usr/bin/env python3
"""Generates every piece of art and static data for the GBA demake as C arrays.

Pure Python, no dependencies. All art is procedural or ASCII-authored so the whole
game builds from source. Output: <outdir>/assets.c and <outdir>/assets.h
"""
import math
import os
import random
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build"
os.makedirs(OUT, exist_ok=True)


def c5(h):
    h = h.lstrip('#')
    return (int(h[0:2], 16) >> 3, int(h[2:4], 16) >> 3, int(h[4:6], 16) >> 3)


def gba(c):
    return c[0] | (c[1] << 5) | (c[2] << 10)


def hsh(*v):
    x = 0x9E3779B1
    for a in v:
        x = ((x ^ (a & 0xffffffff)) * 0x85EBCA6B) & 0xffffffff
        x ^= x >> 13
    x = (x * 0xC2B2AE35) & 0xffffffff
    x ^= x >> 16
    return x


def blank(w, h):
    return [[None] * w for _ in range(h)]


# ----------------------------------------------------------------------------------------
# World: map, terrain art, world tiles and palettes live in tools/world_gen.py. world_cache keeps
# its results under OUT/cache, so edits that leave the world alone skip rebuilding it.
# ----------------------------------------------------------------------------------------
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import world_cache  # noqa: E402
import world_emit  # noqa: E402
world = world_cache.load(OUT)


def outline(img, col):
    h = len(img)
    w = len(img[0])
    out = [r[:] for r in img]
    for y in range(h):
        for x in range(w):
            if img[y][x] is None:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and img[yy][xx] is not None:
                        out[y][x] = col
                        break
    return out


def paste(dst, src, ox, oy):
    for y, row in enumerate(src):
        for x, c in enumerate(row):
            if c is not None and 0 <= oy + y < len(dst) and 0 <= ox + x < len(dst[0]):
                dst[oy + y][ox + x] = c


def ellipse_fill(img, cx, cy, rx, ry, col):
    for y in range(len(img)):
        for x in range(len(img[0])):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                img[y][x] = col


# ----------------------------------------------------------------------------------------
# Sprites
# ----------------------------------------------------------------------------------------
obj_tiles = []   # list of 8x8 index tiles
SPR = {}         # name -> first tile index


def add_sprite(name, rows, w=16, h=16):
    """rows: list of rows of palette indices (ints)."""
    SPR[name] = len(obj_tiles)
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            obj_tiles.append(tuple(tuple(rows[ty + y][tx + x] for x in range(8)) for y in range(8)))


def ascii_sprite(lines, cmap, w=16, h=16):
    assert len(lines) == h, (len(lines), lines)
    out = []
    for ln in lines:
        assert len(ln) == w, (len(ln), ln)
        out.append([cmap[c] for c in ln])
    return out


HUMAN_MAP = {'.': 0, 'o': 1, 's': 2, 'S': 3, 'h': 4, 'H': 5, 't': 6, 'T': 7, 'l': 8, 'L': 9,
             'b': 10, 'a': 11, 'm': 12, 'M': 13, 'n': 14, 'N': 15}

HUM_DOWN_TOP = [
    "................",
    "......oooo......",
    ".....ohhhho.....",
    "....ohhhhhho....",
    "....ohssssho....",
    "....osossoso....",
    "....oSssssSo....",
    ".....oSSSSo.....",
    "....otttttto....",
    "...ottTttTtto...",
    "...osttttttso...",
    "...osaaaaaaso...",
]
HUM_UP_TOP = [
    "................",
    "......oooo......",
    ".....ohhhho.....",
    "....ohhhhhho....",
    "....ohhhhhho....",
    "....ohhhhhho....",
    "....oHhhhhHo....",
    ".....oHHHHo.....",
    "....otttttto....",
    "...ottTttTtto...",
    "...osttttttso...",
    "...osaaaaaaso...",
]
HUM_SIDE_TOP = [
    "................",
    "......oooo......",
    ".....ohhhho.....",
    "....ohhhhhho....",
    "....ohhhssso....",
    "....ohhssoso....",
    "....oHhsssso....",
    ".....oSSSSo.....",
    ".....otttto.....",
    ".....ottTto.....",
    ".....otsTto.....",
    ".....oaasao.....",
]
LEGS_FRONT_STAND = [
    "....ollllllo....",
    "....olloollo....",
    "....obboobbo....",
    ".....oo..oo.....",
]
LEGS_FRONT_STEP = [
    "....ollllllo....",
    "....olloollo....",
    "....obboollo....",
    ".....oo.obbo....",
]
LEGS_SIDE_STAND = [
    ".....ollllo.....",
    ".....ollllo.....",
    ".....obbbbo.....",
    ".....oooooo.....",
]
LEGS_SIDE_STEP = [
    ".....ollllo.....",
    "....olloollo....",
    "...obbo..obbo...",
    "...ooo....ooo...",
]
for name, top, legs in (('HUM_D0', HUM_DOWN_TOP, LEGS_FRONT_STAND), ('HUM_D1', HUM_DOWN_TOP, LEGS_FRONT_STEP),
                        ('HUM_U0', HUM_UP_TOP, LEGS_FRONT_STAND), ('HUM_U1', HUM_UP_TOP, LEGS_FRONT_STEP),
                        ('HUM_S0', HUM_SIDE_TOP, LEGS_SIDE_STAND), ('HUM_S1', HUM_SIDE_TOP, LEGS_SIDE_STEP)):
    add_sprite(name, ascii_sprite(top + legs, HUMAN_MAP))

ANIMAL_MAP = {'.': 0, 'o': 1, 'w': 2, 'g': 3, 'k': 4, 'p': 5, 'h': 6, 'y': 7, 'r': 8, 'b': 9}
COW0 = [
    "................",
    "................",
    "................",
    "...........h..h.",
    "...........owwo.",
    ".ooooooooooowkwo",
    "owwwkkwwwwwowwwo",
    "owwkkkwwwkwowppo",
    "owwwkwwwkkkwoppo",
    "owwwwwwwwkkwooo.",
    "ogwwwwwwwwwwo...",
    ".oggggggggggo...",
    ".owo.owo.owo.owo"[:16],
    ".oko.oko.oko.oko"[:16],
    "................",
    "................"]
COW0[12] = ".owo.owo..owo.o."
COW0[13] = ".oko.oko..oko..."
COW1 = list(COW0)
COW1[12] = "..owo.owo.owo..."
COW1[13] = "..oko.oko.oko..."
CHICK0 = [
    "................",
    "................",
    "................",
    "................",
    "..........rr....",
    ".........owwo...",
    ".........owoyy..",
    "...o.....owwo...",
    "..owo...owwwo...",
    "..owwooowwwwo...",
    "..owwwwwwgwwo...",
    "...owwwwggwo....",
    "....owwwwwo.....",
    ".....oyooyo.....",
    ".....y...y......",
    "................"]
CHICK1 = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "...o............",
    "..owo...........",
    "..owwooooooo....",
    "..owwwwwwwwwrr..",
    "..owwwwwgwowyy..",
    "...owwwwggwo....",
    "....owwwwwo.....",
    ".....oyooyo.....",
    ".....y...y......",
    "................"]
for name, art in (('COW0', COW0), ('COW1', COW1), ('CHK0', CHICK0), ('CHK1', CHICK1)):
    add_sprite(name, ascii_sprite(art, ANIMAL_MAP))

# ---- quest NPC sprite sets -------------------------------------------------------------
# Human-shaped sets are six consecutive 16x16 frames (D0 D1 U0 U1 S0 S1), like HUM_*.
HAT_ROWS = [".......oo.......", "......otto......", ".....ottTto.....", "...otttttttto..."]
WIZ_DOWN_TOP = HAT_ROWS + [
    "....ohssssho....",
    "....osossoso....",
    "....oSssssSo....",
    ".....oShhSo.....",
    "....otthhtto....",
    "...ottThhTtto...",
    "...osttttttso...",
    "...osaaaaaaso...",
]
WIZ_UP_TOP = HAT_ROWS + HUM_UP_TOP[4:]
WIZ_SIDE_TOP = [".......oo.......", "......otto......", ".....ottTto.....", "...ottttttto...."] + [
    "....ohhhssso....",
    "....ohhssoso....",
    "....oHhsssso....",
    ".....oShhho.....",
    ".....otthto.....",
    ".....ottTto.....",
    ".....otsTto.....",
    ".....oaasao.....",
]
ROBE_FRONT_STAND = ["...otttttttto...", "...otTttttTto...", "...oTTTTTTTTo...", "....oooooooo...."]
ROBE_FRONT_STEP = ["...otttttttto...", "...ottTttTtto...", "....oTTTTTTTo...", "....ooooooobo..."]
ROBE_SIDE_STAND = [".....otttto.....", "....ottttTo.....", "....oTTTTTTo....", "....oooooooo...."]
ROBE_SIDE_STEP = [".....otttto.....", "....ottttTTo....", "...oTTTTTTTo....", "...ooboooooo...."]


def recolour(rows, mp):
    return [''.join(mp.get(ch, ch) for ch in r) for r in rows]


DARK = {'t': 'l', 'T': 'L'}
ROBE_SETS = {
    # wizard: pointed hat, beard, robe in the top colour
    'WIZ': ((WIZ_DOWN_TOP, ROBE_FRONT_STAND, ROBE_FRONT_STEP), (WIZ_UP_TOP, ROBE_FRONT_STAND, ROBE_FRONT_STEP),
            (WIZ_SIDE_TOP, ROBE_SIDE_STAND, ROBE_SIDE_STEP)),
    # witch: the same in the dark (leg) colour
    'WITCH': ((recolour(WIZ_DOWN_TOP, DARK), recolour(ROBE_FRONT_STAND, DARK), recolour(ROBE_FRONT_STEP, DARK)),
              (recolour(WIZ_UP_TOP, DARK), recolour(ROBE_FRONT_STAND, DARK), recolour(ROBE_FRONT_STEP, DARK)),
              (recolour(WIZ_SIDE_TOP, DARK), recolour(ROBE_SIDE_STAND, DARK), recolour(ROBE_SIDE_STEP, DARK))),
    # priest / monk: bare head, dark robe
    'PRI': ((recolour(HUM_DOWN_TOP, DARK), recolour(ROBE_FRONT_STAND, DARK), recolour(ROBE_FRONT_STEP, DARK)),
            (recolour(HUM_UP_TOP, DARK), recolour(ROBE_FRONT_STAND, DARK), recolour(ROBE_FRONT_STEP, DARK)),
            (recolour(HUM_SIDE_TOP, DARK), recolour(ROBE_SIDE_STAND, DARK), recolour(ROBE_SIDE_STEP, DARK))),
    # dress / long coat in the top colour
    'DRESS': ((HUM_DOWN_TOP, ROBE_FRONT_STAND, ROBE_FRONT_STEP), (HUM_UP_TOP, ROBE_FRONT_STAND, ROBE_FRONT_STEP),
              (HUM_SIDE_TOP, ROBE_SIDE_STAND, ROBE_SIDE_STEP)),
}
for set_name, views in ROBE_SETS.items():
    for vi, (top, stand, step) in enumerate(views):
        v = 'DUS'[vi]
        add_sprite('%s_%s0' % (set_name, v), ascii_sprite(top + stand, HUMAN_MAP))
        add_sprite('%s_%s1' % (set_name, v), ascii_sprite(top + step, HUMAN_MAP))

# skeleton (animal palette: bone white, grey, near black)
SKEL_DOWN = [
    "................",
    "......oooo......",
    ".....owwwwo.....",
    "....owwwwwwo....",
    "....owkwwkwo....",
    "....owkwwkwo....",
    ".....owwwwo.....",
    ".....okwkwo.....",
    "....ooowwooo....",
    "...owgwwwwgwo...",
    "...owoggggowo...",
    "...owgwwwwgwo...",
    "...ow.oggo.wo...",
    "....o.owwo.o....",
    ".....owoowo.....",
    ".....oo..oo....."]
SKEL_DOWN1 = SKEL_DOWN[:13] + ["....o.owwo.o....", ".....owoowwo....", ".....oo..ooo...."]
SKEL_UP = SKEL_DOWN[:4] + ["....owwwwwwo....", "....owwwwwwo...."] + SKEL_DOWN[6:]
SKEL_UP1 = SKEL_UP[:13] + SKEL_DOWN1[13:]
SKEL_SIDE = [
    "................",
    "......oooo......",
    ".....owwwwo.....",
    "....owwwwwwo....",
    "....owwwkwko....",
    "....owwwkwko....",
    ".....owwwwo.....",
    "......owkko.....",
    ".....oowwo......",
    ".....owgwo......",
    ".....owggo......",
    ".....owwgo......",
    ".....owggo......",
    ".....owwwo......",
    ".....owoow......",
    ".....oo.oo......"]
SKEL_SIDE1 = SKEL_SIDE[:13] + ["....owo.owo.....", "...owo...owo....", "...oo.....oo...."]
for name, art in (('SKEL_D0', SKEL_DOWN), ('SKEL_D1', SKEL_DOWN1), ('SKEL_U0', SKEL_UP), ('SKEL_U1', SKEL_UP1),
                  ('SKEL_S0', SKEL_SIDE), ('SKEL_S1', SKEL_SIDE1)):
    add_sprite(name, ascii_sprite(art, ANIMAL_MAP))

GHOST0 = [
    "................",
    "......oooo......",
    ".....owwwwo.....",
    "....owwwwwwo....",
    "....owkwwkwo....",
    "....owkwwkwo....",
    "....owwwwwwo....",
    "...owwwkkwwwo...",
    "..owwwwwwwwwwo..",
    ".owgowwwwwwogwo.",
    ".oo.owwwwwwo.oo.",
    "....owgwwwgo....",
    "....owwwwwwo....",
    "....owgwwgwo....",
    "....owwoowwo....",
    "....oo.oo.oo...."]
GHOST1 = GHOST0[:13] + ["....owwwwwwo....", "....oowwoowo....", ".....oo..oo....."]
SHEEP0 = [
    "................",
    "................",
    "................",
    "................",
    "...oooooooo.....",
    "..owwgwwwwwo.oo.",
    ".owwwwwwgwwwokko",
    "owgwwwwwwwwwokyk",
    "owwwwgwwwwgwokko",
    "owwwwwwwwwwwo.o.",
    ".owgwwwwgwwwo...",
    "..oooooooooo....",
    "...oko..oko.....",
    "...oko..oko.....",
    "................",
    "................"]
SHEEP1 = SHEEP0[:12] + ["....oko..oko....", "....oko..oko...."] + SHEEP0[14:]
SHORN = {'w': 'h'}
IMP0 = [
    "................",
    "................",
    "................",
    "................",
    "....oo....oo....",
    "....oko..oko....",
    ".....orrrro.....",
    "....orrrrrro....",
    "....oryrryro....",
    "....orrrrrro....",
    ".....orkkro.....",
    "...oorrrrrroo...",
    "..oro.orro.oro..",
    ".....orrrro.....",
    ".....oro.oro....",
    ".....oo...oo...."]
IMP1 = IMP0[:11] + ["..oroorrrroor...", "...o..orro..o...", ".....orrrro.....", "....oro..oro....", "....oo....oo...."]
RAT0 = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..........oo....",
    "........oobpo...",
    "......oobbbbboo.",
    "..o..obbbbbbbko.",
    ".opoobbbbbbbbbpo",
    "op...obbbbbbboo.",
    "......oo..oo....",
    "................",
    "................"]
RAT1 = RAT0[:12] + ["op...obbbbbbboo.", ".....oo....oo..."] + RAT0[14:]
for name, art in (('GHOST0', GHOST0), ('GHOST1', GHOST1), ('SHEEP0', SHEEP0), ('SHEEP1', SHEEP1),
                  ('SHORN0', recolour(SHEEP0, SHORN)), ('SHORN1', recolour(SHEEP1, SHORN)),
                  ('IMP0', IMP0), ('IMP1', IMP1), ('RAT0', RAT0), ('RAT1', RAT1)):
    add_sprite(name, ascii_sprite(art, ANIMAL_MAP))


def demon_frame(f):
    """Delrith: a 32x32 winged demon in the animal palette."""
    A = ANIMAL_MAP
    g = [[None] * 32 for _ in range(32)]

    def setp(x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < 32 and 0 <= y < 32:
            g[y][x] = A[c]

    def ell(cx_, cy_, rx, ry, c):
        for y in range(32):
            for x in range(32):
                if ((x - cx_) / rx) ** 2 + ((y - cy_) / ry) ** 2 <= 1:
                    g[y][x] = A[c]

    def line(x0, y0, x1, y1, c, th=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            for d in range(th):
                setp(x0 + (x1 - x0) * t + d, y0 + (y1 - y0) * t, c)
    lift = -2 if f else 0
    # wings
    for side in (-1, 1):
        for k in range(9):
            line(16 + side * 5, 13, 16 + side * (8 + k * 0.9), 4 + k * 1.6 + lift * (1 - k / 9), 'k')
        line(16 + side * 5, 13, 16 + side * 15, 3 + lift, 'g')
    ell(16, 20, 6.5, 7.5, 'r')                 # body
    ell(16, 21, 3.5, 4.5, 'p')                 # chest highlight
    ell(16, 10, 4.8, 4.6, 'r')                 # head
    for side in (-1, 1):                        # horns
        line(16 + side * 3, 7, 16 + side * 6, 1, 'b', 2 if side < 0 else 1)
        setp(16 + side * 6, 1, 'h')
        setp(16 + side * 2, 9, 'y')             # eyes
        setp(16 + side * 2, 10, 'k')
    for x in range(14, 19):
        setp(x, 13, 'k')
    setp(14, 14, 'w')
    setp(18, 14, 'w')
    arm = -4 if f else 0
    for side in (-1, 1):                        # arms and claws
        line(16 + side * 6, 16, 16 + side * 10, 23 + arm, 'r', 2)
        for c in range(3):
            setp(16 + side * (10 + c * 0.6), 25 + arm + (c % 2), 'w')
        line(16 + side * 3, 26, 16 + side * 4, 30, 'r', 2)   # legs
        setp(16 + side * 4, 31, 'k')
        setp(16 + side * 3, 31, 'k')
    out = outline(g, A['o'])
    return [[0 if c is None else c for c in r] for r in out]


add_sprite('DEMON0', demon_frame(0), 32, 32)
add_sprite('DEMON1', demon_frame(1), 32, 32)

# effects bank (OBJ palette 6)
FX_MAP = {'.': 0, 'o': 1, 'w': 2, 'c': 3, 'B': 4, 'r': 5, 'R': 6, 'f': 7, 'y': 8, 'g': 9, 'G': 10,
          'C': 11, 'D': 12, 'L': 13, 'n': 14, 'x': 15}


def proc(w, h, fn):
    return [[fn(x, y) for x in range(w)] for y in range(h)]


for f in range(3):
    def ripple(x, y, f=f):
        dx, dy = x - 7.5, (y - 8.5) / 0.55
        d = math.sqrt(dx * dx + dy * dy)
        r1 = 2.0 + f * 2.2
        if abs(d - r1) < 0.7 and y > 3:
            return FX_MAP['w']
        if abs(d - (r1 + 3.5) % 9) < 0.6 and y > 3:
            return FX_MAP['c']
        if (x, y) in ((4 + f, 4), (10 - f, 6), (7, 2 + f)):
            return FX_MAP['w']
        return 0
    add_sprite('SPOT%d' % f, proc(16, 16, ripple))

for f in range(3):
    def flame(x, y, f=f):
        if y >= 12 and y <= 14 and 2 <= x <= 13:
            if y == 13 and x in (2, 13):
                return FX_MAP['o']
            return FX_MAP['n'] if (x + y) % 4 else FX_MAP['o']
        cx = 7.5 + math.sin((y + f * 3) * 0.9) * 1.2
        width = (12 - y) * 0.45 if y < 12 else 0
        width = min(width, 4.5) * (1.0 - max(0, (5 - y)) * 0.12)
        d = abs(x - cx)
        if y < 2 or width <= 0:
            return 0
        if d < width * 0.35:
            return FX_MAP['y']
        if d < width * 0.7:
            return FX_MAP['f']
        if d < width:
            return FX_MAP['r']
        return 0
    add_sprite('FIRE%d' % f, proc(16, 16, flame))

# login screen flames: tall licking tongues, no logs (16x32, four frames)
for f in range(4):
    def tflame(x, y, f=f):
        t = (31 - y) / 31.0                      # 0 at the base, 1 at the top
        best = 0
        for k, (ox, ph, top) in enumerate(((7.5, 0.0, 1.0), (4.5, 2.1, 0.62), (10.5, 4.2, 0.72))):
            reach = top * (0.86 + 0.14 * math.sin(f * 1.57 + ph))
            if t > reach:
                continue
            u = t / reach
            cx = ox + math.sin(y * 0.33 + f * 1.57 + ph) * 1.6 * u
            w = (4.2 if k == 0 else 2.6) * (1 - u) ** 0.6
            d = abs(x - cx)
            if d < w * 0.35 and u < 0.75:
                best = max(best, 3)
            elif d < w * 0.7:
                best = max(best, 2)
            elif d < w:
                best = max(best, 1)
        spark = hash((f, x, y)) % 97 == 0 and y < 14
        if spark:
            best = max(best, 2)
        return [0, FX_MAP['r'], FX_MAP['f'], FX_MAP['y']][best]
    add_sprite('TFLAME%d' % f, proc(16, 32, tflame), 16, 32)


def splat(col, dark):
    def fn(x, y):
        dx, dy = x - 7.5, y - 7.5
        a = math.atan2(dy, dx)
        rad = 6.2 + 1.1 * math.sin(a * 5)
        d = math.sqrt(dx * dx + dy * dy)
        if d < rad - 1:
            return FX_MAP[col]
        if d < rad:
            return FX_MAP[dark]
        return 0
    return fn


add_sprite('SPLAT_RED', proc(16, 16, splat('r', 'R')))
add_sprite('SPLAT_BLUE', proc(16, 16, splat('B', 'o')))


def hpbar(k):
    def fn(x, y):
        if y < 1 or y > 4 or x > 15:
            return 0
        if y in (1, 4) or x in (0, 15):
            return FX_MAP['o']
        return FX_MAP['g'] if x - 1 < k else FX_MAP['r']
    return fn


SPR['HPBAR'] = len(obj_tiles)
for k in range(15):
    rows = proc(16, 8, hpbar(k))
    for tx in (0, 8):
        obj_tiles.append(tuple(tuple(rows[y][tx + x] for x in range(8)) for y in range(8)))

CURSOR = ["CC......", "CLCC....", "CLLLCC..", "CLLLLLCC", "CLLLLLCC", "CLLLCC..", "CLCC....", "CC......"]
add_sprite('CURSOR', ascii_sprite(CURSOR, FX_MAP, 8, 8), 8, 8)
ARROWD = ["CCCCCCC.", ".CLLLC..", "..CLC...", "...C....", "........", "........", "........", "........"]
add_sprite('ARROWD', ascii_sprite(ARROWD, FX_MAP, 8, 8), 8, 8)
SHADOW = proc(16, 8, lambda x, y: FX_MAP['o'] if ((x - 7.5) / 6) ** 2 + ((y - 4.5) / 2) ** 2 <= 1 and (x + y) % 2 == 0 else 0)
add_sprite('SHADOW', SHADOW, 16, 8)

OBJ_PALS = [None] * 16
OBJ_PALS[0] = ['#1a1a1a', '#e8b088', '#c08458', '#7a4a22', '#4e2e14', '#b03030', '#7a1e1e',
               '#3a4a7a', '#26305a', '#3a2614', '#6b4423', '#d0d0d0', '#808080', '#8e5c2e', '#5e3a1e']
OBJ_PALS[1] = ['#1a1a1a', '#e0a878', '#b8804e', '#3a3020', '#221a10', '#4a7a3a', '#2e5224',
               '#6a5038', '#4a3624', '#2a1e10', '#8a6a3a', '#c0c0c0', '#707070', '#8e5c2e', '#5e3a1e']
OBJ_PALS[2] = ['#1a1a1a', '#e0a878', '#b8804e', '#b4bcc4', '#7a828a', '#9aa2aa', '#646c74',
               '#4a4a5a', '#30303c', '#2a2a2a', '#d0b030', '#d8dce0', '#8a8e92', '#a03030', '#6a1a1a']
OBJ_PALS[3] = ['#1a1a1a', '#7aaa40', '#4e7a28', '#3a2a18', '#22180c', '#7a5a30', '#523a1c',
               '#5a4a30', '#3a2e1c', '#2a1e10', '#8a6a3a', '#a0a0a0', '#606060', '#7a5a30', '#4a3218']
OBJ_PALS[4] = ['#1a1a1a', '#e8b088', '#c08458', '#a0a0a0', '#6a6a6a', '#3c3c78', '#262650',
               '#2a2a3a', '#1a1a24', '#1a1a1a', '#d0b030', '#c0c0c0', '#707070', '#8e5c2e', '#5e3a1e']
OBJ_PALS[5] = ['#1a1a1a', '#ecece4', '#b0b0a8', '#2a2a2a', '#e8a0a0', '#d8d0b0', '#e8c020', '#d02020', '#7a5030']
OBJ_PALS[6] = ['#101010', '#ffffff', '#90c8f8', '#2050c8', '#d82020', '#801010', '#f08020', '#f8d040',
               '#38d038', '#186818', '#40e0f0', '#108898', '#c0fcff', '#6b4423', '#808080']
OBJ_PALS[7] = ['#1a1a1a', '#e8b088', '#c08458', '#e8e8e8', '#a8a8a8', '#f4f4f4', '#c8c8c8',
               '#3a3a4a', '#24242e', '#1a1a1a', '#e0e0e0', '#c0c0c0', '#707070', '#8e5c2e', '#5e3a1e']

# ----------------------------------------------------------------------------------------
# Icons (procedural)
# ----------------------------------------------------------------------------------------


class Canvas:
    def __init__(self, w=16, h=16):
        self.w, self.h = w, h
        self.p = blank(w, h)

    def set(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def line(self, x0, y0, x1, y1, c, thick=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            for dx in range(thick):
                for dy in range(thick):
                    self.set(x + dx - (thick - 1) / 2, y + dy - (thick - 1) / 2, c)

    def ell(self, cx, cy, rx, ry, c, shade=None):
        for y in range(self.h):
            for x in range(self.w):
                dx, dy = (x - cx) / rx, (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    if shade:
                        light = -(dx * 0.6 + dy * 0.8)
                        self.p[y][x] = shade[0] if light > 0.45 else shade[1] if light > -0.35 else shade[2]
                    else:
                        self.p[y][x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def poly(self, pts, c):
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys))), min(self.h, int(max(ys)) + 1)):
            xs = []
            n = len(pts)
            for i in range(n):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                if (y0 <= y + 0.5 < y1) or (y1 <= y + 0.5 < y0):
                    xs.append(x0 + (y + 0.5 - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for x in range(int(math.ceil(a - 0.5)), int(math.floor(b - 0.5)) + 1):
                    self.set(x, y, c)

    def done(self, out='#1a1410'):
        return outline(self.p, c5(out))


def C(h):
    return c5(h)


METAL = {
    'bronze': (C('#e0a868'), C('#b07a3c'), C('#7a5226')),
    'iron': (C('#c8c8c8'), C('#8c8c8c'), C('#5a5a5a')),
    'steel': (C('#e0e8f0'), C('#a8b0bc'), C('#6a7280')),
    'leather': (C('#b07a48'), C('#8a5a32'), C('#5e3a1e')),
    'wood': (C('#b08048'), C('#8a5a2e'), C('#5e3a1a')),
    'mithril': (C('#8a8ad0'), C('#56569a'), C('#34345e')),
    'adamant': (C('#68a070'), C('#4a7a52'), C('#2a4a30')),
    'rune': (C('#70c8e0'), C('#4a94b0'), C('#2a5a70')),
    'blurite': (C('#7898f0'), C('#4060c8'), C('#243a80')),
}
WOOD = (C('#a0703a'), C('#7a4e24'), C('#4e3014'))


def icon_coins():
    c = Canvas()
    gold = (C('#f8e060'), C('#d8a828'), C('#8a6010'))
    for (x, y) in ((5, 11), (10, 10), (7, 6)):
        c.ell(x, y + 1, 4, 2.5, gold[2])
        c.ell(x, y, 4, 2.5, gold[1])
        c.ell(x - 1, y - 0.5, 2, 1.2, gold[0])
    return c.done()


def icon_logs(cols, ring):
    c = Canvas()
    for (y, x0) in ((10, 2), (6, 4)):
        c.rect(x0, y - 2, x0 + 8, y + 2, cols[1])
        c.line(x0, y - 2, x0 + 8, y - 2, cols[0])
        c.line(x0, y + 2, x0 + 8, y + 2, cols[2])
        c.ell(x0 + 9, y, 2.2, 2.6, ring[0])
        c.set(x0 + 9, y, ring[1])
        c.set(x0 + 3, y, cols[2])
        c.set(x0 + 6, y + 1, cols[2])
    return c.done()


def icon_axe(m, w=WOOD):
    c = Canvas()
    c.line(3, 14, 10, 4, w[1], 2)
    c.line(3, 13, 9, 4, w[0])
    c.poly([(7, 2), (12, 1), (14, 6), (11, 7)], m[1])
    c.line(12, 1, 14, 6, m[0])
    c.line(8, 3, 11, 6, m[2])
    return c.done()


def icon_pick(m, w=WOOD):
    c = Canvas()
    c.line(4, 14, 10, 5, w[1], 2)
    c.line(4, 13, 9, 5, w[0])
    for i in range(15):
        t = i / 14
        x = 2 + 12 * t
        y = 6 - 4 * math.sin(t * math.pi) + 0.0
        c.set(x, y, m[1])
        c.set(x, y + 1, m[2])
        c.set(x, y - 1, m[0] if 3 < x < 12 else m[1])
    return c.done()


def icon_sword(m):
    c = Canvas()
    c.line(4, 11, 13, 2, m[1], 2)
    c.line(5, 10, 13, 2, m[0])
    c.line(2, 9, 6, 13, C('#c8a040'), 2)
    c.line(2, 14, 4, 12, C('#5e3a1e'), 2)
    c.set(1, 15, C('#c8a040'))
    return c.done()


def icon_helm(m):
    c = Canvas()
    c.ell(8, 9, 6, 6.5, None, shade=m)
    c.rect(1, 9, 15, 15, None)
    for y in range(9, 16):
        for x in range(16):
            c.p[y][x] = None
    c.rect(2, 9, 13, 10, m[2])
    c.rect(3, 11, 12, 13, m[1])
    c.rect(5, 11, 10, 11, C('#1a1410'))
    c.line(8, 3, 8, 9, m[0])
    return c.done()


def icon_sqshield(m):
    c = Canvas()
    c.rect(3, 2, 12, 11, m[1])
    c.poly([(3, 11), (13, 11), (8, 15)], m[1])
    c.line(3, 2, 12, 2, m[0])
    c.line(3, 2, 3, 11, m[0])
    c.line(12, 3, 12, 11, m[2])
    c.ell(7.5, 7, 1.6, 1.6, m[0])
    return c.done()


def icon_woodshield():
    c = Canvas()
    c.ell(7.5, 8, 6.5, 6.5, None, shade=METAL['wood'])
    for y in range(16):
        for x in range(16):
            if c.p[y][x] is not None and x % 4 == 1:
                c.p[y][x] = METAL['wood'][2]
    c.ell(7.5, 8, 1.8, 1.8, C('#a0a0a0'))
    return c.done()


def icon_body(m):
    c = Canvas()
    c.poly([(2, 3), (5, 2), (11, 2), (14, 3), (14, 8), (12, 8), (12, 14), (4, 14), (4, 8), (2, 8)], m[1])
    c.line(5, 3, 5, 13, m[0])
    c.line(11, 3, 11, 13, m[2])
    c.line(6, 2, 10, 2, C('#1a1410'))
    c.line(7, 3, 9, 3, C('#1a1410'))
    return c.done()


def icon_legs(m):
    c = Canvas()
    c.poly([(3, 2), (13, 2), (13, 14), (9, 14), (8, 6), (7, 14), (3, 14)], m[1])
    c.line(3, 2, 13, 2, m[0])
    c.line(4, 3, 4, 13, m[0])
    c.line(12, 3, 12, 13, m[2])
    return c.done()


def icon_boots(m):
    c = Canvas()
    for x0 in (2, 8):
        c.rect(x0, 4, x0 + 3, 12, m[1])
        c.rect(x0, 11, x0 + 5, 13, m[1])
        c.line(x0, 4, x0, 12, m[0])
        c.line(x0, 13, x0 + 5, 13, m[2])
    return c.done()


def icon_scimitar(m):
    c = Canvas()
    pts = [(5, 10), (6, 9), (7, 8), (8, 7), (9, 6), (10, 5), (11, 3), (12, 2), (13, 1)]
    for i, (x, y) in enumerate(pts):
        c.set(x, y, m[1])
        c.set(x + 1, y + 1, m[2])
        c.set(x, y + 1, m[0] if i > 1 else m[1])
    c.set(13, 0, m[0])
    c.line(2, 8, 6, 12, C('#c8a040'))
    c.line(1, 14, 4, 11, C('#5e3a1e'), 2)
    return c.done()


def icon_longsword(m):
    c = Canvas()
    c.line(5, 10, 14, 1, m[1], 2)
    c.line(6, 9, 14, 1, m[0])
    c.line(3, 8, 7, 12, m[2], 2)
    c.line(1, 14, 4, 11, C('#5e3a1e'), 2)
    c.set(0, 15, m[2])
    return c.done()


def icon_dagger(m):
    c = Canvas()
    c.line(6, 9, 12, 3, m[1], 2)
    c.line(7, 8, 12, 3, m[0])
    c.line(4, 8, 8, 12, C('#c8a040'))
    c.line(2, 13, 5, 10, C('#5e3a1e'), 2)
    return c.done()


def icon_mace(m, w=WOOD):
    c = Canvas()
    c.line(3, 14, 9, 7, w[1], 2)
    c.line(3, 13, 8, 7, w[0])
    c.ell(10.5, 5, 3.2, 3.2, None, shade=m)
    for (x, y) in ((10, 0), (15, 5), (14, 1), (6, 2), (11, 9), (14, 8)):
        c.set(x, y, m[2])
    return c.done()


def icon_fullhelm(m):
    c = Canvas()
    c.ell(8, 8, 6, 7, None, shade=m)
    for y in range(13, 16):
        for x in range(16):
            c.p[y][x] = None
    c.rect(3, 12, 12, 13, m[2])
    c.rect(4, 7, 11, 7, C('#1a1410'))
    c.rect(7, 8, 8, 10, C('#1a1410'))
    c.line(8, 2, 8, 5, m[0])
    return c.done()


def icon_chainbody(m):
    c = Canvas()
    c.poly([(2, 3), (5, 2), (11, 2), (14, 3), (14, 8), (12, 8), (12, 14), (4, 14), (4, 8), (2, 8)], m[1])
    for y in range(16):
        for x in range(16):
            if c.p[y][x] is not None and (x + y) % 2:
                c.p[y][x] = m[2] if y > 7 else m[0]
    c.line(6, 2, 10, 2, C('#1a1410'))
    c.line(7, 3, 9, 3, C('#1a1410'))
    return c.done()


def icon_skirt(m):
    c = Canvas()
    c.poly([(4, 2), (12, 2), (14.5, 14), (1.5, 14)], m[1])
    c.line(4, 2, 12, 2, m[0])
    for x in (5, 8, 11):
        c.line(x, 4, x + (x - 8) * 0.3, 13, m[2])
    c.line(3, 13, 13, 13, m[2])
    return c.done()


def icon_kite(m):
    c = Canvas()
    c.poly([(2, 2), (14, 2), (13, 8), (8, 15), (3, 8)], m[1])
    c.line(2, 2, 13, 2, m[0])
    c.line(2, 2, 3, 8, m[0])
    c.line(13, 3, 8, 14, m[2])
    c.line(8, 4, 8, 11, m[2])
    c.line(5, 6, 11, 6, m[2])
    return c.done()


def icon_twohand(m):
    c = Canvas()
    c.line(4, 11, 14, 1, m[1], 3)
    c.line(5, 10, 14, 1, m[0])
    c.line(2, 8, 7, 13, C('#c8a040'), 2)
    c.line(0, 15, 4, 11, C('#5e3a1e'), 2)
    return c.done()


def icon_warhammer(m, w=WOOD):
    c = Canvas()
    c.line(2, 14, 9, 7, w[1], 2)
    c.line(2, 13, 8, 7, w[0])
    c.poly([(7, 3), (11, 0), (15, 5), (11, 8)], m[1])
    c.line(11, 0, 15, 5, m[0])
    c.line(7, 3, 11, 8, m[2])
    return c.done()


def icon_battleaxe(m, w=WOOD):
    c = Canvas()
    c.line(3, 15, 12, 2, w[1], 2)
    c.line(3, 14, 11, 2, w[0])
    c.poly([(6, 1), (10, 4), (14, 2), (15, 8), (11, 7), (8, 10), (5, 6)], m[1])
    c.line(14, 2, 15, 8, m[0])
    c.line(6, 1, 5, 6, m[0])
    c.line(9, 4, 10, 6, m[2])
    return c.done()


def icon_claws(m):
    c = Canvas()
    c.rect(2, 10, 8, 14, C('#5e3a1e'))
    c.line(2, 10, 8, 10, C('#8a5a2e'))
    for k in range(3):
        x0 = 3 + k * 2
        for i in range(8):
            t = i / 7
            c.set(x0 + 7 * t, 9 - 8 * t + 2 * t * t, m[1] if i < 6 else m[0])
            c.set(x0 + 7 * t + 1, 9 - 8 * t + 2 * t * t, m[2])
    return c.done()


def icon_nails(m):
    c = Canvas()
    for (x, y) in ((3, 4), (8, 2), (11, 7), (5, 9)):
        c.line(x, y, x + 3, y + 5, m[1])
        c.line(x - 1, y, x + 1, y - 1, m[0])
        c.set(x + 3, y + 5, m[2])
    return c.done()


def icon_darttip(m):
    c = Canvas()
    for (x, y) in ((3, 9), (8, 4), (10, 11)):
        c.poly([(x, y + 3), (x + 4, y - 1), (x + 5, y), (x + 1, y + 4)], m[1])
        c.line(x + 4, y - 1, x + 5, y - 2, m[0])
        c.set(x, y + 4, m[2])
    return c.done()


def icon_arrowtips(m):
    c = Canvas()
    for (x, y) in ((2, 10), (7, 5), (10, 12), (12, 3)):
        c.poly([(x, y), (x + 3, y - 1.5), (x + 3, y + 1.5)], m[1])
        c.set(x, y, m[0])
        c.set(x + 3, y + 1, m[2])
    return c.done()


def icon_tknife(m):
    c = Canvas()
    for (dx, dy) in ((0, 0), (3, 3)):
        c.line(2 + dx, 11 + dy, 5 + dx, 8 + dy, C('#5e3a1e'), 2)
        c.poly([(5 + dx, 8 + dy), (11 + dx, 1 + dy), (12 + dx, 2 + dy), (7 + dx, 9 + dy)], m[1])
        c.line(6 + dx, 7 + dy, 11 + dx, 1 + dy, m[0])
    return c.done()


def icon_javelin(m):
    c = Canvas()
    for (x, y) in ((2, 12), (6, 13), (10, 14)):
        c.poly([(x, y), (x + 1.5, y - 8), (x + 3, y)], m[1])
        c.line(x + 1.5, y - 8, x + 1.5, y - 1, m[0])
        c.line(x, y, x + 3, y, m[2])
    return c.done()


def icon_bolts(m):
    c = Canvas()
    for (x, y) in ((2, 13), (5, 14), (8, 15)):
        c.line(x, y, x + 6, y - 9, C('#8a6a48'))
        c.line(x + 6, y - 9, x + 7, y - 11, m[1])
        c.set(x + 7, y - 12, m[0])
        c.set(x + 5, y - 9, m[2])
    return c.done()


def icon_limbs(m):
    c = Canvas()
    for i in range(31):
        t = i / 30
        x, y = 1 + 14 * t, 4 + 7 * math.sin(t * math.pi)
        c.set(x, y, m[1])
        c.set(x, y + 1, m[2])
        c.set(x, y - 1, m[0] if 0.2 < t < 0.8 else m[1])
    c.rect(6, 9, 9, 12, m[2])
    return c.done()


def icon_wire():
    c = Canvas()
    b = METAL['bronze']
    for k in range(4):
        c.ell(5 + k * 2, 8, 2.5, 5, b[1])
        c.ell(5 + k * 2, 8, 1.5, 4, None)
        c.set(4 + k * 2, 4, b[0])
    c.line(12, 8, 15, 12, b[2])
    return c.done()


def icon_spit():
    c = Canvas()
    i = METAL['iron']
    c.line(1, 14, 14, 1, i[1], 2)
    c.line(2, 14, 14, 2, i[0])
    c.ell(3, 12.5, 2.2, 2.2, i[2])
    c.ell(3, 12.5, 1, 1, None)
    return c.done()


def icon_lantern():
    c = Canvas()
    i = METAL['iron']
    c.rect(4, 4, 11, 13, i[2])
    c.rect(5, 5, 10, 12, None)
    c.line(7, 5, 7, 12, i[1])
    c.line(8, 5, 8, 12, i[1])
    c.rect(3, 13, 12, 14, i[1])
    c.poly([(4, 4), (8, 1), (11, 4)], i[1])
    c.ell(8, 1, 1.4, 1, i[0])
    return c.done()


def icon_studs():
    c = Canvas()
    st = METAL['steel']
    for (x, y) in ((4, 4), (10, 5), (5, 10), (11, 11), (8, 8)):
        c.ell(x, y, 2, 2, None, shade=st)
    return c.done()


def icon_anvil():
    c = Canvas()
    m = METAL['iron']
    c.poly([(1, 4), (12, 4), (15, 3), (14, 6), (11, 7), (11, 8), (5, 8), (5, 7), (2, 7)], m[1])
    c.line(1, 4, 13, 4, m[0])
    c.rect(6, 8, 10, 11, m[2])
    c.rect(3, 11, 13, 13, m[1])
    c.line(3, 13, 13, 13, m[2])
    return c.done()


def icon_gem_ring(gem):
    c = Canvas()
    c.ell(8, 9, 5.5, 4.5, None, shade=GOLD3)
    c.ell(8, 9.5, 3.5, 2.6, None)
    c.ell(8, 4.5, 2.2, 2, None, shade=gem)
    return c.done()


def icon_gauntlets():
    c = Canvas()
    g = (C('#f8e060'), C('#c8a028'), C('#7a5a10'))
    for x0 in (1, 8):
        c.rect(x0 + 1, 2, x0 + 5, 9, g[1])
        c.rect(x0, 9, x0 + 6, 14, g[2])
        c.line(x0 + 1, 2, x0 + 1, 9, g[0])
        for k in range(4):
            c.set(x0 + 1 + k, 1, g[1])
    return c.done()


def icon_mould():
    c = Canvas()
    m = METAL['iron']
    c.rect(2, 4, 13, 12, m[1])
    c.line(2, 4, 13, 4, m[0])
    c.line(2, 12, 13, 12, m[2])
    for (x, y) in ((5, 7), (10, 7), (5, 10), (10, 10)):
        c.ell(x, y - 0.5, 1.4, 1.2, m[2])
    c.line(13, 8, 15, 8, WOOD[1])
    return c.done()


def icon_cannonball():
    c = Canvas()
    st = METAL['steel']
    for (x, y) in ((5, 10), (11, 10), (8, 5)):
        c.ell(x, y, 3, 3, None, shade=st)
    return c.done()


def icon_superheat():
    c = Canvas()
    c.ell(7, 10, 5.5, 4.5, None, shade=(C('#9c9084'), C('#7c7066'), C('#544a42')))
    for (x, y) in ((5, 9), (8, 11), (9, 8)):
        c.set(x, y, C('#b06048'))
    c.poly([(10, 9), (12, 3), (13, 6), (15, 1), (15, 9)], C('#f06010'))
    c.poly([(12, 9), (13, 6), (14, 9)], C('#f8d040'))
    return c.done()


def icon_fish(body, belly, stripe, scale=1.0):
    c = Canvas()
    c.ell(7, 8, 5.5 * scale, 3 * scale, body[1])
    c.poly([(11.5, 8), (15, 4.5), (15, 11.5)], body[2])
    for x in range(2, 12):
        for y in range(9, 12):
            if c.p[y][x] is not None:
                c.p[y][x] = belly
    if stripe:
        c.line(3, 8, 11, 8, stripe)
    c.line(4, 6, 9, 6, body[0])
    c.set(3, 7, C('#101010'))
    return c.done()


def icon_shrimp(cols):
    c = Canvas()
    for i in range(7):
        a = math.pi * (0.15 + i * 0.13)
        c.ell(8 + 4.5 * math.cos(a), 5 + 5 * math.sin(a), 2.4 - i * 0.18, 2.4 - i * 0.18, cols[1] if i % 2 else cols[0])
    c.poly([(3, 6), (1, 3), (4, 3)], cols[2])
    c.line(12, 4, 14, 1, cols[2])
    return c.done()


def icon_meat(cols, fat):
    c = Canvas()
    c.ell(8, 8.5, 6, 4.8, None, shade=cols)
    for y in range(16):
        for x in range(16):
            if c.p[y][x] is not None and ((x - 8) / 6) ** 2 + ((y - 8.5) / 4.8) ** 2 > 0.62 and y < 8:
                c.p[y][x] = fat
    c.set(6, 9, cols[2])
    c.set(10, 8, cols[2])
    return c.done()


def icon_drumstick(cols):
    c = Canvas()
    c.line(9, 9, 13, 13, C('#f0ece0'), 2)
    c.ell(14, 14, 1.2, 1.2, C('#f0ece0'))
    c.ell(6.5, 6.5, 5, 4.2, None, shade=cols)
    return c.done()


def icon_bones():
    c = Canvas()
    bone = (C('#f4f0e4'), C('#d0c8b4'))
    c.line(4, 12, 12, 4, bone[0], 2)
    for (x, y) in ((3, 12), (4, 13), (12, 3), (13, 4)):
        c.ell(x, y, 1.5, 1.5, bone[0])
    c.line(5, 12, 12, 5, bone[1])
    return c.done()


def icon_hide():
    c = Canvas()
    c.poly([(2, 4), (6, 2), (10, 3), (14, 2), (13, 8), (14, 13), (9, 12), (4, 14), (3, 9)], C('#ecece0'))
    for (x, y, r) in ((6, 6, 1.6), (10, 9, 2), (5, 11, 1.2)):
        c.ell(x, y, r, r, C('#2a2a2a'))
    return c.done()


def icon_ore(fleck, fleck2):
    c = Canvas()
    c.ell(8, 9, 6, 5, None, shade=(C('#9c9084'), C('#7c7066'), C('#544a42')))
    for (x, y) in ((5, 7), (9, 6), (10, 10), (6, 11), (8, 9)):
        c.set(x, y, fleck)
        c.set(x + 1, y, fleck2)
    return c.done()


def icon_tinderbox():
    c = Canvas()
    c.rect(3, 6, 12, 12, C('#a08040'))
    c.rect(3, 4, 12, 6, C('#c8a860'))
    c.line(3, 12, 12, 12, C('#6a5020'))
    c.rect(6, 8, 9, 9, C('#3a3a3a'))
    return c.done()


def icon_net():
    c = Canvas()
    c.line(2, 14, 7, 9, WOOD[1], 2)
    c.ell(10, 6, 4.5, 4.5, C('#d8d0b8'))
    for y in range(16):
        for x in range(16):
            if c.p[y][x] == C('#d8d0b8') and ((x + y) % 3 and (x - y) % 3):
                c.p[y][x] = None
    return c.done('#3a3020')


def icon_rod():
    c = Canvas()
    c.line(2, 14, 14, 2, WOOD[0])
    c.line(2, 14, 6, 10, WOOD[2], 2)
    c.ell(5, 12, 1.2, 1.2, C('#a0a0a0'))
    c.line(14, 2, 14, 9, C('#e0e0e0'))
    return c.done()


def icon_feather():
    c = Canvas()
    c.ell(9, 7, 3, 6, C('#f8f8f8'))
    c.line(5, 14, 11, 3, C('#b0b0b0'))
    c.set(10, 5, C('#d0d0d0'))
    return c.done()


def icon_heart():
    c = Canvas()
    red = (C('#ff8080'), C('#e02828'), C('#901010'))
    c.ell(5, 6, 3.5, 3.5, None, shade=red)
    c.ell(11, 6, 3.5, 3.5, None, shade=red)
    c.poly([(1.6, 7), (14.4, 7), (8, 14)], red[1])
    c.set(4, 5, red[0])
    return c.done()


def icon_fist():
    c = Canvas()
    sk = (C('#f0c098'), C('#d09060'), C('#9a6038'))
    c.rect(4, 4, 12, 11, sk[1])
    for x in (4, 6, 8, 10):
        c.line(x, 4, x + 1, 4, sk[0])
        c.set(x + 1, 7, sk[2])
    c.rect(2, 7, 4, 10, sk[1])
    c.rect(5, 12, 11, 15, C('#b03030'))
    return c.done()


def icon_star():
    c = Canvas()
    pts = []
    for i in range(16):
        a = -math.pi / 2 + i * math.pi / 8
        r = 7 if i % 2 == 0 else 3.2
        pts.append((7.5 + r * math.cos(a), 7.5 + r * math.sin(a)))
    c.poly(pts, C('#f8f0b0'))
    c.ell(7.5, 7.5, 2.4, 2.4, C('#ffffff'))
    return c.done('#6a5a20')


def icon_flame():
    c = Canvas()
    c.poly([(8, 1), (13, 9), (11, 14), (5, 14), (3, 9), (6, 6)], C('#f07020'))
    c.poly([(8, 5), (11, 11), (9, 14), (6, 14), (5, 11)], C('#f8c040'))
    c.ell(8, 12, 1.5, 1.8, C('#fff8c0'))
    return c.done('#701808')


def icon_pot():
    c = Canvas()
    c.ell(8, 10, 6, 4.5, None, shade=(C('#606060'), C('#3a3a3a'), C('#1e1e1e')))
    c.rect(2, 6, 13, 7, C('#505050'))
    c.line(4, 4, 5, 2, C('#e0e0e0'))
    c.line(8, 4, 9, 1, C('#e0e0e0'))
    c.line(11, 4, 12, 2, C('#e0e0e0'))
    return c.done()


# ---- quest item icons ---------------------------------------------------------------------
GREY3 = (C('#d0d0d0'), C('#9a9a9a'), C('#606060'))
WHITE3 = (C('#f8f8f0'), C('#d8d8d0'), C('#a8a8a0'))
GOLD3 = (C('#f8e060'), C('#d8a828'), C('#8a6010'))
PAPER3 = (C('#f4ecd0'), C('#d8c898'), C('#a08860'))


def icon_egg():
    c = Canvas()
    c.ell(8, 8.5, 4.2, 5.5, None, shade=(C('#fffff0'), C('#ece4cc'), C('#c8b890')))
    return c.done()


def icon_bucket(fill=None):
    c = Canvas()
    for i in range(13):           # handle
        a = math.pi * i / 12
        c.set(8 - 6 * math.cos(a), 6 - 5 * math.sin(a), GREY3[2])
    c.poly([(2.5, 6), (13.5, 6), (12, 15), (4, 15)], GREY3[1])
    c.line(3, 7, 4.5, 14, GREY3[0])
    c.line(12.5, 7, 11.5, 14, GREY3[2])
    c.line(3, 10, 13, 10, GREY3[2])
    c.ell(8, 6, 5.5, 1.6, fill or C('#3a3a3a'))
    return c.done()


def icon_pot_clay(fill=None):
    c = Canvas()
    clay = (C('#d08a50'), C('#a86030'), C('#704020'))
    c.ell(8, 10, 6, 5, None, shade=clay)
    c.rect(4, 4, 11, 6, clay[1])
    c.line(4, 4, 11, 4, clay[0])
    c.ell(8, 4.5, 3.5, 1.2, fill or clay[2])
    if fill:
        c.ell(8, 4, 3, 1.5, fill)
    return c.done()


def icon_grain():
    c = Canvas()
    for (x0, lean) in ((5, -1.5), (8, 0), (11, 1.5)):
        c.line(x0, 15, x0 + lean, 6, C('#b0a040'))
        for k in range(4):
            c.ell(x0 + lean * (1 - k * 0.15) - 0.5, 3 + k * 1.6, 1.3, 1.0, C('#f0d060') if k % 2 else C('#d8b040'))
    return c.done()


def icon_shears():
    c = Canvas()
    c.line(3, 3, 11, 11, GREY3[0], 2)
    c.line(11, 3, 5, 9, GREY3[1], 2)
    c.ell(12, 12, 2.5, 2.5, C('#a86030'))
    c.ell(12, 12, 1, 1, None)
    c.ell(4, 11, 2.5, 2.5, C('#a86030'))
    return c.done()


def icon_wool():
    c = Canvas()
    for (x, y, r) in ((6, 8, 4), (10, 7, 4), (8, 11, 4), (11, 11, 3)):
        c.ell(x, y, r, r * 0.85, None, shade=WHITE3)
    return c.done()


def icon_ballwool():
    c = Canvas()
    c.ell(8, 8.5, 5.5, 5.5, None, shade=WHITE3)
    for k in range(3):
        c.line(4 + k * 2, 4 + k, 11, 12 - k * 2, WHITE3[2])
    c.line(13, 11, 15, 15, WHITE3[1])
    return c.done()


def icon_amulet(gem):
    c = Canvas()
    for i in range(17):
        a = math.pi * (0.1 + 0.8 * i / 16)
        c.set(8 + 6 * math.cos(a), 1 + 8 * math.sin(a) - 0.5, GOLD3[1])
    c.ell(8, 11.5, 3, 3, None, shade=gem)
    c.set(7, 10, C('#ffffff'))
    return c.done()


def icon_skull():
    c = Canvas()
    c.ell(8, 7, 5.5, 5, None, shade=WHITE3)
    c.rect(5, 10, 11, 14, WHITE3[1])
    for x in (6, 8, 10):
        c.set(x, 14, C('#3a3a3a'))
    c.ell(6, 8, 1.3, 1.5, C('#2a2a2a'))
    c.ell(10.5, 8, 1.3, 1.5, C('#2a2a2a'))
    c.set(8, 10, C('#2a2a2a'))
    return c.done()


def icon_scroll(mark=None):
    c = Canvas()
    c.rect(3, 3, 12, 13, PAPER3[0])
    c.line(3, 3, 3, 13, PAPER3[1])
    c.ell(3, 3, 1.5, 1.5, PAPER3[2])
    c.ell(12, 13, 1.5, 1.5, PAPER3[2])
    for y in (5, 7, 9, 11):
        c.line(5, y, 10, y, PAPER3[2])
    if mark:
        c.line(8, 7, 11, 10, mark)
        c.line(11, 7, 8, 10, mark)
    return c.done()


def icon_berries(cols):
    c = Canvas()
    c.line(8, 1, 8, 5, C('#5a8a30'))
    c.line(8, 3, 12, 2, C('#5a8a30'))
    for (x, y) in ((6, 7), (10, 7), (8, 10), (5, 11), (11, 11), (8, 14)):
        c.ell(x, y, 2.2, 2.2, None, shade=cols)
    return c.done()


def icon_vial(liq):
    c = Canvas()
    glass = C('#c8e0f0')
    c.rect(6, 1, 9, 2, C('#a86030'))
    c.rect(6, 3, 9, 6, glass)
    c.ell(8, 11, 4.5, 4.5, glass)
    c.ell(8, 11.5, 3.5, 3.2, liq)
    c.set(6, 10, C('#ffffff'))
    return c.done()


def icon_lump(cols):
    c = Canvas()
    c.ell(8, 9.5, 6, 4.5, None, shade=cols)
    c.ell(6, 8, 2.5, 2, cols[0])
    return c.done()


def icon_talisman():
    c = Canvas()
    c.ell(8, 8, 6, 6, None, shade=(C('#b8b8b0'), C('#8a8a84'), C('#5a5a56')))
    for i in range(10):
        a = i * 0.7
        c.set(8 + (1 + i * 0.35) * math.cos(a), 8 + (1 + i * 0.35) * math.sin(a), C('#e8f8ff'))
    return c.done()


def icon_package():
    c = Canvas()
    c.rect(2, 4, 13, 13, C('#b08048'))
    c.line(2, 4, 13, 4, C('#d0a060'))
    c.line(13, 5, 13, 13, C('#7a5226'))
    c.line(7, 4, 7, 13, C('#e8e0c0'))
    c.line(2, 8, 13, 8, C('#e8e0c0'))
    return c.done()


def icon_bead(col):
    c = Canvas()
    c.ell(8, 9, 4, 4, None, shade=col)
    c.ell(8, 9, 1.0, 1.0, None)
    c.set(6, 7, C('#ffffff'))
    return c.done()


def icon_eye():
    c = Canvas()
    c.ell(8, 8.5, 5, 4, C('#e8e8d0'))
    c.ell(8, 8.5, 2.6, 2.6, C('#58a038'))
    c.ell(8, 8.5, 1.2, 1.2, C('#101010'))
    c.line(3, 5, 6, 4, C('#a86030'))
    return c.done()


def icon_onion():
    c = Canvas()
    c.ell(8, 10, 5, 4.8, None, shade=(C('#f0d8a0'), C('#d0a060'), C('#a07038')))
    c.line(8, 1, 8, 5, C('#7aaa40'))
    c.line(7, 1, 9, 5, C('#4e7a28'))
    c.line(6, 14, 10, 14, C('#e8e0c0'))
    return c.done()


def icon_tail():
    c = Canvas()
    pts = [(2 + i, 12 - 6 * math.sin(i / 13 * math.pi * 1.5)) for i in range(13)]
    for (x, y) in pts:
        c.ell(x, y, 1.0, 1.0, C('#e8a0a0'))
    c.ell(13, 11, 2, 1.6, C('#7a5030'))
    return c.done()


def icon_bar(cols):
    c = Canvas()
    c.poly([(2, 12), (5, 6), (14, 6), (13, 12)], cols[1])
    c.poly([(2, 12), (13, 12), (13, 14), (2, 14)], cols[2])
    c.line(5, 6, 14, 6, cols[0])
    c.line(5, 7, 11, 7, cols[0])
    return c.done()


def icon_box(top):
    c = Canvas()
    c.rect(3, 4, 12, 14, C('#d0c8a0'))
    c.rect(3, 4, 12, 6, top)
    c.line(12, 5, 12, 14, C('#908860'))
    c.ell(7.5, 10, 2, 2, top)
    return c.done()


def icon_gauge():
    c = Canvas()
    c.ell(8, 8, 6, 6, None, shade=GREY3)
    c.ell(8, 8, 4.5, 4.5, C('#f0f0e0'))
    c.line(8, 8, 11, 5, C('#d02020'))
    c.rect(7, 14, 9, 15, GREY3[2])
    return c.done()


def icon_tube():
    c = Canvas()
    for i in range(18):
        a = math.pi * 1.4 * i / 17
        c.ell(8 + 5 * math.cos(a), 8 + 5 * math.sin(a), 1.2, 1.2, C('#3a3a3a') if i % 3 else C('#5a5a5a'))
    return c.done()


def icon_oilcan():
    c = Canvas()
    c.ell(7, 11, 5, 3.5, None, shade=GREY3)
    c.rect(2, 8, 12, 11, GREY3[1])
    c.line(10, 8, 15, 2, GREY3[2])
    c.line(4, 8, 6, 5, GREY3[2])
    c.line(6, 5, 9, 5, GREY3[2])
    return c.done()


def icon_key(cols):
    c = Canvas()
    c.ell(4.5, 5, 3.2, 3.2, cols[1])
    c.ell(4.5, 5, 1.3, 1.3, None)
    c.line(6, 7, 13, 14, cols[1], 2)
    c.line(10, 11, 12, 9, cols[2])
    c.line(12, 13, 14, 11, cols[2])
    c.set(3, 3, cols[0])
    return c.done()


def icon_spade():
    c = Canvas()
    c.line(3, 2, 9, 9, WOOD[1], 2)
    c.line(2, 2, 5, 1, WOOD[0], 2)
    c.poly([(8, 8), (12, 7), (15, 12), (11, 15), (7, 11)], GREY3[1])
    c.line(12, 7, 15, 12, GREY3[0])
    return c.done()


def icon_jug():
    c = Canvas()
    clay = (C('#d8b078'), C('#b08048'), C('#7a5228'))
    c.ell(7.5, 10, 5, 4.8, None, shade=clay)
    c.rect(5, 2, 10, 5, clay[1])
    c.line(5, 2, 10, 2, clay[0])
    c.ell(13, 8, 2.2, 3, clay[2])
    c.ell(13, 8, 1, 1.6, None)
    return c.done()


def icon_knife():
    c = Canvas()
    c.line(3, 13, 7, 9, WOOD[1], 2)
    c.poly([(7, 9), (14, 2), (15, 3), (9, 10)], GREY3[1])
    c.line(8, 8, 14, 2, GREY3[0])
    return c.done()


def icon_bowl():
    c = Canvas()
    c.ell(8, 9, 6.5, 4.5, None, shade=WOOD)
    c.rect(1, 4, 15, 8, None)
    c.ell(8, 8, 6.5, 1.8, WOOD[2])
    c.rect(6, 13, 10, 14, WOOD[2])
    return c.done()


def icon_cake_tin():
    c = Canvas()
    c.ell(8, 10, 6.5, 3.5, GREY3[2])
    c.rect(2, 6, 14, 10, GREY3[1])
    c.line(2, 6, 2, 10, GREY3[0])
    c.ell(8, 6, 6.5, 2.5, GREY3[0])
    c.ell(8, 6, 5, 1.5, GREY3[2])
    return c.done()


def icon_chisel():
    c = Canvas()
    c.line(3, 13, 8, 8, WOOD[1], 2)
    c.line(8, 8, 13, 3, GREY3[1], 2)
    c.line(13, 3, 14, 2, GREY3[0])
    return c.done()


def icon_hammer():
    c = Canvas()
    c.line(4, 14, 10, 6, WOOD[1], 2)
    c.poly([(6, 3), (10, 1), (14, 6), (10, 8)], GREY3[1])
    c.line(10, 1, 14, 6, GREY3[0])
    return c.done()


def icon_map():
    c = Canvas()
    c.poly([(2, 4), (6, 3), (10, 4), (14, 3), (14, 12), (10, 13), (6, 12), (2, 13)], PAPER3[1])
    c.line(6, 3, 6, 12, PAPER3[2])
    c.line(10, 4, 10, 13, PAPER3[2])
    c.line(3, 9, 9, 6, C('#4a7a3a'))
    c.set(11, 8, C('#b03030'))
    c.set(12, 7, C('#b03030'))
    return c.done()


def icon_book(cover):
    c = Canvas()
    c.rect(3, 3, 12, 14, cover[1])
    c.line(3, 3, 3, 14, cover[2])
    c.rect(4, 13, 13, 14, PAPER3[0])
    c.line(13, 4, 13, 13, PAPER3[1])
    c.rect(6, 6, 10, 7, GOLD3[1])
    return c.done()


def icon_bottle(glass, label):
    c = Canvas()
    c.rect(7, 1, 8, 4, glass[2])
    c.ell(7.5, 10, 4, 5, None, shade=glass)
    c.rect(5, 9, 10, 11, label)
    return c.done()


def icon_ring():
    c = Canvas()
    c.ell(8, 9, 5.5, 4.5, None, shade=GOLD3)
    c.ell(8, 9.5, 3.5, 2.6, None)
    c.ell(8, 4.5, 1.6, 1.6, C('#f8f8ff'))
    return c.done()


def icon_gem(cols):
    c = Canvas()
    c.poly([(4, 6), (7, 3), (9, 3), (12, 6), (8, 14)], cols[1])
    c.poly([(4, 6), (12, 6), (8, 14)], cols[2])
    c.line(5, 6, 8, 3, cols[0])
    c.set(7, 5, C('#ffffff'))
    return c.done()


def icon_cabbage():
    c = Canvas()
    c.ell(8, 9, 6, 5.5, None, shade=(C('#b8e070'), C('#7ab040'), C('#4a7a28')))
    for k in range(3):
        c.line(8, 13, 4 + k * 4, 5, C('#d8f0a0'))
    return c.done()


def icon_silverlight():
    c = Canvas()
    blade = (C('#f0f8ff'), C('#b8d0e8'), C('#7890b0'))
    c.line(4, 11, 13, 2, blade[1], 2)
    c.line(5, 10, 13, 2, blade[0])
    c.line(2, 9, 6, 13, C('#70c8e0'), 2)
    c.line(2, 14, 4, 12, C('#4a94b0'), 2)
    c.set(13, 1, C('#ffffff'))
    c.set(9, 4, C('#ffffff'))
    return c.done()


def icon_mail(m):
    c = Canvas()
    c.poly([(2, 3), (5, 2), (11, 2), (14, 3), (14, 8), (12, 8), (12, 14), (4, 14), (4, 8), (2, 8)], m[1])
    for y in range(16):
        for x in range(16):
            if c.p[y][x] is not None and (x + y) % 2:
                c.p[y][x] = m[2] if y > 7 else m[0]
    c.line(6, 2, 10, 2, C('#1a1410'))
    c.line(4, 8, 12, 8, C('#5e3a1e'))
    return c.done()



# ---- ranged and magic --------------------------------------------------------------------
BOWWOOD = {
    'normal': (C('#c89058'), C('#9a6430'), C('#603a18')),
    'oak': (C('#d8b070'), C('#a88040'), C('#6a4c20')),
    'willow': (C('#b8b070'), C('#8a8440'), C('#565220')),
    'maple': (C('#e0a050'), C('#b06c28'), C('#703c10')),
}
STRING = C('#f0e8d0')


def icon_bow(w, long):
    """Diagonal bow, bulging up-left, with its string."""
    c = Canvas()
    a, b = ((14, 1), (1, 14)) if long else ((13, 3), (3, 13))
    c.line(a[0], a[1], b[0], b[1], STRING)
    n, bulge = 30, 4.2 if long else 3.4
    for i in range(n + 1):
        t = i / n
        k = math.sin(t * math.pi) * bulge
        x = a[0] + (b[0] - a[0]) * t - k * 0.7
        y = a[1] + (b[1] - a[1]) * t - k * 0.7
        c.set(x, y, w[1])
        c.set(x - 0.7, y - 0.7, w[0] if 0.15 < t < 0.85 else w[1])
    m = 0.5
    k = bulge
    gx, gy = a[0] + (b[0] - a[0]) * m - k * 0.7, a[1] + (b[1] - a[1]) * m - k * 0.7
    c.set(gx, gy, w[2])
    c.set(gx + 0.7, gy + 0.7, w[2])
    return c.done()


def arrow_onto(c, x0, y0, x1, y1, head, shaft=(C('#c89058'), C('#8a5a2e'))):
    """One arrow from (x0, y0) (fletching) to (x1, y1) (point)."""
    c.line(x0, y0, x1, y1, shaft[0])
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    px, py = -uy, ux
    c.poly([(x1 + ux * 1.5, y1 + uy * 1.5), (x1 - ux * 2 + px * 1.8, y1 - uy * 2 + py * 1.8),
            (x1 - ux * 2 - px * 1.8, y1 - uy * 2 - py * 1.8)], head[1])
    c.set(x1, y1, head[0])
    for k in (0, 1):
        fx, fy = x0 + ux * k, y0 + uy * k
        c.set(fx + px * 1.2, fy + py * 1.2, C('#f8f8f0'))
        c.set(fx - px * 1.2, fy - py * 1.2, C('#d84040'))


def icon_arrows(m):
    c = Canvas()
    arrow_onto(c, 2, 12, 11, 3, m)
    arrow_onto(c, 4, 14, 13, 5, m)
    return c.done()


def icon_arrow_proj(d):
    c = Canvas()
    head = METAL['steel']
    if d == 'h':
        arrow_onto(c, 2, 8, 12, 8, head)
    elif d == 'v':
        arrow_onto(c, 8, 13, 8, 3, head)
    else:
        arrow_onto(c, 3, 12, 11, 4, head)
    return c.done()


RUNE_STONE = (C('#c8c8c0'), C('#9a9a92'), C('#62625c'))
DARK_STONE = (C('#6a6a70'), C('#44444a'), C('#26262a'))


def icon_rune(sym, shape, stone=RUNE_STONE):
    """A rune stone with its symbol (shape picks the glyph so similar colours stay apart)."""
    c = Canvas()
    c.poly([(3, 2), (13, 2), (14, 4), (14, 13), (12, 14), (3, 14), (2, 12), (2, 4)], stone[1])
    c.line(3, 2, 13, 2, stone[0])
    c.line(2, 4, 2, 12, stone[0])
    c.line(14, 5, 14, 13, stone[2])
    c.line(4, 14, 12, 14, stone[2])
    if shape == 'swirl':
        c.ell(8, 8, 3.5, 3.5, sym)
        c.ell(8.5, 7.5, 1.6, 1.6, stone[1])
        c.set(10, 11, sym)
    elif shape == 'drop':
        c.poly([(8, 3.5), (11, 9), (8, 12), (5, 9)], sym)
    elif shape == 'leaf':
        c.ell(8, 8, 2.5, 4, sym)
        c.line(8, 5, 8, 12, stone[2])
    elif shape == 'flame':
        c.poly([(8, 3), (11, 9), (10, 12), (6, 12), (5, 9)], sym)
        c.ell(8, 10, 1.4, 1.6, C('#f8e060'))
    elif shape == 'ring':
        c.ell(8, 8, 4, 4, sym)
        c.ell(8, 8, 2, 2, stone[1])
    elif shape == 'cross':
        c.line(5, 5, 11, 11, sym, 2)
        c.line(11, 5, 5, 11, sym, 2)
    elif shape == 'star':
        c.line(8, 3, 8, 13, sym)
        c.line(3, 8, 13, 8, sym)
        c.ell(8, 8, 2, 2, sym)
    elif shape == 'skull':
        c.ell(8, 7, 3.5, 3, sym)
        c.rect(6, 9, 10, 11, sym)
        c.set(7, 7, stone[2])
        c.set(9, 7, stone[2])
    elif shape == 'scales':
        c.line(4, 6, 12, 6, sym)
        c.line(8, 4, 8, 12, sym)
        c.line(4, 6, 4, 9, sym)
        c.line(12, 6, 12, 9, sym)
        c.line(6, 12, 10, 12, sym)
    else:   # diamond
        c.poly([(8, 3.5), (12.5, 8), (8, 12.5), (3.5, 8)], sym)
    return c.done()


def icon_staff(orb=None, knob=None):
    c = Canvas()
    w = WOOD
    c.line(3, 14, 11, 4, w[1], 2)
    c.line(3, 13, 10, 4, w[0])
    if orb:
        c.ell(12, 3, 2.6, 2.6, None, shade=orb)
    else:
        c.ell(11.5, 3.5, 1.6, 1.6, knob or w[2])
    return c.done()


def icon_wizard_hat():
    c = Canvas()
    blue = (C('#6080e0'), C('#3050b0'), C('#1a2a70'))
    c.poly([(9, 1), (13, 12), (4, 12)], blue[1])
    c.line(9, 1, 13, 12, blue[2])
    c.line(9, 1, 5, 11, blue[0])
    c.rect(2, 12, 14, 13, blue[2])
    c.set(8, 6, C('#f8e060'))
    c.set(10, 9, C('#f8e060'))
    c.set(7, 10, C('#f8e060'))
    return c.done()


def icon_orb(cols, core):
    c = Canvas()
    c.ell(8, 8, 4, 4, None, shade=cols)
    c.ell(7, 7, 1.5, 1.5, core)
    return c.done()


def icon_home_ring():
    """Top-left quarter of the home teleport's rune circle (an ellipse on the ground, centred on
    the icon's bottom-right corner); the other quarters are mirrored copies."""
    c = Canvas()
    for i in range(80):
        a = math.pi + (math.pi / 2) * i / 79
        x, y = 16 + 15 * math.cos(a), 16 + 7 * math.sin(a)
        c.set(x, y, C('#a0e0ff'))
        c.set(x + 0.8, y + 0.6, C('#3070d0'))
    for a in (1.15, 1.3, 1.45):     # runes around the ring
        x, y = 16 + 12 * math.cos(math.pi * a), 16 + 5 * math.sin(math.pi * a)
        c.set(x, y, C('#f0f8ff'))
    return c.p


def icon_spark():
    c = Canvas()
    c.line(4, 2, 4, 6, C('#a0e0ff'))
    c.line(2, 4, 6, 4, C('#a0e0ff'))
    c.set(4, 4, C('#ffffff'))
    return c.p


def icon_target():
    c = Canvas()
    c.poly([(3, 6), (13, 6), (8, 12)], C('#f8e040'))
    c.line(3, 6, 13, 6, C('#fff8a0'))
    return c.done()


def icon_teleport():
    c = Canvas()
    c.ell(8, 8, 6, 6, None, shade=(C('#a0d0ff'), C('#4080e0'), C('#1a3a90')))
    c.ell(8, 8, 3.5, 3.5, C('#d8f0ff'))
    c.ell(8, 8, 1.6, 1.6, C('#4080e0'))
    return c.done()


def icon_alch():
    c = Canvas()
    gold = GOLD3
    c.ell(8, 11, 5, 3, None, shade=gold)
    c.poly([(8, 1), (11, 6), (10, 9), (6, 9), (5, 6)], C('#50d040'))
    c.ell(8, 7, 1.4, 1.6, C('#d8ffb0'))
    return c.done()

RAW_SHRIMP = (C('#f8a8a0'), C('#e07870'), C('#a04840'))
COOKED_SHRIMP = (C('#f8b868'), C('#e08030'), C('#a05018'))
BURNT = (C('#5a4a40'), C('#3a2e28'), C('#1e1814'))

ICONS = []   # (name, image)
ICON_DEF = [
    ('COINS', icon_coins()),
    ('LOGS', icon_logs(METAL['wood'], (C('#e0c088'), C('#a07840')))),
    ('OAK_LOGS', icon_logs((C('#c8a060'), C('#a07c40'), C('#6a5020')), (C('#f0d8a0'), C('#b09060')))),
    ('WILLOW_LOGS', icon_logs((C('#9aa060'), C('#767a40'), C('#4a4e24')), (C('#e8e0a8'), C('#a8a068')))),
    ('TINDERBOX', icon_tinderbox()),
    ('BRONZE_AXE', icon_axe(METAL['bronze'])),
    ('IRON_AXE', icon_axe(METAL['iron'])),
    ('STEEL_AXE', icon_axe(METAL['steel'])),
    ('BRONZE_PICK', icon_pick(METAL['bronze'])),
    ('IRON_PICK', icon_pick(METAL['iron'])),
    ('NET', icon_net()),
    ('ROD', icon_rod()),
    ('FEATHER', icon_feather()),
    ('RAW_SHRIMP', icon_shrimp(RAW_SHRIMP)),
    ('SHRIMP', icon_shrimp(COOKED_SHRIMP)),
    ('BURNT_SHRIMP', icon_shrimp(BURNT)),
    ('RAW_ANCHOVY', icon_fish((C('#b0c8e0'), C('#7890b0'), C('#4a5a78')), C('#d8e4f0'), None, 0.8)),
    ('ANCHOVY', icon_fish((C('#d8b078'), C('#b08048'), C('#704a20')), C('#e8d0a0'), None, 0.8)),
    ('RAW_TROUT', icon_fish((C('#a8b898'), C('#788868'), C('#4a5a40')), C('#d8d8c0'), C('#e09090'))),
    ('TROUT', icon_fish((C('#d0a060'), C('#a87838'), C('#6a4818')), C('#e8c890'), C('#c88050'))),
    ('RAW_SALMON', icon_fish((C('#c0c8d0'), C('#8a96a4'), C('#58626e')), C('#f0a890'), C('#f08878'))),
    ('SALMON', icon_fish((C('#e8a070'), C('#c07040'), C('#804018')), C('#f0c8a0'), None)),
    ('BURNT_FISH', icon_fish(BURNT, C('#2a2420'), None)),
    ('RAW_BEEF', icon_meat((C('#f07070'), C('#c83838'), C('#801818')), C('#f8e8e0'))),
    ('COOKED_MEAT', icon_meat((C('#c08048'), C('#8a5428'), C('#5a3010')), C('#d8a870'))),
    ('BURNT_MEAT', icon_meat(BURNT, C('#4a3e36'))),
    ('RAW_CHICKEN', icon_drumstick((C('#f8c8c0'), C('#e09890'), C('#b06860')))),
    ('COOKED_CHICKEN', icon_drumstick((C('#f0c070'), C('#c88838'), C('#8a5418')))),
    ('BONES', icon_bones()),
    ('COWHIDE', icon_hide()),
    ('COPPER_ORE', icon_ore(C('#e08840'), C('#a05020'))),
    ('TIN_ORE', icon_ore(C('#f0f0f0'), C('#b0b0b0'))),
    ('IRON_ORE', icon_ore(C('#b06048'), C('#703020'))),
    ('BRONZE_SWORD', icon_sword(METAL['bronze'])),
    ('IRON_SWORD', icon_sword(METAL['iron'])),
    ('STEEL_SWORD', icon_sword(METAL['steel'])),
    ('BRONZE_HELM', icon_helm(METAL['bronze'])),
    ('IRON_HELM', icon_helm(METAL['iron'])),
    ('STEEL_HELM', icon_helm(METAL['steel'])),
    ('WOODEN_SHIELD', icon_woodshield()),
    ('BRONZE_SHIELD', icon_sqshield(METAL['bronze'])),
    ('IRON_SHIELD', icon_sqshield(METAL['iron'])),
    ('STEEL_SHIELD', icon_sqshield(METAL['steel'])),
    ('LEATHER_BODY', icon_body(METAL['leather'])),
    ('BRONZE_BODY', icon_body(METAL['bronze'])),
    ('IRON_BODY', icon_body(METAL['iron'])),
    ('LEATHER_CHAPS', icon_legs(METAL['leather'])),
    ('BRONZE_LEGS', icon_legs(METAL['bronze'])),
    ('IRON_LEGS', icon_legs(METAL['iron'])),
    ('BOOTS', icon_boots(METAL['leather'])),
    ('MITHRIL_AXE', icon_axe(METAL['mithril'])),
    ('ADAMANT_AXE', icon_axe(METAL['adamant'])),
    ('RUNE_AXE', icon_axe(METAL['rune'])),
    ('STEEL_PICK', icon_pick(METAL['steel'])),
    ('MITHRIL_PICK', icon_pick(METAL['mithril'])),
    ('ADAMANT_PICK', icon_pick(METAL['adamant'])),
    ('RUNE_PICK', icon_pick(METAL['rune'])),
    ('BRONZE_DAGGER', icon_dagger(METAL['bronze'])),
    ('IRON_MACE', icon_mace(METAL['iron'])),
    ('IRON_SCIMITAR', icon_scimitar(METAL['iron'])),
    ('MITHRIL_SCIMITAR', icon_scimitar(METAL['mithril'])),
    ('ADAMANT_SCIMITAR', icon_scimitar(METAL['adamant'])),
    ('RUNE_SCIMITAR', icon_scimitar(METAL['rune'])),
    ('STEEL_LONGSWORD', icon_longsword(METAL['steel'])),
    ('RUNE_LONGSWORD', icon_longsword(METAL['rune'])),
    ('BRONZE_FULL_HELM', icon_fullhelm(METAL['bronze'])),
    ('IRON_FULL_HELM', icon_fullhelm(METAL['iron'])),
    ('MITHRIL_FULL_HELM', icon_fullhelm(METAL['mithril'])),
    ('ADAMANT_FULL_HELM', icon_fullhelm(METAL['adamant'])),
    ('RUNE_FULL_HELM', icon_fullhelm(METAL['rune'])),
    ('BRONZE_CHAINBODY', icon_chainbody(METAL['bronze'])),
    ('IRON_CHAINBODY', icon_chainbody(METAL['iron'])),
    ('STEEL_BODY', icon_body(METAL['steel'])),
    ('MITHRIL_BODY', icon_body(METAL['mithril'])),
    ('ADAMANT_BODY', icon_body(METAL['adamant'])),
    ('RUNE_BODY', icon_body(METAL['rune'])),
    ('BRONZE_SKIRT', icon_skirt(METAL['bronze'])),
    ('IRON_SKIRT', icon_skirt(METAL['iron'])),
    ('STEEL_LEGS', icon_legs(METAL['steel'])),
    ('MITHRIL_LEGS', icon_legs(METAL['mithril'])),
    ('ADAMANT_LEGS', icon_legs(METAL['adamant'])),
    ('RUNE_LEGS', icon_legs(METAL['rune'])),
    ('IRON_KITE', icon_kite(METAL['iron'])),
    ('MITHRIL_KITE', icon_kite(METAL['mithril'])),
    ('ADAMANT_KITE', icon_kite(METAL['adamant'])),
    ('RUNE_KITE', icon_kite(METAL['rune'])),
    ('IRON_BOOTS', icon_boots(METAL['iron'])),
    # skill icons
    ('SK_HEART', icon_heart()),
    ('SK_FIST', icon_fist()),
    ('SK_STAR', icon_star()),
    ('SK_FLAME', icon_flame()),
    ('SK_POT', icon_pot()),
    # quest items
    ('EGG', icon_egg()),
    ('BUCKET', icon_bucket()),
    ('BUCKET_MILK', icon_bucket(C('#f8f8f0'))),
    ('BUCKET_WATER', icon_bucket(C('#4a80cc'))),
    ('POT', icon_pot_clay()),
    ('POT_FLOUR', icon_pot_clay(C('#f8f8f0'))),
    ('GRAIN', icon_grain()),
    ('SHEARS', icon_shears()),
    ('WOOL', icon_wool()),
    ('BALL_WOOL', icon_ballwool()),
    ('GHOSTSPEAK', icon_amulet((C('#a0e0ff'), C('#4090d0'), C('#205080')))),
    ('ACCURACY', icon_amulet((C('#ff9090'), C('#d02020'), C('#801010')))),
    ('SKULL', icon_skull()),
    ('SCROLL', icon_scroll()),
    ('PIRATE_MSG', icon_scroll(C('#d02020'))),
    ('CADAVA', icon_berries((C('#c070e0'), C('#8030b0'), C('#501870')))),
    ('CADAVA_POTION', icon_vial(C('#8030b0'))),
    ('CLAY', icon_lump((C('#c8b8a0'), C('#a09078'), C('#706050')))),
    ('TALISMAN', icon_talisman()),
    ('PACKAGE', icon_package()),
    ('RED_BEAD', icon_bead((C('#ff9090'), C('#d02020'), C('#801010')))),
    ('YELLOW_BEAD', icon_bead((C('#fff090'), C('#e8c020'), C('#a08010')))),
    ('BLACK_BEAD', icon_bead((C('#707070'), C('#3a3a3a'), C('#1a1a1a')))),
    ('WHITE_BEAD', icon_bead(WHITE3)),
    ('EYE_NEWT', icon_eye()),
    ('ONION', icon_onion()),
    ('RAT_TAIL', icon_tail()),
    ('GOBLIN_MAIL', icon_mail(METAL['leather'])),
    ('ORANGE_MAIL', icon_mail((C('#f8b060'), C('#e07820'), C('#904010')))),
    ('BLUE_MAIL', icon_mail((C('#80a0f0'), C('#3050c8'), C('#182878')))),
    ('ORANGE_DYE', icon_vial(C('#e07820'))),
    ('BLUE_DYE', icon_vial(C('#3050c8'))),
    ('GOLD_BAR', icon_bar(GOLD3)),
    ('FISH_FOOD', icon_box(C('#e07820'))),
    ('POISON', icon_vial(C('#58a038'))),
    ('POISON_FOOD', icon_box(C('#58a038'))),
    ('GAUGE', icon_gauge()),
    ('TUBE', icon_tube()),
    ('OIL_CAN', icon_oilcan()),
    ('KEY', icon_key(GREY3)),
    ('GOLD_KEY', icon_key(GOLD3)),
    ('SPADE', icon_spade()),
    ('RUM', icon_bottle((C('#c08048'), C('#8a5428'), C('#5a3010')), C('#f4ecd0'))),
    ('GOLD_RING', icon_ring()),
    ('EMERALD', icon_gem((C('#90f0a0'), C('#30b050'), C('#186830')))),
    ('CABBAGE', icon_cabbage()),
    ('SILVERLIGHT', icon_silverlight()),
    ('JUG', icon_jug()),
    ('KNIFE', icon_knife()),
    ('BOWL', icon_bowl()),
    ('CAKE_TIN', icon_cake_tin()),
    ('CHISEL', icon_chisel()),
    ('HAMMER', icon_hammer()),
    ('NEWCOMER_MAP', icon_map()),
    ('SECURITY_BOOK', icon_book((C('#c04040'), C('#902828'), C('#581818')))),
    # ranged
    ('SHORTBOW', icon_bow(BOWWOOD['normal'], False)),
    ('LONGBOW', icon_bow(BOWWOOD['normal'], True)),
    ('OAK_SHORTBOW', icon_bow(BOWWOOD['oak'], False)),
    ('OAK_LONGBOW', icon_bow(BOWWOOD['oak'], True)),
    ('WILLOW_SHORTBOW', icon_bow(BOWWOOD['willow'], False)),
    ('WILLOW_LONGBOW', icon_bow(BOWWOOD['willow'], True)),
    ('MAPLE_SHORTBOW', icon_bow(BOWWOOD['maple'], False)),
    ('MAPLE_LONGBOW', icon_bow(BOWWOOD['maple'], True)),
    ('BRONZE_ARROW', icon_arrows(METAL['bronze'])),
    ('IRON_ARROW', icon_arrows(METAL['iron'])),
    ('STEEL_ARROW', icon_arrows(METAL['steel'])),
    ('MITHRIL_ARROW', icon_arrows(METAL['mithril'])),
    ('ADAMANT_ARROW', icon_arrows(METAL['adamant'])),
    # magic
    ('AIR_RUNE', icon_rune(C('#f8f8f8'), 'swirl')),
    ('WATER_RUNE', icon_rune(C('#3070e0'), 'drop')),
    ('EARTH_RUNE', icon_rune(C('#8a5a2a'), 'leaf')),
    ('FIRE_RUNE', icon_rune(C('#e04010'), 'flame')),
    ('MIND_RUNE', icon_rune(C('#f09020'), 'ring')),
    ('BODY_RUNE', icon_rune(C('#6070d0'), 'ring')),
    ('CHAOS_RUNE', icon_rune(C('#f0c020'), 'cross')),
    ('DEATH_RUNE', icon_rune(C('#f8f8f0'), 'skull', DARK_STONE)),
    ('STAFF', icon_staff()),
    ('MAGIC_STAFF', icon_staff((C('#d090f0'), C('#9040c0'), C('#501a78')))),
    ('STAFF_AIR', icon_staff(WHITE3)),
    ('STAFF_WATER', icon_staff((C('#80b0ff'), C('#3070e0'), C('#183c90')))),
    ('STAFF_EARTH', icon_staff((C('#90c060'), C('#5a8a30'), C('#30501a')))),
    ('STAFF_FIRE', icon_staff((C('#f8a060'), C('#e04010'), C('#801808')))),
    ('COSMIC_RUNE', icon_rune(C('#f0f040'), 'star')),
    ('LAW_RUNE', icon_rune(C('#2848b0'), 'scales')),
    ('NATURE_RUNE', icon_rune(C('#30a030'), 'leaf')),
    ('BLOOD_RUNE', icon_rune(C('#c01010'), 'drop')),
    ('SOUL_RUNE', icon_rune(C('#b090d0'), 'diamond')),
    ('WRATH_RUNE', icon_rune(C('#e02030'), 'diamond', DARK_STONE)),
    ('SK_BOW', icon_bow(BOWWOOD['maple'], True)),
    ('SK_MAGIC', icon_wizard_hat()),
    # projectiles, the target marker and spellbook pictures
    ('PROJ_ARROW_H', icon_arrow_proj('h')),
    ('PROJ_ARROW_V', icon_arrow_proj('v')),
    ('PROJ_ARROW_D', icon_arrow_proj('d')),
    ('PROJ_WIND', icon_orb(WHITE3, C('#ffffff'))),
    ('PROJ_WATER', icon_orb((C('#80b0ff'), C('#3070e0'), C('#183c90')), C('#d0e8ff'))),
    ('PROJ_EARTH', icon_orb((C('#a0c060'), C('#6a8a30'), C('#3a501a')), C('#d8f0a0'))),
    ('PROJ_FIRE', icon_orb((C('#f8c040'), C('#f06010'), C('#a02008')), C('#fff0a0'))),
    ('PROJ_CURSE', icon_orb((C('#d090f0'), C('#9040c0'), C('#501a78')), C('#f0d0ff'))),
    ('PROJ_BIND', icon_orb((C('#70e070'), C('#30a030'), C('#186018')), C('#c8ffc8'))),
    ('TARGET_MARK', icon_target()),
    ('SPELL_TELE', icon_teleport()),
    ('SPELL_ALCH', icon_alch()),
    ('HT_RING', icon_home_ring()),
    ('HT_SPARK', icon_spark()),
    # smithing (appended so existing icon ids stay put): bars, ores, skill and spell pictures,
    # then the anvils' products
    ('BRONZE_BAR', icon_bar(METAL['bronze'])),
    ('IRON_BAR', icon_bar(METAL['iron'])),
    ('STEEL_BAR', icon_bar(METAL['steel'])),
    ('SILVER_BAR', icon_bar(WHITE3)),
    ('MITHRIL_BAR', icon_bar(METAL['mithril'])),
    ('ADAMANT_BAR', icon_bar(METAL['adamant'])),
    ('RUNE_BAR', icon_bar(METAL['rune'])),
    ('COAL', icon_ore(C('#303030'), C('#141414'))),
    ('SILVER_ORE', icon_ore(C('#f0f0f8'), C('#a8a8b8'))),
    ('GOLD_ORE', icon_ore(C('#f8d848'), C('#b08820'))),
    ('MITHRIL_ORE', icon_ore(C('#7a7ad8'), C('#3a3a80'))),
    ('ADAMANT_ORE', icon_ore(C('#60a868'), C('#2a5a30'))),
    ('RUNE_ORE', icon_ore(C('#78d8f0'), C('#3a8aa8'))),
    ('SK_ANVIL', icon_anvil()),
    ('SPELL_HEAT', icon_superheat()),
    ('BLURITE_ORE', icon_ore(C('#88a8ff'), C('#3050c0'))),
    ('BLURITE_BAR', icon_bar(METAL['blurite'])),
    ('BLURITE_BOLTS', icon_bolts(METAL['blurite'])),
    ('BLURITE_LIMBS', icon_limbs(METAL['blurite'])),
    ('RING_FORGING', icon_gem_ring((C('#ff9090'), C('#d02020'), C('#801010')))),
    ('GOLDSMITH_GAUNTLETS', icon_gauntlets()),
    ('AMMO_MOULD', icon_mould()),
    ('CANNONBALL', icon_cannonball()),
    ('BRONZE_SCIMITAR', icon_scimitar(METAL['bronze'])),
    ('BRONZE_LONGSWORD', icon_longsword(METAL['bronze'])),
    ('BRONZE_2H', icon_twohand(METAL['bronze'])),
    ('BRONZE_MACE', icon_mace(METAL['bronze'])),
    ('BRONZE_WARHAMMER', icon_warhammer(METAL['bronze'])),
    ('BRONZE_BATTLEAXE', icon_battleaxe(METAL['bronze'])),
    ('BRONZE_CLAWS', icon_claws(METAL['bronze'])),
    ('BRONZE_KITE', icon_kite(METAL['bronze'])),
    ('BRONZE_NAILS', icon_nails(METAL['bronze'])),
    ('BRONZE_DART_TIP', icon_darttip(METAL['bronze'])),
    ('BRONZE_ARROWTIPS', icon_arrowtips(METAL['bronze'])),
    ('BRONZE_KNIFE', icon_tknife(METAL['bronze'])),
    ('BRONZE_JAVELIN_TIPS', icon_javelin(METAL['bronze'])),
    ('BRONZE_BOLTS', icon_bolts(METAL['bronze'])),
    ('BRONZE_LIMBS', icon_limbs(METAL['bronze'])),
    ('BRONZE_WIRE', icon_wire()),
    ('IRON_DAGGER', icon_dagger(METAL['iron'])),
    ('IRON_LONGSWORD', icon_longsword(METAL['iron'])),
    ('IRON_2H', icon_twohand(METAL['iron'])),
    ('IRON_WARHAMMER', icon_warhammer(METAL['iron'])),
    ('IRON_BATTLEAXE', icon_battleaxe(METAL['iron'])),
    ('IRON_CLAWS', icon_claws(METAL['iron'])),
    ('IRON_NAILS', icon_nails(METAL['iron'])),
    ('IRON_DART_TIP', icon_darttip(METAL['iron'])),
    ('IRON_ARROWTIPS', icon_arrowtips(METAL['iron'])),
    ('IRON_KNIFE', icon_tknife(METAL['iron'])),
    ('IRON_JAVELIN_TIPS', icon_javelin(METAL['iron'])),
    ('IRON_BOLTS', icon_bolts(METAL['iron'])),
    ('IRON_LIMBS', icon_limbs(METAL['iron'])),
    ('IRON_SPIT', icon_spit()),
    ('OIL_LANTERN_FRAME', icon_lantern()),
    ('STEEL_DAGGER', icon_dagger(METAL['steel'])),
    ('STEEL_SCIMITAR', icon_scimitar(METAL['steel'])),
    ('STEEL_2H', icon_twohand(METAL['steel'])),
    ('STEEL_MACE', icon_mace(METAL['steel'])),
    ('STEEL_WARHAMMER', icon_warhammer(METAL['steel'])),
    ('STEEL_BATTLEAXE', icon_battleaxe(METAL['steel'])),
    ('STEEL_CLAWS', icon_claws(METAL['steel'])),
    ('STEEL_CHAINBODY', icon_chainbody(METAL['steel'])),
    ('STEEL_SKIRT', icon_skirt(METAL['steel'])),
    ('STEEL_FULL_HELM', icon_fullhelm(METAL['steel'])),
    ('STEEL_KITE', icon_kite(METAL['steel'])),
    ('STEEL_NAILS', icon_nails(METAL['steel'])),
    ('STEEL_DART_TIP', icon_darttip(METAL['steel'])),
    ('STEEL_ARROWTIPS', icon_arrowtips(METAL['steel'])),
    ('STEEL_KNIFE', icon_tknife(METAL['steel'])),
    ('STEEL_JAVELIN_TIPS', icon_javelin(METAL['steel'])),
    ('STEEL_BOLTS', icon_bolts(METAL['steel'])),
    ('STEEL_LIMBS', icon_limbs(METAL['steel'])),
    ('STEEL_STUDS', icon_studs()),
    ('MITHRIL_DAGGER', icon_dagger(METAL['mithril'])),
    ('MITHRIL_SWORD', icon_sword(METAL['mithril'])),
    ('MITHRIL_LONGSWORD', icon_longsword(METAL['mithril'])),
    ('MITHRIL_2H', icon_twohand(METAL['mithril'])),
    ('MITHRIL_MACE', icon_mace(METAL['mithril'])),
    ('MITHRIL_WARHAMMER', icon_warhammer(METAL['mithril'])),
    ('MITHRIL_BATTLEAXE', icon_battleaxe(METAL['mithril'])),
    ('MITHRIL_CLAWS', icon_claws(METAL['mithril'])),
    ('MITHRIL_CHAINBODY', icon_chainbody(METAL['mithril'])),
    ('MITHRIL_SKIRT', icon_skirt(METAL['mithril'])),
    ('MITHRIL_HELM', icon_helm(METAL['mithril'])),
    ('MITHRIL_SHIELD', icon_sqshield(METAL['mithril'])),
    ('MITHRIL_NAILS', icon_nails(METAL['mithril'])),
    ('MITHRIL_DART_TIP', icon_darttip(METAL['mithril'])),
    ('MITHRIL_ARROWTIPS', icon_arrowtips(METAL['mithril'])),
    ('MITHRIL_KNIFE', icon_tknife(METAL['mithril'])),
    ('MITHRIL_JAVELIN_TIPS', icon_javelin(METAL['mithril'])),
    ('MITHRIL_BOLTS', icon_bolts(METAL['mithril'])),
    ('MITHRIL_LIMBS', icon_limbs(METAL['mithril'])),
    ('ADAMANT_DAGGER', icon_dagger(METAL['adamant'])),
    ('ADAMANT_SWORD', icon_sword(METAL['adamant'])),
    ('ADAMANT_LONGSWORD', icon_longsword(METAL['adamant'])),
    ('ADAMANT_2H', icon_twohand(METAL['adamant'])),
    ('ADAMANT_MACE', icon_mace(METAL['adamant'])),
    ('ADAMANT_WARHAMMER', icon_warhammer(METAL['adamant'])),
    ('ADAMANT_BATTLEAXE', icon_battleaxe(METAL['adamant'])),
    ('ADAMANT_CLAWS', icon_claws(METAL['adamant'])),
    ('ADAMANT_CHAINBODY', icon_chainbody(METAL['adamant'])),
    ('ADAMANT_SKIRT', icon_skirt(METAL['adamant'])),
    ('ADAMANT_HELM', icon_helm(METAL['adamant'])),
    ('ADAMANT_SHIELD', icon_sqshield(METAL['adamant'])),
    ('ADAMANT_NAILS', icon_nails(METAL['adamant'])),
    ('ADAMANT_DART_TIP', icon_darttip(METAL['adamant'])),
    ('ADAMANT_ARROWTIPS', icon_arrowtips(METAL['adamant'])),
    ('ADAMANT_KNIFE', icon_tknife(METAL['adamant'])),
    ('ADAMANT_JAVELIN_TIPS', icon_javelin(METAL['adamant'])),
    ('ADAMANT_BOLTS', icon_bolts(METAL['adamant'])),
    ('ADAMANT_LIMBS', icon_limbs(METAL['adamant'])),
    ('RUNE_DAGGER', icon_dagger(METAL['rune'])),
    ('RUNE_SWORD', icon_sword(METAL['rune'])),
    ('RUNE_2H', icon_twohand(METAL['rune'])),
    ('RUNE_MACE', icon_mace(METAL['rune'])),
    ('RUNE_WARHAMMER', icon_warhammer(METAL['rune'])),
    ('RUNE_BATTLEAXE', icon_battleaxe(METAL['rune'])),
    ('RUNE_CLAWS', icon_claws(METAL['rune'])),
    ('RUNE_CHAINBODY', icon_chainbody(METAL['rune'])),
    ('RUNE_SKIRT', icon_skirt(METAL['rune'])),
    ('RUNE_HELM', icon_helm(METAL['rune'])),
    ('RUNE_SHIELD', icon_sqshield(METAL['rune'])),
    ('RUNE_NAILS', icon_nails(METAL['rune'])),
    ('RUNE_DART_TIP', icon_darttip(METAL['rune'])),
    ('RUNE_ARROWTIPS', icon_arrowtips(METAL['rune'])),
    ('RUNE_KNIFE', icon_tknife(METAL['rune'])),
    ('RUNE_JAVELIN_TIPS', icon_javelin(METAL['rune'])),
    ('RUNE_BOLTS', icon_bolts(METAL['rune'])),
    ('RUNE_LIMBS', icon_limbs(METAL['rune'])),
]

# Icons are 8bpp sprites whose colours live in OBJ palette entries 128..255 (banks 8-15).
icon_cols = []
for _, img in ICON_DEF:
    for r in img:
        for col in r:
            if col is not None and col not in icon_cols:
                icon_cols.append(col)
# 8bpp icons share 127 palette entries: if over budget, merge the closest pair of colours
# (keeping the more used one) until they fit
if len(icon_cols) > 127:
    uses = {col: 0 for col in icon_cols}
    for _, img in ICON_DEF:
        for r in img:
            for col in r:
                if col is not None:
                    uses[col] += 1
    remap = {}
    while len(icon_cols) > 127:
        best = None
        for i in range(len(icon_cols)):
            for j in range(i + 1, len(icon_cols)):
                a, b = icon_cols[i], icon_cols[j]
                d = sum((x - y) ** 2 for x, y in zip(a, b))
                if best is None or d < best[0]:
                    best = (d, a, b)
        _, a, b = best
        keep, drop = (a, b) if uses[a] >= uses[b] else (b, a)
        icon_cols.remove(drop)
        uses[keep] += uses.pop(drop)
        remap[drop] = keep
        for k, v in list(remap.items()):
            if v == drop:
                remap[k] = keep
    for _, img in ICON_DEF:
        for r in img:
            for x, col in enumerate(r):
                if col in remap:
                    r[x] = remap[col]
    print('icons: merged %d near-duplicate colours' % len(remap), file=sys.stderr)
if len(obj_tiles) % 2:
    obj_tiles.append(tuple(tuple(0 for _ in range(8)) for _ in range(8)))
# Icons are streamed into an OBJ VRAM cache at runtime (src/gfx.c), so only their pixels live
# in ROM: 16x16 at 8bpp = 4 tiles of 64 bytes, in 1D OBJ order.
ICON_INFO = []
icon_words = []
for name, img in ICON_DEF:
    rows = [[0 if col is None else 129 + icon_cols.index(col) for col in r] for r in img]
    for ty in (0, 8):
        for tx in (0, 8):
            for y in range(8):
                r = rows[ty + y][tx:tx + 8]
                icon_words.append(r[0] | (r[1] << 8) | (r[2] << 16) | (r[3] << 24))
                icon_words.append(r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24))
    ICON_INFO.append((name, 0, 0))
ICON_PAL_FLAT = [0] * 128
for i, col in enumerate(icon_cols):
    ICON_PAL_FLAT[1 + i] = gba(col)
print('icons: %d icons, %d colours; obj tiles %d' % (len(ICON_DEF), len(icon_cols), len(obj_tiles)), file=sys.stderr)

DYN_TILE_BASE = len(obj_tiles)

# ----------------------------------------------------------------------------------------
# Font (proportional, 9 rows: caps rows 0-6, descenders 7-8)
# ----------------------------------------------------------------------------------------
FONT_SRC = r"""
A .##. #..# #..# #### #..# #..# #..#
B ###. #..# #..# ###. #..# #..# ###.
C .##. #..# #... #... #... #..# .##.
D ###. #..# #..# #..# #..# #..# ###.
E #### #... #... ###. #... #... ####
F #### #... #... ###. #... #... #...
G .##. #..# #... #.## #..# #..# .###
H #..# #..# #..# #### #..# #..# #..#
I ### .#. .#. .#. .#. .#. ###
J ...# ...# ...# ...# #..# #..# .##.
K #..# #..# #.#. ##.. #.#. #..# #..#
L #... #... #... #... #... #... ####
M #...# ##.## #.#.# #.#.# #...# #...# #...#
N #..# ##.# ##.# #.## #.## #..# #..#
O .##. #..# #..# #..# #..# #..# .##.
P ###. #..# #..# ###. #... #... #...
Q .##. #..# #..# #..# #..# #.#. .#.#
R ###. #..# #..# ###. #.#. #..# #..#
S .### #... #... .##. ...# ...# ###.
T ##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..
U #..# #..# #..# #..# #..# #..# .##.
V #...# #...# #...# #...# .#.#. .#.#. ..#..
W #...# #...# #...# #.#.# #.#.# ##.## #...#
X #...# #...# .#.#. ..#.. .#.#. #...# #...#
Y #...# #...# .#.#. ..#.. ..#.. ..#.. ..#..
Z #### ...# ..#. ..#. .#.. #... ####
a .... .... .##. ...# .### #..# .###
b #... #... ###. #..# #..# #..# ###.
c ... ... .## #.. #.. #.. .##
d ...# ...# .### #..# #..# #..# .###
e .... .... .##. #..# #### #... .##.
f .## #.. ### #.. #.. #.. #..
g .... .... .### #..# #..# #..# .### ...# .##.
h #... #... ###. #..# #..# #..# #..#
i # . # # # # #
j .# .. .# .# .# .# .# .# #.
k #... #... #..# #.#. ##.. #.#. #..#
l #. #. #. #. #. #. .#
m ..... ..... ####. #.#.# #.#.# #.#.# #.#.#
n .... .... ###. #..# #..# #..# #..#
o .... .... .##. #..# #..# #..# .##.
p .... .... ###. #..# #..# #..# ###. #... #...
q .... .... .### #..# #..# #..# .### ...# ...#
r ... ... #.# ##. #.. #.. #..
s .... .... .### #... .##. ...# ###.
t ... .#. ### .#. .#. .#. ..#
u .... .... #..# #..# #..# #..# .###
v ..... ..... #...# #...# .#.#. .#.#. ..#..
w ..... ..... #...# #...# #.#.# #.#.# .#.#.
x .... .... #..# #..# .##. #..# #..#
y .... .... #..# #..# #..# #..# .### ...# .##.
z .... .... #### ..#. .#.. #... ####
0 .##. #..# #.## ##.# #..# #..# .##.
1 .#. ##. .#. .#. .#. .#. ###
2 .##. #..# ...# ..#. .#.. #... ####
3 ###. ...# ...# .##. ...# ...# ###.
4 #..# #..# #..# #### ...# ...# ...#
5 #### #... ###. ...# ...# #..# .##.
6 .##. #... #... ###. #..# #..# .##.
7 #### ...# ..#. ..#. .#.. .#.. .#..
8 .##. #..# #..# .##. #..# #..# .##.
9 .##. #..# #..# .### ...# ...# .##.
. . . . . . . #
, . . . . . . # #
! # # # # # . #
? .##. #..# ...# ..#. .#.. .... .#..
: . . # . . # .
; . . # . . # # #
' # # . . . . .
" #.# #.# ... ... ... ... ...
- ... ... ... ### ... ... ...
+ ... ... .#. ### .#. ... ...
/ ..# ..# .#. .#. .#. #.. #..
( .# #. #. #. #. #. .#
) #. .# .# .# .# .# #.
% #...# ...#. ..#.. .#... #...# ..... .....
* ... #.# .#. #.# ... ... ...
= ... ... ### ... ### ... ...
< ... ..# .#. #.. .#. ..# ...
> ... #.. .#. ..# .#. #.. ...
[ ## #. #. #. #. #. ##
] ## .# .# .# .# .# ##
_ .... .... .... .... .... .... .... ####
# ..... .#.#. ##### .#.#. ##### .#.#. .....
& .#.. #.#. .#.. #.#. #..# #.#. .#.#
"""
font_w = [0] * 96
font_rows = [[0] * 9 for _ in range(96)]
for line in FONT_SRC.strip('\n').split('\n'):
    ch = line[0]
    rows = line[2:].split(' ')
    w = max(len(r) for r in rows)
    i = ord(ch) - 32
    font_w[i] = w
    for y, r in enumerate(rows):
        v = 0
        for x, c in enumerate(r):
            if c == '#':
                v |= 1 << x
        font_rows[i][y] = v
font_w[0] = 3  # space
for i in range(96):
    if font_w[i] == 0:
        font_w[i] = font_w[ord('?') - 32]
        font_rows[i] = font_rows[ord('?') - 32]

# ----------------------------------------------------------------------------------------
# XP table
# ----------------------------------------------------------------------------------------
xp_table = [0] * 100
pts = 0
for lvl in range(1, 100):
    xp_table[lvl] = pts // 4
    pts += int(lvl + 300 * 2 ** (lvl / 7.0))

# ----------------------------------------------------------------------------------------
# Emit C
# ----------------------------------------------------------------------------------------


def tile_words(t):
    words = []
    for r in t:
        v = 0
        for x, c in enumerate(r):
            v |= (c & 15) << (4 * x)
        words.append(v)
    return words


def arr(ctype, name, vals, per=12, fmt='0x%04X'):
    decl = name if '[' in name else '%s[%d]' % (name, len(vals))
    s = ['const %s %s = {' % (ctype, decl)]
    for i in range(0, len(vals), per):
        s.append('  ' + ','.join(fmt % v for v in vals[i:i + per]) + ',')
    s.append('};')
    return '\n'.join(s)


h = ['// generated by tools/gen_assets.py - do not edit', '#pragma once', '#include "gba.h"', '']
world_emit.emit_header(world, h)
for n, v in SPR.items():
    h.append('#define SPR_%s %d' % (n, v))
for i, (n, t, b) in enumerate(ICON_INFO):
    h.append('#define ICON_%s %d' % (n, i))
h.append('#define ICON_COUNT %d' % len(ICON_INFO))
h.append('#define DYN_TILE_BASE %d' % DYN_TILE_BASE)
h.append('#define OBJ_TILE_COUNT %d' % len(obj_tiles))
KINDS = ['COW', 'CHICKEN', 'GOBLIN', 'MAN', 'WOMAN', 'GUARD', 'BANKER', 'SHOPKEEPER', 'GUIDE', 'COOK', 'HANS',
         'WARRIOR', 'DARK_WIZARD', 'BARBARIAN',
         # quests
         'DUKE', 'FRED', 'SHEEP', 'DAIRY_COW', 'AERECK', 'URHNEY', 'GHOST', 'SKELETON', 'ROMEO', 'JULIET',
         'LAWRENCE', 'APOTHECARY', 'DORIC', 'SEDRIDOR', 'AUBURY', 'MIZGOG', 'TRAIBORN', 'IMP', 'HETTY', 'RAT',
         'AGGIE', 'WARTFACE', 'BENTNOZE', 'VERONICA', 'ODDENSTEIN', 'FRANK', 'BARTENDER', 'AMIK', 'BK_GUARD',
         'ARIS', 'PRYSIN', 'ROVIN', 'DELRITH', 'BORDER_GUARD',
         # shopkeepers
         'BOB', 'GERRANT', 'BETTY', 'SWORD_KEEPER', 'HORVIK', 'PEKSA', 'CASSIE', 'WAYNE', 'ZEKE', 'LOUIE',
         'NURMOF', 'LOWE', 'ZAFF']
for i, k in enumerate(KINDS):
    h.append('#define NPC_%s %d' % (k, i))
h.append('#define NPC_KIND_COUNT %d' % len(KINDS))
h.append('''
extern const u32 obj_tiles[OBJ_TILE_COUNT * 8];
extern const u16 obj_pal[16 * 16];
extern const u32 icon_gfx[ICON_COUNT * 64];
extern const u8 font_width[96];
extern const u8 font_rows[96][9];
extern const u32 xp_table[100];
extern const u16 title_pal[16];           // login screen backdrop (tools/gen_title.py)
extern const u32 title_bg[600 * 8];       // panel canvas tiles, 30x20
''')

c = ['// generated by tools/gen_assets.py - do not edit', '#include "assets.h"', '']
world_emit.emit_source(world, c, arr, tile_words, KINDS)
ow = []
for t in obj_tiles:
    if t is None:
        continue
    if t[0] == '8bpp':
        for r in t[1]:
            ow.append(r[0] | (r[1] << 8) | (r[2] << 16) | (r[3] << 24))
            ow.append(r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24))
    else:
        ow += tile_words(t)
c.append(arr('u32', 'obj_tiles[OBJ_TILE_COUNT * 8]', ow, 8, '0x%08X'))
op = []
for b in OBJ_PALS:
    row = [0] * 16
    if b:
        for i, col in enumerate(b):
            row[i + 1] = gba(c5(col) if isinstance(col, str) else col)
    op += row
op[128:256] = ICON_PAL_FLAT
c.append(arr('u16', 'obj_pal[16 * 16]', op, 16))
c.append(arr('u32', 'icon_gfx[ICON_COUNT * 64]', icon_words, 8, '0x%08X'))
c.append(arr('u8', 'font_width[96]', font_w, 16, '%d'))
c.append('const u8 font_rows[96][9] = {')
for r in font_rows:
    c.append('  {' + ','.join('0x%02X' % v for v in r) + '},')
c.append('};')
c.append(arr('u32', 'xp_table[100]', xp_table, 10, '%d'))
import gen_title  # noqa: E402
title_pal, title_words = gen_title.make(font_w, font_rows)
c.append(arr('u16', 'title_pal[16]', [gba(c5(col)) for col in title_pal], 16))
c.append(arr('u32', 'title_bg[600 * 8]', title_words, 8, '0x%08X'))
with open(os.path.join(OUT, 'assets.h'), 'w') as f:
    f.write('\n'.join(h) + '\n')
with open(os.path.join(OUT, 'assets.c'), 'w') as f:
    f.write('\n'.join(c) + '\n')

# spawn list for test scripts: npc index, kind and OSRS tile (X Y plane); objects, items, named
# places, where to stand facing each thing, and the zones
O = world.osrs_of
with open(os.path.join(OUT, 'spawns.txt'), 'w') as f:
    for i, (k, x, y) in enumerate(world.SPAWNS):
        f.write('%d %s %d %d %d\n' % ((i, k) + O(x, y)))
    for (t, x, y) in world.QUEST_OBJS:
        f.write('obj %s %d %d %d\n' % ((t,) + O(x, y)))
    for (it, x, y) in world.ITEM_SPAWNS:
        f.write('item %s %d %d %d\n' % ((it,) + O(x, y)))
    for n, (x, y) in sorted(world.QPOS.items()):
        f.write('place %s %d %d %d\n' % ((n,) + O(x, y)))
    for n, x, y in world.FORGES:
        f.write('obj %s %d %d %d\n' % ((world.TY[y][x],) + O(x, y)))
    # where the player can stand to face each NPC / object / item (DIR the player faces)
    wk = lambda x, y: 0 <= x < world.MW and 0 <= y < world.MH and world.TY[y][x] in world.WALKSET
    done = set()
    for name, x, y in ([(k, x, y) for (k, x, y) in world.SPAWNS] + [('obj:' + t, x, y) for (t, x, y) in world.QUEST_OBJS] +
                       [('item:' + it, x, y) for (it, x, y) in world.ITEM_SPAWNS] + list(world.FORGES)):
        if name in done:
            continue
        done.add(name)
        for d, (dx, dy) in enumerate(((0, 1), (0, -1), (-1, 0), (1, 0))):
            sx, sy = x - dx, y - dy
            if wk(sx, sy) and world.TY[sy][sx] != 'DOOR':
                f.write('stand %s %d %d %d %d\n' % ((name,) + O(sx, sy) + (d,)))
    f.write('world %d %d\n' % (world.WORLD_X0, world.WORLD_Y1))   # px = (X - X0) * 16, py = (Y1 - Y) * 16
    for n, z, ps, plane in world.ZONES:
        f.write('zone %s %d\n' % (n, plane))
    for c in world.CITIES:                  # towns lie in the overworld: their names are aliases
        f.write('zone %s 0\n' % c.name)
import gen_quests  # noqa: E402
gen_quests.emit(OUT, font_w, world, KINDS)

if os.environ.get('MAPDUMP'):
    print(sys.modules['world_gen'].ascii_map(), file=sys.stderr)
