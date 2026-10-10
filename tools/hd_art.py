#!/usr/bin/env python3
"""Remastered terrain art for the 1:1 zones (Lumbridge and the castle floors).

Imported by world_gen.py, which calls render(wg, x, y) for every zone cell and roof_images(wg, g)
for every roof group. Images are 16x16 lists of rows of 5-bit (r, g, b) tuples, None = transparent.

Style: a GBA-era 3/4 view (FireRed/LeafGreen, Golden Sun) over 2007 OSRS colours and geometry.
  - terrain classes blend through a bilinear field over the 3x3 cell neighbourhood with a fixed
    per-pixel dither, so edges are rounded and organic yet repeat exactly (tiles dedupe well);
    the higher class gets a one-pixel rim where it meets a lower one
  - light comes from the upper left: walls, roofs and trees cast soft shadows down and right
  - walls show a cap and a block face toward the viewer; free-standing walls get battlements
  - roofs live on the overlay layer (above sprites) and hide while the player is under them
Every pixel pattern depends only on the cell's neighbourhood and its position inside the cell (or
its 8x8 tile), never on the absolute world position, so the window budget stays low.
"""
import math


def c5(h):
    h = h.lstrip('#')
    return (int(h[0:2], 16) >> 3, int(h[2:4], 16) >> 3, int(h[4:6], 16) >> 3)


P = {k: c5(v) for k, v in dict(
    # grass: Lumbridge olive green pushed toward a brighter GBA green
    g0='#5c9c38', g1='#4a8630', g2='#36682a', g3='#78b448', g4='#a0d064', gs='#2e5a26', gs2='#3e7230',
    # grey Lumbridge paths
    p0='#a69c88', p1='#bcb29c', p2='#877d6b', p3='#6a6252', ps='#7a7262',
    # swamp
    s0='#58663a', s1='#6a7844', s2='#3e4a2a', s3='#4a5a5c', ss='#36402a',
    # water
    w0='#3c70c4', w1='#5c94e0', w2='#b0d8fc', w3='#2a5498', w4='#d8f0ff', wb='#6b5134', wbd='#4e3a24',
    # castle stone
    c0='#8e8a7e', c1='#a8a496', c2='#6e6a60', c3='#4e4a42', c4='#c4c0b2', c5='#383430',
    # floors: warm sandstone flags so rooms read apart from the grey walls
    fl0='#aa9c80', fl1='#c0b294', fl2='#8a7e66', fl3='#665c4a',
    f0='#8a8a92', f1='#a2a2aa', f2='#6c6c76', f3='#52525c', fs='#5e5e68',
    k0='#bab4a4', k1='#5a564e', k2='#d2ccbc', k3='#46423c',
    h0='#a4703a', h1='#bc8648', h2='#82572c', h3='#5c3a1c', hs='#70482a',
    r0='#a82c2c', r1='#c84040', r2='#7e1c1c', gold='#e0b838', gold2='#a8801c',
    # timber-frame walls and doors
    pl0='#e4d8b4', pl1='#c8b890', pl2='#f4ecd0', bm0='#6a4424', bm1='#4a2c14', bm2='#8a5c34',
    gl0='#5c84b8', gl1='#9cc0e4', gl2='#3c5c88',
    # roofs
    sl0='#5e6a7a', sl1='#7a8696', sl2='#465060', sl3='#98a4b2', sl4='#343c48',
    rd0='#a24a2e', rd1='#c0643a', rd2='#7a3420', rd3='#d88454', rd4='#5a2414',
    th0='#b48c46', th1='#d0aa5a', th2='#8a6830', th3='#e6c87a', th4='#664a20',
    kr0='#8c8a82', kr1='#a6a49a', kr2='#6e6c64', kr3='#4c4a44',
    # trees
    t0='#2e6c26', t1='#3e8a30', t2='#5aa83e', t3='#86c858', t4='#1c4618', tk0='#6e4826', tk1='#8e6234',
    tk2='#4a2e16', o0='#4a6e22', o1='#628c2c', o2='#82aa3c', o3='#304a16', o4='#a6c85a',
    # bits
    out='#1c1814', fy='#f8e058', fw='#f8f8f0', fr='#e05050', fp='#c070d8', ir0='#3c3c40', ir1='#5c5c64',
    fi0='#f08020', fi1='#f8d040', paper='#f0e8d0', hd0='#3a7a2c', hd1='#4e9636', hd2='#6cb44a', hd3='#2a5a22',
).items()}

# shared ramps: aliases keep the zone palette (14 banks of 15 colours) packable
for _a, _b in (('gs', 'g2'), ('gs2', 'g2'), ('g4', 'g3'), ('tk0', 'h2'), ('tk1', 'h0'), ('ps', 'p2'), ('ss', 's2'), ('w4', 'w2'), ('fs', 'c2'), ('f0', 'c0'),
               ('f1', 'c1'), ('f2', 'c2'), ('f3', 'c3'), ('k0', 'c4'), ('k2', 'fw'), ('k1', 'c3'), ('k3', 'c5'),
               ('hs', 'h3'), ('bm0', 'h3'), ('bm1', 'c5'), ('bm2', 'h2'), ('pl2', 'paper'), ('gl2', 'sl2'),
               ('kr0', 'c0'), ('kr1', 'c1'), ('kr2', 'c2'), ('kr3', 'c3'), ('o0', 't0'), ('o1', 't1'), ('o2', 't2'),
               ('o3', 't4'), ('o4', 't3'), ('hd0', 't0'), ('hd1', 't1'), ('hd2', 't2'), ('hd3', 't4'), ('sl3', 'c4'),
               ('rd3', 'th1'), ('th3', 'paper'), ('sl4', 'c5'), ('rd4', 'c5'), ('th4', 'h3'), ('wbd', 'h3'),
               ('tk2', 'c5'), ('out', 'c5'), ('ir0', 'c5'), ('ir1', 'c3'), ('gold2', 'h2'), ('r1', 'fr'),
               ('s3', 'w3'), ('pl1', 'th0')):
    P[_a] = P[_b]

# the two water shades the palette shimmer swaps (see water_slots in world_gen.py)
WATER_SWAP = (P['w1'], P['w2'])


def hsh(*v):
    x = 0x9E3779B1
    for a in v:
        x = ((x ^ (a & 0xffffffff)) * 0x85EBCA6B) & 0xffffffff
        x ^= x >> 13
    x = (x * 0xC2B2AE35) & 0xffffffff
    x ^= x >> 16
    return x


def blank():
    return [[None] * 16 for _ in range(16)]


def outline(img, col):
    h, w = len(img), len(img[0])
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


def paste(dst, src, ox=0, oy=0):
    for y, row in enumerate(src):
        for x, c in enumerate(row):
            if c is not None and 0 <= oy + y < len(dst) and 0 <= ox + x < len(dst[0]):
                dst[oy + y][ox + x] = c


# ----------------------------------------------------------------------------------------
# Terrain field
# ----------------------------------------------------------------------------------------
CLASS = {'GRASS': 'grass', 'FLOWERS': 'grass', 'PATH': 'path', 'BRIDGE': 'path', 'SWAMP': 'swamp',
         'SAND': 'path', 'COBBLE': 'path', 'DITCH': 'path', 'WATER': 'water'}
RANK = {'water': 0, 'swamp': 1, 'path': 2, 'grass': 3}
INTERIOR = {'FLOOR', 'WOODFLOOR', 'CARPET', 'CHECKER'}
# fixed dither, 16x16, values -0.5..0.5 (one table for every cell, so tiles repeat)
DITHER = [[((hsh(91, x, y) & 255) / 255.0 - 0.5) for x in range(16)] for y in range(16)]


class Env:
    """Read access to the generator's grids."""

    def __init__(self, wg):
        self.wg = wg

    def t(self, x, y):
        wg = self.wg
        return wg.TY[y][x] if wg.inb(x, y) else 'VOID'

    def g(self, x, y):
        wg = self.wg
        return wg.GR[y][x] if wg.inb(x, y) else 'VOID'

    def roof(self, x, y):
        wg = self.wg
        return wg.ROOF[y][x] if wg.inb(x, y) else 0

    def cls(self, x, y):
        g = self.g(x, y)
        if self.t(x, y) == 'WATER':
            return 'water'
        return CLASS.get(g)


def field_class(env, x, y, px, py):
    """Terrain class at pixel (px, py) of cell (x, y); px/py may be -1 or 16 (neighbour cells)."""
    if px < 0:
        return field_class(env, x - 1, y, px + 16, py)
    if px > 15:
        return field_class(env, x + 1, y, px - 16, py)
    if py < 0:
        return field_class(env, x, y - 1, px, py + 16)
    if py > 15:
        return field_class(env, x, y + 1, px, py - 16)
    me = env.cls(x, y)
    if me is None:
        return None
    # the four cell centres around the pixel and their bilinear weights
    fx, fy = (px + 0.5) / 16.0 - 0.5, (py + 0.5) / 16.0 - 0.5
    ax, ay = (-1 if fx < 0 else 0), (-1 if fy < 0 else 0)
    tx, ty = fx - ax, fy - ay
    w = {}
    for (dx, dy, k) in ((ax, ay, (1 - tx) * (1 - ty)), (ax + 1, ay, tx * (1 - ty)),
                        (ax, ay + 1, (1 - tx) * ty), (ax + 1, ay + 1, tx * ty)):
        c = env.cls(x + dx, y + dy)
        if c is None:
            c = me
        w[c] = w.get(c, 0) + k
    if len(w) == 1:
        return me
    d = DITHER[py][px] * 0.22
    best = max(w, key=lambda c: (w[c] + (d if RANK[c] > RANK[me] else -d if c != me else 0) + RANK[c] * 0.001))
    return best


def grass_px(px, py, tx, ty):
    """Grass with tufts; the pattern is chosen per 8x8 tile (tx, ty = tile coords in the world)."""
    v = hsh(7, tx, ty) % 6
    lx, ly = px & 7, py & 7
    c = P['g0']
    h = hsh(3, lx, ly, v) % 100
    if h < 6:
        c = P['g1']
    elif h < 9:
        c = P['g3']
    tufts = {0: [(2, 5)], 1: [(5, 2)], 2: [(1, 2), (5, 6)], 3: [], 4: [(4, 4)], 5: [(6, 1), (1, 6)]}[v]
    for (bx, by) in tufts:
        # a small "w" tuft: dark blades, light tips
        if (lx, ly) in ((bx, by), (bx + 2, by), (bx + 1, by + 1)):
            c = P['g2']
        elif (lx, ly) in ((bx, by - 1), (bx + 2, by - 1)):
            c = P['g3']
        elif (lx, ly) == (bx + 1, by):
            c = P['g1']
    if v == 3 and (lx, ly) in ((2, 2), (6, 5)):
        c = P['g4']
    return c


def path_px(px, py, tx, ty):
    lx, ly = px & 7, py & 7
    v = hsh(5, tx, ty) % 4
    h = hsh(13, lx, ly, v) % 100
    c = P['p0']
    if h < 10:
        c = P['p2']
    elif h < 18:
        c = P['p1']
    # pebbles: light top, dark bottom
    for (bx, by) in ((1, 1), (5, 4), (2, 6))[:1 + v % 3]:
        if (lx, ly) == (bx, by):
            c = P['p1']
        elif (lx, ly) == (bx, by + 1):
            c = P['p3']
    return c


def swamp_px(px, py, tx, ty):
    lx, ly = px & 7, py & 7
    v = hsh(17, tx, ty) % 4
    h = hsh(19, lx, ly, v) % 100
    if v == 0 and (lx - 4) ** 2 + (ly - 4) ** 2 * 2 < 7:
        return P['s3'] if (lx + ly) % 3 else P['s2']
    if h < 12:
        return P['s2']
    if h < 26:
        return P['s1']
    return P['s0']


SHADE = {'grass': {P['g0']: P['gs2'], P['g1']: P['gs'], P['g2']: P['gs'], P['g3']: P['g1'], P['g4']: P['g1']},
         'path': {P['p0']: P['ps'], P['p1']: P['p2'], P['p2']: P['p3'], P['p3']: P['p3']},
         'swamp': {P['s0']: P['ss'], P['s1']: P['s2'], P['s2']: P['ss'], P['s3']: P['s2']}}
RIM = {'grass': P['g2'], 'path': P['p3'], 'swamp': P['s2']}
GPX = {'grass': grass_px, 'path': path_px, 'swamp': swamp_px}


def water_px(px, py, tx, ty):
    lx, ly = px & 7, py & 7
    v = hsh(23, tx, ty) % 3
    if (ly == 2 and (lx + 2 * v) % 8 in (1, 2, 3)) or (ly == 6 and (lx + 3 + v) % 8 in (0, 1)):
        return P['w1']
    if ly == 1 and (lx + v * 3) % 8 == 2 and v != 1:
        return P['w2']
    return P['w0']


def terrain(env, x, y, x0=0, y0=0):
    """Blended ground for the cell. Pixels whose class is None are left None."""
    img = blank()
    for py in range(16):
        for px in range(16):
            c = field_class(env, x, y, px, py)
            if c is None:
                continue
            tx, ty = (x * 16 + px) >> 3, (y * 16 + py) >> 3
            if c == 'water':
                col = water_px(px, py, tx, ty)
                # shoreline: foam next to land, shadow under a bank to the north
                near = [field_class(env, x, y, px + dx, py + dy) for (dx, dy) in ((0, -1), (-1, 0), (1, 0), (0, 1))]
                up2 = field_class(env, x, y, px, py - 2)
                up3 = field_class(env, x, y, px, py - 3)
                if any(n not in (None, 'water') for n in near):
                    col = P['w4'] if (px + py) % 3 else P['w2']
                elif up2 not in (None, 'water') or up3 not in (None, 'water'):
                    col = P['w3']
            else:
                col = GPX[c](px, py, tx, ty)
                # rim where a higher class meets a lower one
                for (dx, dy) in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                    n = field_class(env, x, y, px + dx, py + dy)
                    if n == 'water':
                        col = P['wb'] if dy >= 0 else P['wbd']
                        break
                    if n is not None and RANK[n] < RANK[c] and c != 'path':
                        col = RIM[c]
                        break
                    if n is not None and c == 'path' and RANK[n] > RANK[c] and dy == -1:
                        col = P['p3']           # path edge in the shade of the grass above
                        break
                # earth bank below land that drops to water (two pixels of bank face)
                if c != 'water' and field_class(env, x, y, px, py + 1) == 'water':
                    col = P['wb']
                elif c != 'water' and field_class(env, x, y, px, py + 2) == 'water':
                    col = P['wbd'] if (px % 4) else P['wb']
            img[py][px] = col
    return img


# ----------------------------------------------------------------------------------------
# Shadows (light from the upper left)
# ----------------------------------------------------------------------------------------
TALL = {'WALL', 'HWALL', 'WWALL', 'SWALL', 'STATUE', 'HEDGE'}


def casts(env, x, y):
    t = env.t(x, y)
    return t in TALL or env.roof(x, y) != 0


def apply_shadow(env, img, x, y, cls):
    sh = SHADE.get(cls)
    if not sh:
        return
    n, w, nw = casts(env, x, y - 1), casts(env, x - 1, y), casts(env, x - 1, y - 1)
    for py in range(16):
        for px in range(16):
            c = img[py][px]
            if c is None or c not in sh:
                continue
            d = (n and py < 3 + (px % 5 == 0)) or (w and px < 3 + (py % 5 == 0)) or (nw and px < 3 and py < 3)
            if d:
                img[py][px] = sh[c]


# ----------------------------------------------------------------------------------------
# Interiors
# ----------------------------------------------------------------------------------------
def floor_px(px, py):
    row = py // 8
    sx = (px + (4 if row % 2 else 0)) % 8
    sy = py % 8
    stone = hsh(31, (px + (4 if row % 2 else 0)) // 8, row) % 3
    if sx == 7 or sy == 7:
        return P['fl3']
    if sx == 0 or sy == 0:
        return P['fl1']
    return (P['fl0'], P['fl0'], P['fl2'])[stone] if (sx * 3 + sy) % 7 else P['fl2']


def checker_px(px, py):
    light = ((px // 8) + (py // 8)) % 2 == 0
    lx, ly = px % 8, py % 8
    if light:
        return P['k2'] if lx == 0 or ly == 0 else P['k0'] if lx < 7 and ly < 7 else P['k1']
    return P['k1'] if lx < 7 and ly < 7 else P['k3']


def wood_px(px, py):
    sy = py % 4
    if sy == 3:
        return P['h3']
    if (px + 5 * (py // 4)) % 16 == 0:
        return P['h3']
    if sy == 0:
        return P['h1']
    return P['h2'] if (px * 7 + py * 3) % 11 == 0 else P['h0']


def carpet_px(px, py):
    if (px + py) % 8 == 0 or (px - py) % 8 == 0:
        return P['r2']
    if (px + py) % 8 == 4 and (px - py) % 8 == 4:
        return P['gold']
    return P['r0']


def interior(env, x, y):
    g = env.g(x, y)
    t = env.t(x, y)
    f = {'FLOOR': floor_px, 'CHECKER': checker_px, 'WOODFLOOR': wood_px, 'CARPET': carpet_px}.get(
        t if t in INTERIOR else g, floor_px)
    img = [[f(px, py) for px in range(16)] for py in range(16)]
    if (t if t in INTERIOR else g) == 'CARPET':
        isc = lambda dx, dy: (env.t(x + dx, y + dy) if env.t(x + dx, y + dy) in INTERIOR else env.g(x + dx, y + dy)) == 'CARPET' \
            and env.t(x + dx, y + dy) not in TALL
        for i in range(16):
            if not isc(-1, 0):
                img[i][0], img[i][1] = P['gold2'], P['gold']
            if not isc(1, 0):
                img[i][15], img[i][14] = P['gold2'], P['gold']
            if not isc(0, -1):
                img[0][i], img[1][i] = P['gold2'], P['gold']
            if not isc(0, 1):
                img[15][i], img[14][i] = P['gold2'], P['gold']
    # walls cast a shadow over the floor below and to the right
    shade = {P['fl0']: P['fl2'], P['fl1']: P['fl2'], P['fl2']: P['fl3'], P['k0']: P['k1'], P['k2']: P['k1'],
             P['k1']: P['k3'], P['h0']: P['hs'], P['h1']: P['h2'], P['h2']: P['h3'], P['r0']: P['r2'],
             P['gold']: P['gold2']}
    n, w = env.t(x, y - 1) in TALL, env.t(x - 1, y) in TALL
    for py in range(16):
        for px in range(16):
            if (n and py < 4) or (w and px < 3):
                img[py][px] = shade.get(img[py][px], img[py][px])
    return img


# ----------------------------------------------------------------------------------------
# Walls
# ----------------------------------------------------------------------------------------
WALLT = {'WALL', 'HWALL', 'WWALL', 'SWALL'}


def is_wall(env, x, y):
    return env.t(x, y) in WALLT or env.t(x, y) in ('BOOTH',) and False


def stone_face(px, py, crenel):
    """Castle stone wall face: cap rows 0-3, blocks below."""
    if py < 4:
        if crenel and py < 2 and (px % 8) in (5, 6, 7):
            return None                                  # gap between merlons
        if py == 3:
            return P['c3']
        if py == 0 or (crenel and py < 2 and (px % 8) == 4):
            return P['c4']
        return P['c1']
    if py == 15:
        return P['c5']
    r = (py - 4) // 4
    bx = (px + (4 if r % 2 else 0)) % 8
    ly = (py - 4) % 4
    if ly == 3 or bx == 7:
        return P['c3']
    if ly == 0:
        return P['c1']
    if bx == 0:
        return P['c1']
    return P['c2'] if (hsh(41, (px + (4 if r % 2 else 0)) // 8, r) % 4 == 0) else P['c0']


def stone_top(env, x, y, crenel):
    """A wall seen from above: dark stone, outlined, lit edge on the upper left. Free-standing
    walls (no roof) get merlons along their exposed sides."""
    img = [[(P['c2'] if hsh(43, px // 4, py // 4, (px + py) // 8) % 4 else P['c3']) for px in range(16)] for py in range(16)]
    open_ = lambda dx, dy: not is_wall(env, x + dx, y + dy) and env.t(x + dx, y + dy) != 'VOID'
    L, R, U, D = open_(-1, 0), open_(1, 0), open_(0, -1), open_(0, 1)
    for i in range(16):
        if L:
            img[i][0], img[i][1] = P['c5'], P['c1']
        if R:
            img[i][15], img[i][14] = P['c5'], P['c3']
        if U:
            img[0][i], img[1][i] = P['c5'], P['c1']
        if D:
            img[15][i], img[14][i] = P['c5'], P['c3']
    if crenel:
        for i in range(16):
            m = (i % 8) < 4
            for (side, on) in (('L', L), ('R', R), ('U', U)):
                if not on:
                    continue
                for k in (2, 3, 4):
                    c = (P['c4'] if k == 2 else P['c1']) if m else (P['c5'] if k == 4 else P['c3'])
                    if side == 'L':
                        img[i][k] = c
                    elif side == 'R':
                        img[i][15 - k] = c if m else P['c3']
                    else:
                        img[k][i] = c
    return img


def timber_face(px, py, window):
    if py < 2:
        return P['bm1'] if py == 1 else P['bm0']
    if py >= 14:
        return P['bm1'] if py == 15 else P['bm0']
    if px in (0, 15):
        return P['bm1'] if px == 15 else P['bm0']
    if window and 4 <= px <= 11 and 4 <= py <= 10:
        if px in (4, 11) or py in (4, 10):
            return P['bm1']
        if px in (7, 8) or py == 7:
            return P['bm0']
        return P['gl1'] if (px < 7 and py < 7) else P['gl0'] if py < 10 else P['gl2']
    if py == 8 and not window:
        return P['bm0']
    return P['pl2'] if py == 2 else P['pl1'] if (px + py) % 9 == 0 else P['pl0']


def timber_top(env, x, y):
    img = [[(P['bm0'] if (py % 4) else P['bm1']) for px in range(16)] for py in range(16)]
    open_ = lambda dx, dy: not is_wall(env, x + dx, y + dy)
    for i in range(16):
        if open_(-1, 0):
            img[i][0] = P['bm1']
            img[i][1] = P['bm2']
        if open_(1, 0):
            img[i][15] = P['bm1']
        if open_(0, -1):
            img[0][i] = P['bm1']
            img[1][i] = P['bm2']
    return img


def wall_image(env, x, y):
    t = env.t(x, y)
    below = env.t(x, y + 1)
    face = not is_wall(env, x, y + 1) and below != 'VOID'
    crenel = env.roof(x, y) == 0
    if t == 'HWALL':
        if face:
            img = [[timber_face(px, py, hsh(47, x, y) % 3 == 0 and env.roof(x, y) == 0 and False) for px in range(16)] for py in range(16)]
            return img
        return timber_top(env, x, y)
    if face:
        img = stone_top(env, x, y, crenel) if False else [[stone_face(px, py, crenel) for px in range(16)] for py in range(16)]
        # sides of the face follow the wall's outline
        for py in range(16):
            if not is_wall(env, x - 1, y):
                img[py][0] = P['c3'] if img[py][0] is not None else None
            if not is_wall(env, x + 1, y):
                img[py][15] = P['c5'] if img[py][15] is not None else None
        return img
    return stone_top(env, x, y, crenel)


def facade_image(env, x, y):
    """South wall of a roofed building: the street side, with windows."""
    t = env.t(x, y)
    if t == 'HWALL':
        win = hsh(53, x, y) % 2 == 0 and is_wall(env, x - 1, y) and is_wall(env, x + 1, y)
        img = [[timber_face(px, py, win) for px in range(16)] for py in range(16)]
    else:
        img = [[stone_face(px, py, False) for px in range(16)] for py in range(16)]
        if hsh(53, x, y) % 3 == 0 and is_wall(env, x - 1, y) and is_wall(env, x + 1, y):
            for py in range(5, 12):
                for px in range(6, 10):
                    img[py][px] = P['c5'] if py > 5 or px in (7, 8) else P['c3']
            for px in range(5, 11):
                img[12][px] = P['c4']
    for py in range(3):                          # the roof's eave shadow
        for px in range(16):
            c = img[py][px]
            img[py][px] = {P['pl0']: P['pl1'], P['pl2']: P['pl1'], P['c1']: P['c2'], P['c4']: P['c0'],
                           P['bm0']: P['bm1']}.get(c, c)
    return img


def door_image(env, x, y):
    horiz = is_wall(env, x - 1, y) or is_wall(env, x + 1, y)
    wall_t = env.t(x - 1, y) if is_wall(env, x - 1, y) else env.t(x + 1, y) if is_wall(env, x + 1, y) else \
        env.t(x, y - 1) if is_wall(env, x, y - 1) else env.t(x, y + 1)
    base = interior(env, x, y) if env.g(x, y) in INTERIOR else terrain(env, x, y)
    for py in range(16):
        for px in range(16):
            if base[py][px] is None:
                base[py][px] = floor_px(px, py)
    if horiz:
        facing = not is_wall(env, x, y + 1) and env.roof(x, y + 1) != env.roof(x, y)
        if facing:
            # a door in the facade: frame, planked door leaf (open: dark doorway)
            img = [r[:] for r in base]
            for py in range(16):
                for px in range(16):
                    if px < 2 or px > 13 or py < 2:
                        img[py][px] = P['bm0'] if wall_t == 'HWALL' else P['c1'] if py < 2 else P['c2']
                    elif py < 15:
                        img[py][px] = P['out'] if py < 4 else P['bm1'] if px in (2, 13) else P['h3'] if (px % 3 == 0) else P['h2']
            img[9][11] = P['gold']
            for px in range(16):
                img[15][px] = P['p2']
            return img
        # an inner door seen from above: threshold across a gap in the wall line
        img = [r[:] for r in base]
        for px in range(16):
            img[0][px] = P['c3']
            img[1][px] = P['h2']
            img[14][px] = P['h2']
            img[15][px] = P['c3']
        return img
    img = [r[:] for r in base]
    for py in range(16):
        img[py][0] = P['c3']
        img[py][1] = P['h2']
        img[py][14] = P['h2']
        img[py][15] = P['c3']
    return img


# ----------------------------------------------------------------------------------------
# Roofs (overlay layer)
# ----------------------------------------------------------------------------------------
ROOF_COLS = {'slate': ('sl0', 'sl1', 'sl2', 'sl3', 'sl4'), 'red': ('rd0', 'rd1', 'rd2', 'rd3', 'rd4'),
             'thatch': ('th0', 'th1', 'th2', 'th3', 'th4'), 'tower': ('sl0', 'sl1', 'sl2', 'sl3', 'sl4')}


def roof_images(wg, g):
    """{cell: 16x16 image} for roof group g: a hipped roof over the footprint minus its facade."""
    env = Env(wg)
    style, cells = wg.ROOFS[g]
    covered = set(c for c in cells if (c[0], c[1] + 1) in cells)       # facade row stays open
    if not covered:
        return {}
    xs, ys = [c[0] for c in covered], [c[1] for c in covered]
    X0, Y0 = min(xs) * 16, min(ys) * 16
    W, H = (max(xs) + 1) * 16 - X0, (max(ys) + 1) * 16 - Y0
    inside = lambda px, py: ((X0 + px) >> 4, (Y0 + py) >> 4) in covered
    # distance (in pixels) to the outside, with the nearest edge's direction
    INF = 10 ** 6
    dist = [[INF] * W for _ in range(H)]
    side = [[None] * W for _ in range(H)]
    from collections import deque
    q = deque()
    for py in range(H):
        for px in range(W):
            if not inside(px, py):
                continue
            for (dx, dy, s) in ((0, -1, 'N'), (0, 1, 'S'), (-1, 0, 'W'), (1, 0, 'E')):
                xx, yy = px + dx, py + dy
                if not (0 <= xx < W and 0 <= yy < H) or not inside(xx, yy):
                    dist[py][px] = 0
                    side[py][px] = s
                    q.append((px, py))
                    break
    while q:
        px, py = q.popleft()
        for (dx, dy) in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            xx, yy = px + dx, py + dy
            if 0 <= xx < W and 0 <= yy < H and inside(xx, yy) and dist[yy][xx] > dist[py][px] + 1:
                dist[yy][xx] = dist[py][px] + 1
                side[yy][xx] = side[py][px]
                q.append((xx, yy))
    out = {}
    if style == 'keep':
        cols = [P['sl0'], P['sl1'], P['sl2'], P['c5']]
        for (cx_, cy_) in covered:
            img = blank()
            for py in range(16):
                for px in range(16):
                    gx, gy = cx_ * 16 + px - X0, cy_ * 16 + py - Y0
                    d = dist[gy][gx]
                    s = side[gy][gx]
                    if d < 4:
                        # parapet with merlons, lit from the upper left
                        along = gx if s in 'NS' else gy
                        merlon = (along % 8) < 5
                        if d == 0:
                            c = P['c3']
                        elif d < 3:
                            c = (P['c4'] if s in 'NW' else P['c1']) if merlon or d == 1 else P['c2']
                        else:
                            c = P['c3'] if s in 'NW' else P['c2']
                    elif d < 7 and s in 'NW':
                        c = cols[2] if d < 6 else cols[3]            # the parapet's shadow
                    else:
                        lx, ly = (gx // 8) % 2, (gy // 8) % 2
                        bx, by = gx % 8, gy % 8
                        c = cols[3] if bx == 7 or by == 7 else cols[1] if bx == 0 or by == 0 else \
                            (cols[0] if (lx + ly) % 2 else cols[0] if hsh(61, gx // 8, gy // 8) % 3 else cols[2])
                    img[py][px] = c
            out[(cx_, cy_)] = img
        return out
    c0, c1, c2, c3, c4 = [P[k] for k in ROOF_COLS[style]]
    for (cx_, cy_) in covered:
        img = blank()
        for py in range(16):
            for px in range(16):
                gx, gy = cx_ * 16 + px - X0, cy_ * 16 + py - Y0
                d, s = dist[gy][gx], side[gy][gx]
                base = {'N': c1, 'W': c1, 'S': c0, 'E': c2}[s]
                band = d % 4
                if d == 0:
                    c = c4                                           # eave outline
                elif d == 1:
                    c = c2
                elif band == 0:
                    c = c2 if s in 'SE' else c0                      # shingle row line
                else:
                    c = base
                    if style == 'thatch' and (gx * 3 + gy * 5) % 7 == 0:
                        c = c3 if s in 'NW' else c2
                    elif style != 'thatch' and band == 1 and ((gx if s in 'NS' else gy) + (d // 4) * 3) % 6 == 0:
                        c = c2                                       # gaps between tiles
                # ridge: where two facets meet, a light line
                for (dx, dy) in ((1, 0), (0, 1)):
                    xx, yy = gx + dx, gy + dy
                    if xx < W and yy < H and side[yy][xx] is not None and side[yy][xx] != s and dist[yy][xx] >= 3 and d >= 3:
                        c = c3
                img[py][px] = c
        out[(cx_, cy_)] = img
    return out


# ----------------------------------------------------------------------------------------
# Objects
# ----------------------------------------------------------------------------------------
def tree_image(kind):
    """16x32: canopy rows 0..19, trunk below."""
    img = [[None] * 16 for _ in range(32)]
    if kind == 'DEADTREE':
        for y in range(14, 31):
            for x in range(7, 9 + (1 if y > 26 else 0)):
                img[y][x] = P['tk1'] if x == 7 else P['tk0']
        for (x0, y0, dx, n) in ((7, 20, -1, 5), (8, 17, 1, 5), (7, 14, -1, 4), (8, 12, 1, 3), (7, 10, 0, 3)):
            x, y = x0, y0
            for i in range(n):
                x += dx
                y -= 1
                if 0 <= x < 16:
                    img[y][x] = P['tk0']
        return outline(img, P['tk2'])
    if kind == 'OAK':
        cols = (P['o3'], P['o0'], P['o1'], P['o2'], P['o4'])
        cx_, cy_, rx, ry = 7.5, 12.0, 7.6, 9.5
    else:
        cols = (P['t4'], P['t0'], P['t1'], P['t2'], P['t3'])
        cx_, cy_, rx, ry = 7.5, 12.5, 7.2, 9.0
    for y in range(19, 31):
        for x in range(6, 10):
            img[y][x] = P['tk1'] if x == 7 else P['tk2'] if x == 9 else P['tk0']
    img[30][5] = img[30][10] = P['tk0']
    can = [[None] * 16 for _ in range(32)]
    lobes = ((cx_, cy_ - 3, 5.0), (cx_ - 3.2, cy_ + 1.5, 4.3), (cx_ + 3.2, cy_ + 1.5, 4.3), (cx_, cy_ + 3, 4.6))
    for y in range(32):
        for x in range(16):
            best = None
            for (lx, ly, r) in lobes:
                d = math.hypot(x + 0.5 - lx, (y + 0.5 - ly) * 1.05) / r
                if d <= 1.0 and (best is None or d < best[0]):
                    best = (d, lx, ly, r)
            if best is None:
                continue
            d, lx, ly, r = best
            # light from the upper left, per lobe, plus a fixed dapple
            nx, ny = (x + 0.5 - lx) / r, (y + 0.5 - ly) / r
            light = -(nx * 0.75 + ny * 0.9) + ((hsh(71, x, y) % 100) - 50) / 220.0 - (y - cy_) * 0.03
            c = cols[4] if light > 0.75 else cols[3] if light > 0.3 else cols[2] if light > -0.25 else cols[1]
            if d > 0.86 and light < 0.1:
                c = cols[1]
            can[y][x] = c
    can = outline(can, cols[0])
    paste(img, can)
    return img


TREE_IMG = {}


def tree(kind):
    if kind not in TREE_IMG:
        TREE_IMG[kind] = tree_image(kind if kind in ('OAK', 'DEADTREE') else 'TREE')
    return TREE_IMG[kind]


def shadow_ellipse(img, cx_, cy_, rx, ry, shade):
    for y in range(16):
        for x in range(16):
            if ((x + 0.5 - cx_) / rx) ** 2 + ((y + 0.5 - cy_) / ry) ** 2 <= 1.0 and img[y][x] in shade:
                img[y][x] = shade[img[y][x]]


def obj_fence(env, x, y, base):
    isf = lambda dx, dy: env.t(x + dx, y + dy) in ('FENCE', 'TOLLGATE')
    L, R, U, D = isf(-1, 0), isf(1, 0), isf(0, -1), isf(0, 1)
    img = [r[:] for r in base]
    if L or R or not (U or D):
        for px in range(0 if L else 6, (16 if R else 10)):
            for (py, c) in ((6, P['h1']), (7, P['h3']), (11, P['h1']), (12, P['h3'])):
                img[py][px] = c
    if U or D:
        for py in range(0 if U else 4, 16 if D else 12):
            img[py][7] = P['h1']
            img[py][8] = P['h2']
            img[py][9] = P['h3']
    for py in range(3, 14):
        for px in range(6, 10):
            img[py][px] = P['h3'] if px == 9 or py == 13 else P['h1'] if px == 6 or py == 3 else P['h0']
    for px in range(10, 13):
        if img[14][px] in SHADE['grass']:
            img[14][px] = SHADE['grass'][img[14][px]]
    return img


def obj_fountain(env, x, y):
    isf = lambda dx, dy: env.t(x + dx, y + dy) == 'FOUNTAIN'
    img = [[water_px(px, py, px >> 3, py >> 3) for px in range(16)] for py in range(16)]
    for py in range(16):
        for px in range(16):
            rim = (px < 3 and not isf(-1, 0)) or (px > 12 and not isf(1, 0)) or \
                  (py < 3 and not isf(0, -1)) or (py > 12 and not isf(0, 1))
            if rim:
                edge_in = (px == 2 and not isf(-1, 0)) or (px == 13 and not isf(1, 0)) or \
                          (py == 2 and not isf(0, -1)) or (py == 13 and not isf(0, 1))
                img[py][px] = P['c3'] if edge_in else P['c4'] if (py == 0 or px == 0) else P['c1'] if (px + py) % 4 else P['c0']
            elif (py < 5 and not isf(0, -1)) or (px < 5 and not isf(-1, 0)):
                img[py][px] = P['w3']
    for (ddx, ddy, ox, oy) in ((1, 1, 13, 13), (-1, 1, 0, 13), (1, -1, 13, 0), (-1, -1, 0, 0)):
        if isf(ddx, 0) and isf(0, ddy) and isf(ddx, ddy):
            for py in range(3):
                for px in range(3):
                    img[oy + py][ox + px] = P['c4'] if py == 0 else P['c1']
            img[min(15, max(0, oy + (-1 if ddy > 0 else 3)))][ox + 1] = P['w4']
    return img


def obj_hedge(env, x, y, base):
    ish = lambda dx: env.t(x + dx, y) == 'HEDGE'
    img = [r[:] for r in base]
    x0 = 0 if ish(-1) else 1
    x1 = 16 if ish(1) else 15
    for py in range(2, 15):
        for px in range(x0, x1):
            top = py < 6
            h = hsh(77, px, py) % 7
            c = P['hd2'] if top and h < 4 else P['hd1'] if h < 5 else P['hd0']
            if py == 14 or (px == x0 and not ish(-1)) or (px == x1 - 1 and not ish(1)):
                c = P['hd3']
            if py == 2:
                c = P['hd3'] if px in (x0,) and not ish(-1) else P['hd2']
            img[py][px] = c
    for (fx, fy) in ((3, 4), (8, 3), (12, 5), (6, 7)):
        img[fy][fx] = P['fy']
    for px in range(x0, x1):
        if img[15][px] in SHADE['grass']:
            img[15][px] = SHADE['grass'][img[15][px]]
    return img


def obj_statue(base):
    img = [r[:] for r in base]
    for py in range(11, 16):
        for px in range(2, 14):
            img[py][px] = P['c4'] if py == 11 else P['c3'] if py == 15 or px == 13 else P['c1'] if px < 4 else P['c0']
    fig = [[None] * 16 for _ in range(16)]
    for py in range(1, 11):
        for px in range(5, 11):
            body = (py < 4 and 6 <= px <= 9) or (4 <= py <= 10 and 5 <= px <= 10)
            if body:
                fig[py][px] = P['c4'] if px < 7 else P['c1'] if px < 9 else P['c2']
    fig[0][7] = fig[0][8] = P['c1']
    for py in range(2, 9):
        fig[py][11] = P['c2']                       # sword
    fig[1][11] = P['c4']
    paste(img, outline(fig, P['c5']))
    return img


def obj_box(base, top, front, edge, y0=4, y1=15, x0=1, x1=15, legs=False):
    img = [r[:] for r in base]
    for py in range(y0, y1):
        for px in range(x0, x1):
            if legs and py > y0 + 5 and x0 + 1 < px < x1 - 2:
                continue
            c = top if py < y0 + 5 else front
            if py == y0 or px == x0:
                c = edge[0]
            elif py == y1 - 1 or px == x1 - 1:
                c = edge[1]
            elif py == y0 + 5:
                c = edge[1]
            img[py][px] = c
    return img


def obj_stairs(env, x, y, base):
    """2x2 spiral staircase; this cell is one quarter of it."""
    left = env.t(x - 1, y) != 'STAIRS'
    top = env.t(x, y - 1) != 'STAIRS'
    ox, oy = (0 if left else 16), (0 if top else 16)
    img = [r[:] for r in base]
    for py in range(16):
        for px in range(16):
            gx, gy = ox + px + 0.5 - 16, oy + py + 0.5 - 16
            r = math.hypot(gx, gy)
            if r > 15.5:
                continue
            a = (math.atan2(gy, gx) + math.pi) / (2 * math.pi)
            step = int(a * 12)
            if r < 3:
                c = P['c3']
            elif r > 14.5:
                c = P['c5']
            else:
                frac = a * 12 - step
                c = P['c4'] if frac < 0.12 else P['c3'] if frac > 0.88 else (P['c1'], P['c0'], P['c2'])[step % 3]
                # deeper (lower) steps are darker
                if step > 8:
                    c = {P['c1']: P['c0'], P['c0']: P['c2'], P['c4']: P['c1']}.get(c, c)
            img[py][px] = c
    return img


def obj_range(base):
    img = [r[:] for r in base]
    for py in range(2, 15):
        for px in range(1, 15):
            edge = px in (1, 14) or py in (2, 14)
            img[py][px] = P['out'] if edge else P['ir1'] if py < 6 else P['ir0']
    for (ax, ay) in ((4, 3), (10, 3)):
        img[ay][ax] = img[ay][ax + 1] = P['out']
    for py in range(8, 12):
        for px in range(4, 12):
            img[py][px] = P['fi1'] if (py == 10 and px % 3 == 1) else P['fi0']
    for px in range(4, 12):
        img[7][px] = P['out']
    return img


def obj_booth(base):
    img = [r[:] for r in base]
    for py in range(1, 16):
        for px in range(16):
            if py < 8:
                c = P['gl1'] if py < 3 and 3 <= px <= 12 else P['gl0'] if 3 <= px <= 12 else P['h2']
                if py == 1:
                    c = P['h3']
            elif py == 8:
                c = P['gold']
            elif py == 15:
                c = P['h3']
            else:
                c = P['h0'] if px % 5 else P['h3']
            img[py][px] = c
    for px in range(5, 11):
        img[10][px] = P['paper']
    return img


def obj_bed(env, x, y, base):
    left = env.t(x - 1, y) != 'BED'
    img = [r[:] for r in base]
    for py in range(1, 16):
        for px in range(16):
            if (left and px == 0) or (not left and px == 15) or py in (1, 15):
                c = P['h3']
            elif left and px < 6:
                c = P['paper'] if py > 2 else P['fw']               # pillow
            elif py < 4:
                c = P['fw']
            else:
                c = P['r1'] if py < 6 else P['r0'] if (px + py) % 6 else P['r2']
            img[py][px] = c
    return img


def obj_pew(base):
    img = [r[:] for r in base]
    for py in range(4, 14):
        for px in range(16):
            c = P['h1'] if py < 6 else P['h0'] if py < 9 else P['h2'] if py < 12 else P['h3']
            if px % 8 == 0 and py >= 9:
                c = P['h3']
            img[py][px] = c
    return img


def obj_altar(base):
    img = [r[:] for r in base]
    for py in range(3, 15):
        for px in range(2, 14):
            img[py][px] = P['fw'] if py < 6 else P['c2'] if py == 14 or px in (2, 13) else P['c4']
    for px in range(4, 12):
        img[6][px] = P['r1']
        img[7][px] = P['r0']
    for py in range(7, 13):
        img[py][7] = img[py][8] = P['gold']
    img[9][6] = img[9][9] = P['gold']
    return img


def obj_grave(base):
    img = [r[:] for r in base]
    stone = [[None] * 16 for _ in range(16)]
    for py in range(3, 14):
        for px in range(4, 12):
            if py < 5 and px in (4, 11):
                continue
            stone[py][px] = P['c4'] if px < 6 or py < 5 else P['c1'] if px < 10 else P['c2']
    for px in range(6, 10):
        stone[7][px] = P['c3']
    for py in range(6, 11):
        stone[py][7] = P['c3']
    paste(img, outline(stone, P['c5']))
    for px in range(4, 14):
        c = img[14][px]
        if c in SHADE['grass']:
            img[14][px] = SHADE['grass'][c]
    return img


def obj_flowers(img, x, y):
    cols = (P['fy'], P['fw'], P['fr'], P['fp'])
    v = hsh(83, x, y)
    for i, (fx, fy) in enumerate(((3, 4), (10, 3), (6, 10), (12, 11), (2, 13))):
        c = cols[(v >> (i * 2)) & 3]
        img[fy][fx] = c
        img[fy - 1][fx] = P['fw'] if c != P['fw'] else P['fy']
        img[fy + 1][fx] = P['g2']


def obj_bush(base):
    img = [r[:] for r in base]
    b = [[None] * 16 for _ in range(16)]
    for (cx_, cy_, rr) in ((8, 8, 5.5), (4.5, 11, 4), (11.5, 11, 4)):
        for y in range(16):
            for x in range(16):
                if (x + 0.5 - cx_) ** 2 + (y + 0.5 - cy_) ** 2 <= rr * rr:
                    light = -((x - 8) * 0.12 + (y - 8) * 0.16) + (hsh(3, x, y) % 10 - 5) / 22
                    b[y][x] = P['t3'] if light > 0.7 else P['t2'] if light > 0.15 else P['t1'] if light > -0.5 else P['t0']
    b[6][7] = P['fr']
    b[10][11] = P['fr']
    paste(img, outline(b, P['t4']))
    return img


def bridge_image(env, x, y):
    ns = env.t(x - 1, y) == 'WATER' and env.t(x + 1, y) == 'WATER'
    img = blank()
    for py in range(16):
        for px in range(16):
            a, b = (py, px) if ns else (px, py)
            img[py][px] = P['h3'] if a % 4 == 3 else (P['h2'] if b % 7 == 0 else P['h1'] if a % 4 == 0 else P['h0'])
    if not ns:
        for px in range(16):
            for (py, k) in ((0, -1), (15, 1)):
                if env.t(x, y + k) == 'WATER':
                    img[py][px] = P['h3']
                    img[1 if py == 0 else 14][px] = P['bm1'] if px % 4 == 0 else P['h2']
    else:
        for py in range(16):
            for (px, k) in ((0, -1), (15, 1)):
                if env.t(x + k, y) == 'WATER':
                    img[py][px] = P['h3']
                    img[py][1 if px == 0 else 14] = P['bm1'] if py % 4 == 0 else P['h2']
    return img


# ----------------------------------------------------------------------------------------
def render(wg, x, y, quest_art=None):
    """(ground image, canopy image for the cell above or None)."""
    env = Env(wg)
    t, g = env.t(x, y), env.g(x, y)
    if t == 'VOID':
        return blank(), None
    if t == 'BRIDGE':
        return bridge_image(env, x, y), None
    if t in WALLT:
        if env.roof(x, y) and (x, y + 1) not in wg.ROOFS[env.roof(x, y)][1]:
            return facade_image(env, x, y), None
        return wall_image(env, x, y), None
    if t == 'DOOR':
        return door_image(env, x, y), None
    if g in INTERIOR or t in INTERIOR:
        base = interior(env, x, y)
        cls = None
    else:
        base = terrain(env, x, y)
        cls = env.cls(x, y)
        for py in range(16):
            for px in range(16):
                if base[py][px] is None:
                    base[py][px] = P['g0']
        apply_shadow(env, base, x, y, cls)
    over = None
    gshade = SHADE.get(cls, {})
    if t in ('TREE', 'OAK', 'WILLOW', 'YEW', 'DEADTREE'):
        ti = tree(t)
        shadow_ellipse(base, 9, 14.5, 7, 2.2, gshade)
        paste(base, ti[16:])
        over = ti[:16]
        if all(c is None for r in over for c in r):
            over = None
        return base, over
    if t == 'FLOWERS':
        obj_flowers(base, x, y)
    elif t == 'FENCE' or t == 'TOLLGATE':
        base = obj_fence(env, x, y, base)
    elif t == 'FOUNTAIN':
        base = obj_fountain(env, x, y)
    elif t == 'HEDGE':
        base = obj_hedge(env, x, y, base)
    elif t == 'STATUE':
        shadow_ellipse(base, 9, 14, 7, 2.5, gshade)
        base = obj_statue(base)
    elif t == 'STAIRS':
        base = obj_stairs(env, x, y, base)
    elif t == 'TABLE':
        base = obj_box(base, P['h1'], P['h2'], (P['h0'], P['h3']), y0=3, y1=14, x0=0 if env.t(x - 1, y) == 'TABLE' else 1,
                       x1=16 if env.t(x + 1, y) == 'TABLE' else 15)
    elif t == 'CRATE':
        base = obj_box(base, P['h1'], P['h0'], (P['h1'], P['h3']), y0=3, y1=15, x0=2, x1=14)
        for py in range(8, 15):
            base[py][7] = P['h3']
    elif t == 'COUNTER':
        base = obj_box(base, P['h1'], P['h2'], (P['h0'], P['h3']), y0=3, y1=15, x0=0, x1=16)
    elif t == 'BED':
        base = obj_bed(env, x, y, base)
    elif t == 'PEW':
        base = obj_pew(base)
    elif t == 'RANGE':
        base = obj_range(base)
    elif t == 'BOOTH':
        base = obj_booth(base)
    elif t == 'ALTAR':
        base = obj_altar(base)
    elif t == 'GRAVE':
        base = obj_grave(base)
    elif t == 'BUSH':
        shadow_ellipse(base, 9, 14, 7, 2, gshade)
        base = obj_bush(base)
    elif quest_art and t in quest_art:
        paste(base, quest_art[t])
    return base, over
