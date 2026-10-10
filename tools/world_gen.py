#!/usr/bin/env python3
"""World map generator: a scaled recreation of the OSRS free-to-play mainland.

Scale: one map cell = 2x2 OSRS tiles. The map covers OSRS x 2928..3343 and y 3137..3552
(208 x 208 cells). Everything is authored in real OSRS coordinates with drawing primitives
(polygons, polylines, rectangles, buildings, scatter), then rendered into 16x16 cell images,
cut into 8x8 tiles, deduplicated and palette-packed.

Runtime model (see src/gfx.c): the map lives in ROM; the world tilemap is streamed into a
VRAM tile cache of 575 slots as the camera moves, so the number of unique tiles in the whole
world is unbounded. check_window_budget() proves offline that no camera position ever needs
more than the cache holds.

Imported by gen_assets.py; exposes emit_header(h) / emit_source(c, arr, tile_words).
"""
import math
import random
import sys

# ----------------------------------------------------------------------------------------
# small helpers (duplicated from gen_assets so this module is standalone)


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
# World colours
# ----------------------------------------------------------------------------------------
WC = {k: c5(v) for k, v in dict(
    G0='#3f8a2e', G1='#4f9c35', G2='#2f6e24', G3='#68b84a',
    FY='#f0dc50', FW='#f4f4ec', FR='#d84848',
    D0='#9c7a4c', D1='#b08c58', D2='#7d5f3a',
    S0='#dcc890', S1='#c4ad72',
    W0='#2f5fa8', W1='#4a80cc', W2='#8cbcf0', W3='#234a86',
    F0='#8a8a90', F1='#a4a4aa', F2='#5e5e66',
    C0='#a8a8a0', C1='#c8c8c0', C2='#7a7a74', C3='#4a4a46',
    H0='#8a5a32', H1='#a06c3e', H2='#5e3c1e',
    K0='#6b4423', K1='#8e5c2e', K2='#43280f',
    L0='#2e6b1f', L1='#3f8a2a', L2='#1d4712', L3='#62ad3e',
    O0='#4c7420', O1='#64902a', O2='#304c14', O3='#8ab040',
    Y0='#6a9a38', Y1='#8cbc4c', Y2='#4a6e26', Y3='#b0d870',
    R0='#7c7066', R1='#9c9084', R2='#544a42',
    CU='#d07838', CUd='#9a5020', TI='#d8d8d0', TId='#a0a0a0', FE='#9c5040', FEd='#6a3020',
    OUT='#1a1610', GOLD='#e0b838', IR0='#3a3a3a', IR1='#5a5a5a', IR2='#222222',
    FI0='#f08020', FI1='#f8d040', PAPER='#ece4cc',
    # new terrain
    DS0='#e0c47c', DS1='#c8aa62', DS2='#f0dca4', DS3='#a88a48',
    SW0='#4c5c2c', SW1='#5e6e36', SW2='#36421e', SW3='#6a5634',
    WD0='#4a3e2e', WD1='#5c4c38', WD2='#30281c', WD3='#78703e',
    WW0='#dcdcd4', WW1='#f6f6ee', WW2='#b4b4ac', WW3='#84847e',
    SS0='#c8a064', SS1='#e0bc80', SS2='#a07a44', SS3='#70522c',
    CP0='#982424', CP1='#bc3838', CP2='#681414',
    YW0='#285a2c', YW1='#387236', YW2='#183a1a', YW3='#548c48',
    CA0='#4a8a3a', CA1='#74b04e', CA2='#2c5a22',
    CR0='#e0c050', CR1='#b8962e', CR2='#7c6420',
    SN0='#e8eef4', SN1='#bccad8',
).items()}

# Colour aliases: merging near-identical shades keeps the 14 shared palette banks packable.
for _a, _b in (('H0', 'K1'), ('H2', 'K2'), ('C0', 'F1'), ('C2', 'F0'), ('S0', 'DS0'), ('S1', 'DS1'),
               ('FW', 'WW1'), ('PAPER', 'WW1'), ('SN0', 'WW1'), ('CA0', 'G0'), ('CA1', 'G3'), ('CA2', 'G2'),
               ('CR0', 'GOLD'), ('CR1', 'D1'), ('CR2', 'D2'), ('Y2', 'O0'), ('Y1', 'O3'), ('YW2', 'L2'),
               ('YW0', 'L0'), ('IR2', 'OUT'), ('SW2', 'O2'), ('DS3', 'D1'), ('SS3', 'K0'), ('SS2', 'H1'),
               ('WW3', 'F0'), ('WW2', 'F1'), ('TId', 'F1'), ('TI', 'WW0'), ('WD2', 'K2')):
    WC[_a] = WC[_b]

# ----------------------------------------------------------------------------------------
# Map geometry
# ----------------------------------------------------------------------------------------
OX, OY = 2928, 3552
MW = MH = 208


def cx(X):
    return (X - OX) // 2


def cy(Y):
    return (OY - Y) // 2


def fpt(X, Y):
    return ((X - OX) / 2.0, (OY - Y) / 2.0)


WALK = ['GRASS', 'FLOWERS', 'PATH', 'COBBLE', 'FLOOR', 'WOODFLOOR', 'CARPET', 'BRIDGE', 'SAND',
        'DESERT', 'SWAMP', 'WILD', 'SNOW', 'DITCH', 'DOOR']
BLOCK = ['WATER', 'WALL', 'HWALL', 'WWALL', 'SWALL', 'FENCE', 'TREE', 'OAK', 'WILLOW', 'YEW',
         'DEADTREE', 'CACTUS', 'STUMP', 'COPPER', 'TIN', 'IRON', 'ROCK_EMPTY', 'BOULDER', 'BOOTH',
         'RANGE', 'COUNTER', 'BUSH', 'FOUNTAIN', 'CROPS', 'STALL', 'ALTAR', 'CLIFF', 'FIREPIT',
         'GRAVE', 'CLAY', 'HOPPER', 'FLOURBIN', 'WHEEL', 'COFFIN', 'CADAVA', 'CAULDRON', 'COMPOST',
         'LOCKDOOR', 'GRILL', 'HOLE', 'DRAIN', 'CHEST', 'TOLLGATE']
TYPES = WALK + BLOCK
T = {n: i for i, n in enumerate(TYPES)}
WALKSET = set(WALK)
WALLS = {'WALL', 'HWALL', 'WWALL', 'SWALL'}
TREES = {'TREE', 'OAK', 'WILLOW', 'YEW', 'DEADTREE'}
ROCKS = {'COPPER', 'TIN', 'IRON', 'CLAY'}

TY = [['GRASS'] * MW for _ in range(MH)]   # cell type (terrain or object)
GR = [['GRASS'] * MW for _ in range(MH)]   # ground under the cell
RES = [[False] * MW for _ in range(MH)]    # reserved: keep scatter away
ROAD = [[False] * MW for _ in range(MH)]   # cells drawn by road()

rng = random.Random(2007)


def inb(x, y):
    return 0 <= x < MW and 0 <= y < MH


def setg(x, y, g):
    if inb(x, y):
        TY[y][x] = g
        GR[y][x] = g


def put(x, y, t):
    if inb(x, y):
        TY[y][x] = t


def reserve(x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if inb(x, y):
                RES[y][x] = True


def orect(X0, Y0, X1, Y1):
    return cx(min(X0, X1)), cy(max(Y0, Y1)), cx(max(X0, X1)), cy(min(Y0, Y1))


def fill_cells(x0, y0, x1, y1, g):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            setg(x, y, g)


def ofill(X0, Y0, X1, Y1, g):
    fill_cells(*orect(X0, Y0, X1, Y1), g)


def opoly(pts, fn):
    P = [fpt(*p) for p in pts]
    ys = [p[1] for p in P]
    for y in range(max(0, int(min(ys))), min(MH, int(max(ys)) + 2)):
        yc = y + 0.5
        xs = []
        for i in range(len(P)):
            (x1, y1), (x2, y2) = P[i], P[(i + 1) % len(P)]
            if (y1 <= yc) != (y2 <= yc):
                xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(max(0, int(math.ceil(xs[i] - 0.5))), min(MW, int(math.floor(xs[i + 1] - 0.5)) + 1)):
                fn(x, y)


def opolyline(pts, w, fn):
    P = [fpt(*p) for p in pts]
    seen = set()
    for i in range(len(P) - 1):
        (x1, y1), (x2, y2) = P[i], P[i + 1]
        n = int(max(abs(x2 - x1), abs(y2 - y1)) * 4) + 1
        for k in range(n + 1):
            fx = x1 + (x2 - x1) * k / n
            fy = y1 + (y2 - y1) * k / n
            bx = int(math.floor(fx - (w - 1) / 2.0))
            by = int(math.floor(fy - (w - 1) / 2.0))
            for dy in range(w):
                for dx in range(w):
                    c = (bx + dx, by + dy)
                    if c not in seen and inb(*c):
                        seen.add(c)
                        fn(*c)


def oellipse(X0, Y0, X1, Y1, fn):
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    rx, ry = (x1 - x0) / 2.0 + 0.5, (y1 - y0) / 2.0 + 0.5
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            wob = 0.12 * math.sin(x * 1.3 + y * 0.7)
            if ((x - mx) / rx) ** 2 + ((y - my) / ry) ** 2 <= 1.0 + wob:
                fn(x, y)


# ----------------------------------------------------------------------------------------
# Higher-level authoring primitives


def road(pts, w=2, g='PATH'):
    def f(x, y):
        t = TY[y][x]
        ROAD[y][x] = True
        if t in ('WATER', 'BRIDGE'):
            setg(x, y, 'BRIDGE')
        elif t in WALLS or t in ('DOOR', 'FLOOR', 'WOODFLOOR', 'CARPET', 'CLIFF'):
            pass
        elif g == 'PATH' and GR[y][x] in ('COBBLE',):
            pass
        else:
            setg(x, y, g)
    opolyline(pts, w, f)


def river(pts, w=3):
    opolyline(pts, w, lambda x, y: setg(x, y, 'WATER'))


def building(X0, Y0, X1, Y1, wall='WALL', floor='FLOOR', doors=(), margin=1):
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    return cbuilding(x0, y0, x1, y1, wall, floor, doors, margin)


def cbuilding(x0, y0, x1, y1, wall='WALL', floor='FLOOR', doors=(), margin=1):
    reserve(x0 - margin, y0 - margin, x1 + margin, y1 + margin)
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            edge = x in (x0, x1) or y in (y0, y1)
            TY[y][x] = wall if edge else floor
            GR[y][x] = floor
    r = (x0, y0, x1, y1)
    for d in doors:
        door(r, *d)
    return r


def door(r, side, v, outside='PATH'):
    """side N/S/E/W, v = OSRS coordinate along the wall (x for N/S, y for E/W)."""
    x0, y0, x1, y1 = r
    if side in 'NS':
        x = min(max(cx(v), x0 + 1), x1 - 1)
        y, oy = (y0, y0 - 1) if side == 'N' else (y1, y1 + 1)
        ox = x
    else:
        y = min(max(cy(v), y0 + 1), y1 - 1)
        x, ox = (x0, x0 - 1) if side == 'W' else (x1, x1 + 1)
        oy = y
    TY[y][x] = 'DOOR'
    if inb(ox, oy):
        if TY[oy][ox] not in WALKSET:
            g = GR[oy][ox] if GR[oy][ox] in WALKSET else outside
            setg(ox, oy, g)
        reserve(ox - 1, oy - 1, ox + 1, oy + 1)
    return (x, y)


def wall_line(x0, y0, x1, y1, wall='WALL', doors=()):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if (x, y) not in doors:
                TY[y][x] = wall
    for (x, y) in doors:
        TY[y][x] = 'DOOR'


SPAWNS = []


def service(r, side, obj, npc):
    """Bank booths / shop counter row along one wall, with staff standing behind it."""
    x0, y0, x1, y1 = r
    ix0, iy0, ix1, iy1 = x0 + 1, y0 + 1, x1 - 1, y1 - 1
    cells = []
    if side in 'NS':
        srow = iy0 if side == 'N' else iy1
        orow = srow + (1 if side == 'N' else -1)
        a, b = (ix0 + 1, ix1 - 1) if ix1 - ix0 >= 4 else (ix0, ix1)
        for x in range(a, b + 1):
            TY[orow][x] = obj
            cells.append((x, srow))
    else:
        scol = ix0 if side == 'W' else ix1
        ocol = scol + (1 if side == 'W' else -1)
        a, b = (iy0 + 1, iy1 - 1) if iy1 - iy0 >= 4 else (iy0, iy1)
        for y in range(a, b + 1):
            TY[y][ocol] = obj
            cells.append((scol, y))
    for i, (x, y) in enumerate(cells):
        if i % 2 == 0 or len(cells) <= 2:
            if len([s for s in SPAWNS if s[0] == npc and abs(s[1] - x) + abs(s[2] - y) < 2]) == 0:
                SPAWNS.append((npc, x, y))
    return cells


def fence_line(pts, keep_roads=True):
    def f(x, y):
        if TY[y][x] in ('PATH', 'COBBLE', 'BRIDGE', 'DOOR') or TY[y][x] in WALLS or TY[y][x] == 'WATER':
            return
        TY[y][x] = 'FENCE'
    opolyline(pts, 1, f)


def fence_rect(X0, Y0, X1, Y1, gates=()):
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    reserve(x0, y0, x1, y1)
    for x in range(x0, x1 + 1):
        for y in (y0, y1):
            if TY[y][x] not in ('PATH', 'COBBLE', 'BRIDGE'):
                TY[y][x] = 'FENCE'
    for y in range(y0, y1 + 1):
        for x in (x0, x1):
            if TY[y][x] not in ('PATH', 'COBBLE', 'BRIDGE'):
                TY[y][x] = 'FENCE'
    for side, v in gates:
        if side in 'NS':
            x, y = cx(v), (y0 if side == 'N' else y1)
        else:
            x, y = (x0 if side == 'W' else x1), cy(v)
        TY[y][x] = GR[y][x] if GR[y][x] in WALKSET else 'GRASS'
        oy = y + (-1 if side == 'N' else 1 if side == 'S' else 0)
        ox = x + (-1 if side == 'W' else 1 if side == 'E' else 0)
        reserve(ox - 1, oy - 1, ox + 1, oy + 1)
    return (x0, y0, x1, y1)


def scatter(X0, Y0, X1, Y1, dens, kinds, on=('GRASS',), seed=0):
    r = random.Random(seed or hsh(X0, Y0, X1, Y1))
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    tot = sum(w for _, w in kinds)
    for y in range(max(0, y0), min(MH, y1 + 1)):
        for x in range(max(0, x0), min(MW, x1 + 1)):
            if TY[y][x] in on and GR[y][x] == TY[y][x] and not RES[y][x] and r.random() < dens:
                k = r.random() * tot
                for t, w in kinds:
                    k -= w
                    if k <= 0:
                        put(x, y, t)
                        break


def vnoise(x, y, seed):
    """Smooth value noise in [0,1]."""
    xi, yi = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - xi, y - yi
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    v = lambda a, b: (hsh(seed, a, b) & 0xffff) / 65535.0
    top = v(xi, yi) * (1 - fx) + v(xi + 1, yi) * fx
    bot = v(xi, yi + 1) * (1 - fx) + v(xi + 1, yi + 1) * fx
    return top * (1 - fy) + bot * fy


def forest(X0, Y0, X1, Y1, dens, kinds, on=('GRASS',), seed=1, scale=4.5):
    """Clumped woodland: tree probability follows smooth noise so trees form groves and glades."""
    r = random.Random(seed)
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    tot = sum(w for _, w in kinds)
    for y in range(max(0, y0), min(MH, y1 + 1)):
        for x in range(max(0, x0), min(MW, x1 + 1)):
            if TY[y][x] not in on or GR[y][x] != TY[y][x] or RES[y][x]:
                continue
            n = 0.65 * vnoise(x / scale, y / scale, seed) + 0.35 * vnoise(x / 2.0, y / 2.0, seed + 7)
            p = dens * max(0.0, min(1.0, (n - 0.38) / 0.3))
            if r.random() < p:
                k = r.random() * tot
                for t, w in kinds:
                    k -= w
                    if k <= 0:
                        put(x, y, t)
                        break


def oput(X, Y, t):
    put(cx(X), cy(Y), t)


def mine(X0, Y0, X1, Y1, ores, ground='PATH', boulders=2, seed=0):
    oellipse(X0, Y0, X1, Y1, lambda x, y: setg(x, y, ground))
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    reserve(x0, y0, x1, y1)
    r = random.Random(seed or hsh(X0, Y0))
    free = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
            if TY[y][x] == ground and all(TY[y + dy][x + dx] in WALKSET for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if inb(x + dx, y + dy))]
    r.shuffle(free)
    used = set()
    for t, n in list(ores) + [('BOULDER', boulders)]:
        k = 0
        for (x, y) in free:
            if k >= n:
                break
            if (x, y) in used or any((x + dx, y + dy) in used for dx in (-1, 0, 1) for dy in (-1, 0, 1)) and r.random() < 0.7:
                continue
            used.add((x, y))
            put(x, y, t)
            k += 1


def infill(X0, Y0, X1, Y1, n, wall='HWALL', floor='WOODFLOOR', seed=1, ground=('GRASS', 'COBBLE', 'FLOWERS')):
    """Fill empty city blocks with houses that keep a one-cell lane around them and open onto
    a road (or any walkable side)."""
    r = random.Random(seed)
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    built = 0
    for attempt in range(n * 60):
        if built >= n:
            break
        w, h = r.randint(4, 6), r.randint(4, 5)
        bx, by = r.randint(x0, x1 - w + 1), r.randint(y0, y1 - h + 1)
        ok = True
        for y in range(by - 1, by + h + 1):
            for x in range(bx - 1, bx + w + 1):
                inside = bx <= x < bx + w and by <= y < by + h
                if not inb(x, y):
                    ok = False
                elif inside:
                    ok = not ROAD[y][x] and TY[y][x] in ground and GR[y][x] in ground
                else:   # the lane around the house only has to stay walkable
                    ok = TY[y][x] in WALKSET and TY[y][x] != 'DOOR' and not any(
                        inb(x + ax, y + ay) and TY[y + ay][x + ax] == 'DOOR' for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if not ok:
                    break
            if not ok:
                break
        if not ok:
            continue
        sides = []
        for side in 'SNEW':
            if side in 'NS':
                yy = by + h if side == 'S' else by - 1
                cells = [(x, yy) for x in range(bx + 1, bx + w - 1)]
            else:
                xx = bx + w if side == 'E' else bx - 1
                cells = [(xx, y) for y in range(by + 1, by + h - 1)]
            road_cells = [c for c in cells if inb(*c) and ROAD[c[1]][c[0]]]
            sides.append((len(road_cells) > 0, side, (road_cells or cells)[len(road_cells or cells) // 2]))
        sides.sort(key=lambda s: (not s[0], r.random()))
        _, side, (ox, oy) = sides[0]
        rect = cbuilding(bx, by, bx + w - 1, by + h - 1, wall, floor, (), margin=0)
        if side in 'NS':
            dx_, dy_ = ox, (by if side == 'N' else by + h - 1)
        else:
            dx_, dy_ = (bx if side == 'W' else bx + w - 1), oy
        TY[dy_][dx_] = 'DOOR'
        built += 1
    return built


def spawn(kind, X, Y, cell=False):
    x, y = (X, Y) if cell else (cx(X), cy(Y))
    for rad in range(0, 5):
        for dy in range(-rad, rad + 1):
            for dx in range(-rad, rad + 1):
                if max(abs(dx), abs(dy)) != rad:
                    continue
                xx, yy = x + dx, y + dy
                if inb(xx, yy) and TY[yy][xx] in WALKSET and TY[yy][xx] not in ('DOOR', 'BRIDGE') and REACH[yy][xx] and \
                        not any(s[1] == xx and s[2] == yy for s in SPAWNS):
                    SPAWNS.append((kind, xx, yy))
                    return (xx, yy)
    raise SystemExit('no spawn cell for %s at %d,%d' % (kind, X, Y))


def spawn_area(kind, n, X0, Y0, X1, Y1, seed=0):
    r = random.Random(seed or hsh(n, X0, Y0, len(kind)))
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    cand = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
            if inb(x, y) and TY[y][x] in WALKSET and TY[y][x] not in ('DOOR', 'BRIDGE') and REACH[y][x]]
    r.shuffle(cand)
    k = 0
    for (x, y) in cand:
        if k >= n:
            break
        if any(s[1] == x and s[2] == y for s in SPAWNS):
            continue
        SPAWNS.append((kind, x, y))
        k += 1
    assert k == n, (kind, X0, Y0, k)


# ========================================================================================
# THE MAP (all coordinates are OSRS world coordinates unless noted)
# ========================================================================================

# --- Wilderness (north of the ditch at y=3520) ---------------------------------------------
fill_cells(0, 0, MW - 1, cy(3521), 'WILD')
fill_cells(0, cy(3520), MW - 1, cy(3520), 'DITCH')

# --- Kharidian Desert (east of the Al Kharid fence) -------------------------------------------
opoly([(3266, 3136), (3266, 3328), (3280, 3336), (3300, 3334), (3344, 3330), (3344, 3136)],
      lambda x, y: setg(x, y, 'DESERT'))

# --- Lumbridge Swamp ---------------------------------------------------------------------
opoly([(3133, 3140), (3133, 3203), (3145, 3216), (3190, 3214), (3197, 3200), (3236, 3194),
       (3247, 3190), (3256, 3190), (3256, 3140)], lambda x, y: setg(x, y, 'SWAMP'))
for (X, Y, w, h) in ((3170, 3180, 8, 5), (3200, 3170, 10, 6), (3155, 3160, 6, 8), (3215, 3185, 6, 4),
                     (3185, 3155, 7, 5), (3236, 3172, 6, 6)):
    oellipse(X - w, Y - h, X + w, Y + h, lambda x, y: setg(x, y, 'WATER'))

# --- Ice Mountain ----------------------------------------------------------------------------
opoly([(2992, 3462), (2998, 3488), (3008, 3502), (3026, 3504), (3040, 3492), (3036, 3468),
       (3018, 3458)], lambda x, y: (TY[y].__setitem__(x, 'CLIFF'), GR[y].__setitem__(x, 'SNOW')))
opoly([(2986, 3456), (2990, 3490), (3006, 3508), (3030, 3510), (3046, 3494), (3044, 3462),
       (3018, 3452)], lambda x, y: setg(x, y, 'SNOW') if TY[y][x] == 'GRASS' else None)

# --- Sea: south and west coasts, Port Sarim inlet ------------------------------------------
opoly([(2928, 3136), (2928, 3193), (2960, 3191), (2994, 3194), (2998, 3204), (2999, 3236),
       (3008, 3239), (3016, 3237), (3024, 3226), (3026, 3204), (3050, 3196), (3062, 3202),
       (3068, 3216), (3078, 3227), (3092, 3229), (3099, 3222), (3102, 3196), (3106, 3184),
       (3130, 3182), (3134, 3170), (3134, 3150), (3140, 3141), (3170, 3138), (3200, 3141),
       (3226, 3139), (3244, 3141), (3252, 3150), (3262, 3149), (3280, 3147), (3300, 3143),
       (3320, 3140), (3344, 3138), (3344, 3136)], lambda x, y: setg(x, y, 'WATER'))
# Wizards' Tower islet
oellipse(3099, 3150, 3119, 3171, lambda x, y: setg(x, y, 'GRASS'))
# beaches
for y in range(MH):
    for x in range(MW):
        if TY[y][x] in ('GRASS',) and any(inb(x + dx, y + dy) and TY[y + dy][x + dx] == 'WATER' and y + dy > cy(3260)
                                          for dx in (-1, 0, 1) for dy in (-1, 0, 1)) and y > cy(3245) and hsh(x, y) % 3:
            setg(x, y, 'SAND')

# --- River Lum -----------------------------------------------------------------------------
river([(3134, 3552), (3132, 3520), (3127, 3500), (3116, 3472), (3108, 3446), (3107, 3424),
       (3112, 3404), (3128, 3386), (3145, 3369), (3162, 3352), (3186, 3336), (3210, 3321),
       (3234, 3306), (3242, 3282), (3243, 3255), (3241, 3226), (3247, 3207), (3253, 3190),
       (3255, 3166), (3254, 3140)], 3)
# Lum lake near the Champions' Guild
oellipse(3164, 3338, 3186, 3352, lambda x, y: setg(x, y, 'WATER'))

# ========================================================================================
# Roads
# ========================================================================================
# Lumbridge: castle gate -> bridge -> Al Kharid toll gate
road([(3231, 3218), (3235, 3224), (3236, 3226), (3248, 3226), (3262, 3228), (3272, 3228)], 2)
# Lumbridge -> Varrock (east bank of the Lum)
road([(3247, 3226), (3249, 3240), (3251, 3270), (3251, 3300), (3247, 3326), (3237, 3350),
      (3226, 3366), (3215, 3376), (3212, 3384)], 2)
# Lumbridge north road past the general store, to Fred's farm and the mill
road([(3224, 3232), (3222, 3240), (3220, 3256), (3205, 3268), (3195, 3282), (3175, 3298), (3166, 3301)], 1)
# Lumbridge -> Draynor
road([(3214, 3236), (3196, 3238), (3170, 3242), (3140, 3246), (3112, 3248), (3080, 3249)], 2)
# Draynor -> Draynor Manor
road([(3104, 3249), (3104, 3280), (3106, 3310), (3108, 3330)], 1)
# Draynor -> Wizards' Tower bridge
road([(3106, 3246), (3108, 3220), (3109, 3200), (3109, 3184), (3109, 3171)], 1)
# Draynor -> Port Sarim -> Rimmington
road([(3080, 3249), (3056, 3246), (3032, 3244), (3010, 3244), (2990, 3240), (2970, 3228), (2956, 3218)], 2)
# Port Sarim -> Falador south gate
road([(3034, 3246), (3026, 3270), (3014, 3296), (3007, 3324)], 2)
# Draynor -> Falador east gate
road([(3078, 3252), (3072, 3280), (3064, 3310), (3064, 3338), (3056, 3340)], 1)
# Falador north gate -> Barbarian Village
road([(2965, 3396), (2970, 3410), (2995, 3423), (3040, 3426), (3070, 3424)], 2)
# Falador north gate -> Doric -> Goblin Village
road([(2964, 3400), (2958, 3430), (2955, 3445), (2956, 3470), (2957, 3494)], 1)
# Barbarian Village -> bridge -> Varrock west gate
road([(3082, 3423), (3100, 3421), (3118, 3421), (3150, 3426), (3176, 3428)], 2)
# Barbarian Village -> Edgeville
road([(3088, 3440), (3092, 3460), (3095, 3486)], 2)
# Edgeville main street -> bridge -> Grand Exchange -> Varrock north-west
road([(3066, 3503), (3100, 3503), (3126, 3507), (3142, 3505), (3160, 3497), (3176, 3487)], 2)
# Varrock east gate -> SE mine -> Al Kharid mine -> Al Kharid
road([(3274, 3428), (3282, 3400), (3286, 3375), (3282, 3345), (3286, 3322), (3292, 3290),
      (3290, 3250), (3284, 3228), (3285, 3205), (3292, 3184)], 2)
# Al Kharid toll gate -> palace
road([(3270, 3228), (3280, 3214), (3290, 3198), (3292, 3184)], 2)
# Varrock south gate -> Champions' Guild -> SW mine
road([(3212, 3386), (3200, 3374), (3192, 3368), (3182, 3368)], 1)
# Edgeville -> Monastery
road([(3066, 3503), (3058, 3494)], 1)

# ========================================================================================
# LUMBRIDGE
# ========================================================================================
# courtyard and outer walls
ofill(3217, 3205, 3229, 3231, 'GRASS')
reserve(*orect(3199, 3201, 3233, 3235))
wall_line(cx(3217), cy(3233), cx(3229), cy(3233))           # north curtain
wall_line(cx(3217), cy(3203), cx(3229), cy(3203))           # south curtain
wall_line(cx(3229), cy(3233), cx(3229), cy(3203))           # east curtain
for (X0, Y0) in ((3228, 3221), (3228, 3211)):               # gate towers
    fill_cells(cx(X0), cy(Y0 + 3), cx(X0 + 3), cy(Y0), 'GRASS')
    for y in range(cy(Y0 + 3), cy(Y0) + 1):
        for x in range(cx(X0), cx(X0 + 3) + 1):
            TY[y][x] = 'WALL'
            GR[y][x] = 'FLOOR'
for y in range(cy(3219), cy(3216) + 1):                     # gate opening
    for x in range(cx(3227), cx(3233) + 1):
        setg(x, y, 'COBBLE')
road([(3217, 3218), (3233, 3218)], 2, 'COBBLE')
# corner towers of the keep
for (X0, Y0) in ((3199, 3228), (3199, 3203)):
    fill_cells(cx(X0), cy(Y0 + 3), cx(X0 + 3), cy(Y0), 'GRASS')
# keep
KEEP = building(3201, 3205, 3217, 3231, 'WALL', 'FLOOR', [('E', 3218)])
kx0, ky0, kx1, ky1 = KEEP
for (X0, Y0) in ((3199, 3228), (3199, 3203)):
    for y in range(cy(Y0 + 3), cy(Y0) + 1):
        for x in range(cx(X0), cx(X0 + 3) + 1):
            TY[y][x] = 'WALL'
            GR[y][x] = 'FLOOR'
# kitchen (south-west room) with the range and the Cook
wall_line(kx0, cy(3214), kx0 + 5, cy(3214), doors=[(kx0 + 3, cy(3214))])
wall_line(kx0 + 5, cy(3214), kx0 + 5, ky1, doors=[(kx0 + 5, ky1 - 2)])
put(kx0 + 1, ky1 - 1, 'RANGE')
put(kx0 + 4, ky1 - 1, 'RANGE')
LUMB_COOK = (kx0 + 2, ky1 - 2)
SPAWNS.append(('COOK', *LUMB_COOK))
# bank (north side of the keep)
for x in range(kx0 + 2, kx0 + 6):
    put(x, ky0 + 2, 'BOOTH')
SPAWNS.append(('BANKER', kx0 + 2, ky0 + 1))
SPAWNS.append(('BANKER', kx0 + 4, ky0 + 1))
# throne room carpet (Duke Horacio stands here)
for y in range(ky0 + 4, ky0 + 8):
    for x in range(kx1 - 3, kx1):
        setg(x, y, 'CARPET')
DUKE = (kx1 - 2, ky0 + 5)
HANS = (kx1 + 2, cy(3222))
SPAWNS.append(('HANS', *HANS))
SPAWNS.append(('GUARD', kx1 - 1, ky0 + 9))
SPAWNS.append(('GUARD', kx0 + 7, ky1 - 2))
START = (cx(3222), cy(3218))

# general store
GS_LUMB = building(3209, 3242, 3219, 3252, 'HWALL', 'WOODFLOOR', [('S', 3214)])
service(GS_LUMB, 'N', 'COUNTER', 'SHOPKEEPER')
# Sheared Ram pub, Bob's axes, other houses
building(3226, 3238, 3235, 3246, 'HWALL', 'WOODFLOOR', [('W', 3241)])
BOB = building(3228, 3195, 3236, 3201, 'HWALL', 'WOODFLOOR', [('N', 3232)])
building(3200, 3238, 3207, 3245, 'HWALL', 'WOODFLOOR', [('E', 3241)])
building(3232, 3250, 3239, 3257, 'HWALL', 'WOODFLOOR', [('W', 3253)])
# church with altar + graveyard
CHURCH_L = building(3236, 3203, 3249, 3216, 'WALL', 'FLOOR', [('W', 3210)])
put(CHURCH_L[2] - 1, cy(3210), 'ALTAR')
AERECK = (CHURCH_L[2] - 3, cy(3209))
fence_rect(3236, 3190, 3250, 3201, gates=[('N', 3241), ('W', 3192)])   # the N gate faces the church wall
for (X, Y) in ((3239, 3197), (3243, 3197), (3247, 3197), (3239, 3193), (3243, 3193), (3247, 3193)):
    oput(X, Y, 'GRAVE')
for y in range(cy(3201), cy(3190) + 1):
    for x in range(cx(3236), cx(3250) + 1):
        if TY[y][x] == 'SWAMP':
            setg(x, y, 'GRASS')
road([(3232, 3210), (3236, 3210)], 1)
# Lozar's house and the goblin plain east of the river
building(3250, 3240, 3256, 3246, 'HWALL', 'WOODFLOOR', [('S', 3253)])
building(3256, 3204, 3262, 3210, 'HWALL', 'WOODFLOOR', [('N', 3259)])
# Al Kharid border fence and toll gate
fence_line([(3266, 3184), (3266, 3330)])
fence_line([(3256, 3184), (3266, 3184)])
fence_line([(3266, 3330), (3282, 3338)])
TOLLGATE = [(cx(3266), y) for y in range(cy(3229), cy(3227) + 1)]   # shut until the toll is paid
# east cow field
fence_rect(3254, 3255, 3265, 3298, gates=[('W', 3270), ('W', 3290)])
# north chicken farm with coop
fence_rect(3220, 3286, 3234, 3300, gates=[('W', 3292)])
building(3229, 3294, 3233, 3299, 'HWALL', 'WOODFLOOR', [('W', 3296)])
# Fred's farm, chicken pen, sheep field, crops
building(3184, 3270, 3192, 3279, 'HWALL', 'WOODFLOOR', [('S', 3188)])
fence_rect(3168, 3286, 3186, 3302, gates=[('E', 3294)])
building(3172, 3296, 3177, 3301, 'HWALL', 'WOODFLOOR', [('S', 3174)])
fence_rect(3194, 3256, 3214, 3268, gates=[('N', 3204)])
# Mill Lane Mill + wheat field
building(3162, 3302, 3170, 3310, 'WALL', 'WOODFLOOR', [('E', 3306)])
ofill(3152, 3286, 3164, 3298, 'PATH')
for y in range(cy(3297), cy(3287) + 1):
    for x in range(cx(3153), cx(3163) + 1):
        if y % 2 == 0 or True:
            put(x, y, 'CROPS')
fence_rect(3151, 3285, 3165, 3299, gates=[('N', 3158)])
# farm crops near Fred
ofill(3196, 3285, 3210, 3296, 'PATH')
for y in range(cy(3295), cy(3286) + 1):
    for x in range(cx(3197), cx(3209) + 1):
        put(x, y, 'CROPS')
fence_rect(3195, 3284, 3211, 3297, gates=[('S', 3203)])

# Lumbridge swamp mines and features
mine(3222, 3142, 3234, 3152, [('COPPER', 4), ('TIN', 4)], 'SWAMP', 1)
mine(3142, 3144, 3152, 3154, [('COPPER', 3), ('TIN', 3), ('IRON', 1)], 'SWAMP', 1)
building(3144, 3180, 3151, 3186, 'HWALL', 'WOODFLOOR', [('E', 3183)])       # Father Urhney
building(3180, 3196, 3186, 3202, 'HWALL', 'WOODFLOOR', [('N', 3183)])       # adventurers' camp hut

# ========================================================================================
# DRAYNOR VILLAGE
# ========================================================================================
BANK_DRAY = building(3088, 3238, 3099, 3247, 'WALL', 'FLOOR', [('N', 3093)])
service(BANK_DRAY, 'S', 'BOOTH', 'BANKER')
building(3086, 3251, 3097, 3258, 'WALL', 'FLOOR', [('S', 3090)])             # Wise Old Man
# market square with stalls
ofill(3070, 3243, 3084, 3257, 'COBBLE')
for (X, Y) in ((3074, 3254), (3080, 3254), (3074, 3246), (3080, 3246)):
    oput(X, Y, 'STALL')
fence_rect(3070, 3260, 3078, 3266, gates=[('S', 3074)])                     # pig pen
building(3120, 3240, 3131, 3251, 'WALL', 'FLOOR', [('W', 3245)])             # jail
building(3078, 3260, 3085, 3267, 'HWALL', 'WOODFLOOR', [('S', 3082)])       # Aggie
building(3097, 3256, 3103, 3262, 'HWALL', 'WOODFLOOR', [('W', 3259)])       # Ned
building(3094, 3266, 3101, 3272, 'HWALL', 'WOODFLOOR', [('S', 3097)])       # Morgan
building(3086, 3268, 3092, 3274, 'HWALL', 'WOODFLOOR', [('E', 3271)])       # Spria
# willows on the shore south-west of the bank
for (X, Y) in ((3083, 3236), (3087, 3233), (3091, 3236), (3085, 3240), (3080, 3232)):
    if TY[cy(Y)][cx(X)] in ('GRASS', 'SAND'):
        g = GR[cy(Y)][cx(X)]
        put(cx(X), cy(Y), 'WILLOW')
# Draynor Manor
fence_rect(3086, 3330, 3130, 3380, gates=[('S', 3108)])
MANOR = building(3096, 3352, 3124, 3372, 'WALL', 'WOODFLOOR', [('S', 3110)])
wall_line(MANOR[0] + 5, MANOR[1], MANOR[0] + 5, MANOR[3] - 3, doors=[(MANOR[0] + 5, MANOR[1] + 3)])
wall_line(MANOR[2] - 5, MANOR[1], MANOR[2] - 5, MANOR[3] - 3, doors=[(MANOR[2] - 5, MANOR[1] + 3)])
road([(3108, 3330), (3110, 3350)], 1)
scatter(3087, 3331, 3129, 3379, 0.12, [('DEADTREE', 5), ('BUSH', 1)], seed=77)
# Wizards' Tower (round tower on the islet)
wcx, wcy = fpt(3109, 3161)
for y in range(MH):
    for x in range(MW):
        d = math.hypot(x + 0.5 - wcx, y + 0.5 - wcy)
        if d <= 5.2:
            TY[y][x] = 'WALL' if d > 4.2 else 'FLOOR'
            GR[y][x] = 'FLOOR'
reserve(int(wcx) - 7, int(wcy) - 7, int(wcx) + 7, int(wcy) + 7)
TY[int(wcy) - 5][int(wcx)] = 'DOOR'
setg(int(wcx), int(wcy) - 6, 'PATH')
road([(3109, 3171), (3109, 3166)], 1)

# ========================================================================================
# PORT SARIM and RIMMINGTON
# ========================================================================================
for (Y,) in ((3217,), (3230,)):
    for x in range(cx(3012), cx(3027) + 1):
        if TY[cy(Y)][x] == 'WATER':
            setg(x, cy(Y), 'BRIDGE')
for y in range(cy(3200), cy(3186) + 1):
    for x in range(cx(3045), cx(3048) + 1):
        if TY[y][x] == 'WATER':
            setg(x, y, 'BRIDGE')
ofill(3027, 3200, 3031, 3240, 'WOODFLOOR')                                  # quay
GS_SARIM = building(3036, 3250, 3044, 3258, 'HWALL', 'WOODFLOOR', [('S', 3040)])   # food store
building(3046, 3250, 3054, 3258, 'HWALL', 'WOODFLOOR', [('S', 3050)])       # Betty's
building(3040, 3232, 3050, 3240, 'HWALL', 'WOODFLOOR', [('N', 3044)])       # Rusty Anchor
building(3034, 3206, 3042, 3214, 'HWALL', 'WOODFLOOR', [('W', 3210)])       # Gerrant's
building(3046, 3214, 3054, 3222, 'WALL', 'FLOOR', [('N', 3050)])            # jail
# Rimmington
building(2950, 3204, 2958, 3211, 'HWALL', 'WOODFLOOR', [('N', 2954)])
building(2962, 3208, 2970, 3215, 'HWALL', 'WOODFLOOR', [('W', 3211)])
building(2946, 3216, 2954, 3223, 'HWALL', 'WOODFLOOR', [('E', 3220)])
building(2958, 3226, 2965, 3233, 'HWALL', 'WOODFLOOR', [('S', 2961)])
building(2935, 3205, 2942, 3212, 'HWALL', 'WOODFLOOR', [('E', 3208)])
mine(2968, 3232, 2990, 3250, [('COPPER', 4), ('TIN', 4), ('IRON', 4), ('CLAY', 3)], 'PATH', 2)
# Crafting Guild (west)
building(2928, 3278, 2938, 3292, 'WALL', 'FLOOR', [('E', 3285)])
# fields between Falador and Port Sarim (cows!)
fence_rect(3020, 3296, 3046, 3314, gates=[('W', 3305), ('E', 3300)])
ofill(3040, 3272, 3056, 3288, 'PATH')
for y in range(cy(3287), cy(3273) + 1):
    for x in range(cx(3041), cx(3055) + 1):
        put(x, y, 'CROPS')
fence_rect(3039, 3271, 3057, 3289, gates=[('W', 3280)])
building(3060, 3280, 3066, 3287, 'HWALL', 'WOODFLOOR', [('N', 3063)])

# ========================================================================================
# FALADOR
# ========================================================================================
FALADOR_POLY = [(2966, 3395), (2943, 3393), (2936, 3388), (2935, 3320), (2939, 3313), (2967, 3309),
                (2985, 3309), (2992, 3316), (2995, 3316), (3002, 3323), (3012, 3323), (3016, 3327),
                (3025, 3329), (3060, 3329), (3060, 3363), (3066, 3369), (3066, 3385), (3061, 3390),
                (3021, 3390), (3020, 3392), (3007, 3395), (2984, 3395), (2969, 3395)]
opoly(FALADOR_POLY, lambda x, y: (setg(x, y, 'GRASS'), RES[y].__setitem__(x, True)))
# streets
road([(2965, 3394), (2965, 3372), (2990, 3362), (3012, 3362), (3030, 3350), (3040, 3340), (3061, 3338)], 2, 'COBBLE')
road([(3007, 3324), (3007, 3346), (2995, 3362)], 2, 'COBBLE')
road([(2965, 3372), (2942, 3367)], 2, 'COBBLE')
road([(2965, 3372), (2965, 3350), (2972, 3354)], 1, 'COBBLE')
road([(2990, 3362), (2990, 3390)], 1, 'COBBLE')
road([(3030, 3350), (3030, 3388)], 1, 'COBBLE')
road([(2945, 3367), (2945, 3330), (2955, 3318), (2985, 3316), (3000, 3326)], 1, 'COBBLE')
# white walls along the outline, gates where streets leave
opolyline(FALADOR_POLY + [FALADOR_POLY[0]], 1,
          lambda x, y: None if TY[y][x] == 'COBBLE' else (TY[y].__setitem__(x, 'WWALL'), GR[y].__setitem__(x, 'FLOOR')))
# park with fountain, trees and flowers
ofill(2988, 3366, 3024, 3389, 'GRASS')
for y in range(cy(3390), cy(3365) + 1):
    for x in range(cx(2987), cx(3025) + 1):
        ROAD[y][x] = True
scatter(2988, 3366, 3024, 3389, 0.12, [('FLOWERS', 4), ('BUSH', 1)], seed=31)
for (X, Y) in ((2992, 3386), (3000, 3384), (3018, 3386), (3022, 3378), (2994, 3370), (3014, 3369)):
    oput(X, Y, 'TREE')
for (X, Y) in ((3006, 3388), (3020, 3370)):
    oput(X, Y, 'OAK')
fill_cells(cx(3004), cy(3379), cx(3007), cy(3376), 'GRASS')
for y in range(cy(3379), cy(3376) + 1):
    for x in range(cx(3004), cx(3007) + 1):
        put(x, y, 'FOUNTAIN')
# White Knights' Castle
WKC = building(2954, 3326, 2988, 3352, 'WALL', 'FLOOR', [('N', 2971), ('E', 3340)])
wall_line(WKC[0] + 6, WKC[1], WKC[0] + 6, WKC[3], doors=[(WKC[0] + 6, WKC[1] + 6)])
wall_line(WKC[2] - 6, WKC[1], WKC[2] - 6, WKC[3], doors=[(WKC[2] - 6, WKC[1] + 6)])
for y in range(WKC[1] + 2, WKC[3] - 1):
    for x in range(WKC[0] + 8, WKC[2] - 7):
        if x in (WKC[0] + 11, WKC[0] + 12):
            setg(x, y, 'CARPET')
# banks
BANK_FALE = building(3008, 3351, 3020, 3360, 'WALL', 'FLOOR', [('N', 3014)])
service(BANK_FALE, 'S', 'BOOTH', 'BANKER')
BANK_FALW = building(2941, 3362, 2951, 3375, 'WALL', 'FLOOR', [('E', 3367)])
service(BANK_FALW, 'W', 'BOOTH', 'BANKER')
# shops and houses
GS_FAL = building(2952, 3382, 2962, 3391, 'HWALL', 'WOODFLOOR', [('E', 3386)])
service(GS_FAL, 'N', 'COUNTER', 'SHOPKEEPER')
building(2970, 3380, 2980, 3390, 'HWALL', 'WOODFLOOR', [('W', 3385)])       # Cassie's shields
building(2943, 3350, 2951, 3358, 'HWALL', 'WOODFLOOR', [('E', 3354)])       # Herquin's gems
building(2970, 3360, 2980, 3368, 'WALL', 'FLOOR', [('S', 2975)])            # furnace
building(2954, 3373, 2963, 3380, 'HWALL', 'WOODFLOOR', [('S', 2958)])       # Rising Sun Inn
PARTY = building(3036, 3368, 3056, 3386, 'WALL', 'WOODFLOOR', [('S', 3046)])
building(3016, 3333, 3024, 3343, 'WALL', 'FLOOR', [('W', 3338)])            # Mining Guild
building(3032, 3330, 3042, 3340, 'HWALL', 'WOODFLOOR', [('N', 3037)])
building(2992, 3328, 3000, 3338, 'HWALL', 'WOODFLOOR', [('E', 3333)])
building(3042, 3348, 3052, 3358, 'HWALL', 'WOODFLOOR', [('W', 3353)])
building(2968, 3312, 2978, 3320, 'HWALL', 'WOODFLOOR', [('N', 2973)])       # Wayne's chains
infill(2936, 3310, 3065, 3394, 14, 'WWALL', 'FLOOR', seed=34)
scatter(2936, 3310, 3065, 3394, 0.03, [('TREE', 2), ('FLOWERS', 3), ('BUSH', 1)], seed=33)

# ========================================================================================
# NORTH OF FALADOR: Doric, Goblin Village, Ice Mountain, Black Knights, Monastery
# ========================================================================================
DORIC = building(2946, 3446, 2955, 3454, 'HWALL', 'WOODFLOOR', [('E', 3450)])
DORIC_POS = (DORIC[0] + 2, DORIC[1] + 2)
for (X, Y) in ((2950, 3504), (2958, 3500), (2963, 3508), (2950, 3493)):
    building(X - 3, Y - 3, X + 3, Y + 3, 'HWALL', 'WOODFLOOR', [('S', X)])
ofill(2952, 3496, 2962, 3504, 'PATH')
building(3028, 3448, 3036, 3455, 'WALL', 'FLOOR', [('S', 3032)])            # Dwarven Mine entrance
road([(3010, 3426), (3020, 3440), (3032, 3447)], 1)
BKF = building(3008, 3504, 3032, 3518, 'WALL', 'FLOOR', [('W', 3510)])      # Black Knights' Fortress (the south side faces the cliffs)
wall_line(BKF[0] + 4, BKF[1], BKF[0] + 4, BKF[3], doors=[(BKF[0] + 4, BKF[1] + 3)])
wall_line(BKF[2] - 4, BKF[1], BKF[2] - 4, BKF[3], doors=[(BKF[2] - 4, BKF[1] + 3)])
MONASTERY = building(3042, 3480, 3060, 3500, 'WALL', 'FLOOR', [('E', 3492)])
put(MONASTERY[0] + 2, MONASTERY[1] + 4, 'ALTAR')

# ========================================================================================
# BARBARIAN VILLAGE
# ========================================================================================
LONGHALL = building(3072, 3434, 3086, 3448, 'HWALL', 'WOODFLOOR', [('S', 3079)])
put(LONGHALL[0] + 3, LONGHALL[1] + 3, 'FIREPIT')
put(LONGHALL[2] - 3, LONGHALL[1] + 3, 'FIREPIT')
building(3068, 3426, 3075, 3432, 'HWALL', 'WOODFLOOR', [('E', 3429)])       # Peksa's helmets
building(3082, 3426, 3088, 3432, 'HWALL', 'WOODFLOOR', [('W', 3429)])       # spinning hut
mine(3074, 3412, 3088, 3424, [('TIN', 5), ('COPPER', 3)], 'PATH', 1)
oput(3098, 3428, 'FIREPIT')                                                 # never-ending fire
fence_line([(3064, 3410), (3064, 3452), (3096, 3452)])
building(3138, 3438, 3148, 3448, 'WALL', 'FLOOR', [('W', 3443)])             # Cooking Guild
put(cx(3146), cy(3445), 'RANGE')
# lookout tower on the Varrock road
building(3132, 3414, 3136, 3418, 'WALL', 'FLOOR', [('N', 3134)])

# ========================================================================================
# EDGEVILLE
# ========================================================================================
BANK_EDGE = building(3090, 3487, 3100, 3500, 'WALL', 'FLOOR', [('S', 3094)])
service(BANK_EDGE, 'N', 'BOOTH', 'BANKER')
GS_EDGE = building(3076, 3505, 3085, 3514, 'HWALL', 'WOODFLOOR', [('S', 3080)])
service(GS_EDGE, 'N', 'COUNTER', 'SHOPKEEPER')
DAVE = building(3077, 3489, 3086, 3497, 'HWALL', 'WOODFLOOR', [('E', 3493)])  # Evil Dave's house
put(DAVE[0] + 1, DAVE[1] + 1, 'RANGE')
building(3104, 3495, 3112, 3501, 'WALL', 'FLOOR', [('W', 3498)])             # furnace
building(3104, 3510, 3114, 3518, 'WALL', 'FLOOR', [('S', 3109)])             # jail
# dilapidated mansion with yews south of the bank
for (x0, y0, x1, y1) in (orect(3080, 3466, 3094, 3482),):
    for x in range(x0, x1 + 1):
        for y in (y0, y1):
            if (x + y) % 4:
                TY[y][x] = 'WALL'
                GR[y][x] = 'GRASS'
    for y in range(y0, y1 + 1):
        for x in (x0, x1):
            if (x + y) % 3:
                TY[y][x] = 'WALL'
                GR[y][x] = 'GRASS'
    reserve(x0, y0, x1, y1)
    TY[y1][(x0 + x1) // 2] = 'GRASS'
for (X, Y) in ((3086, 3478), (3089, 3471), (3070, 3477), (3104, 3474)):
    oput(X, Y, 'YEW')
oput(3104, 3490, 'WILLOW')
oput(3108, 3486, 'WILLOW')

# ========================================================================================
# VARROCK
# ========================================================================================
VX0, VY0, VX1, VY1 = 3174, 3382, 3276, 3506
ofill(VX0, VY0, VX1, VY1, 'GRASS')
reserve(*orect(VX0, VY0, VX1, VY1))
# streets
road([(3176, 3428), (3274, 3428)], 2, 'COBBLE')                             # main east-west
road([(3212, 3383), (3212, 3458)], 2, 'COBBLE')                             # south gate -> palace
road([(3186, 3400), (3262, 3400)], 1, 'COBBLE')
road([(3186, 3400), (3186, 3490), (3198, 3500)], 1, 'COBBLE')
road([(3248, 3400), (3248, 3500)], 1, 'COBBLE')
road([(3212, 3456), (3246, 3456)], 1, 'COBBLE')
road([(3246, 3500), (3246, 3507)], 2, 'COBBLE')                             # north gate
ofill(3202, 3418, 3224, 3438, 'COBBLE')                                     # square
keep_open = lambda X0, Y0, X1, Y1: [ROAD[y].__setitem__(x, True) for y in range(orect(X0, Y0, X1, Y1)[1], orect(X0, Y0, X1, Y1)[3] + 1)
                                    for x in range(orect(X0, Y0, X1, Y1)[0], orect(X0, Y0, X1, Y1)[2] + 1)]
keep_open(3200, 3416, 3226, 3440)
for y in range(cy(3430), cy(3427) + 1):
    for x in range(cx(3210), cx(3213) + 1):
        put(x, y, 'FOUNTAIN')
# city walls
for (a, b) in (((VX0, VY0), (VX1, VY0)), ((VX1, VY0), (VX1, VY1)), ((VX1, VY1), (VX0, VY1)), ((VX0, VY1), (VX0, VY0))):
    opolyline([a, b], 1, lambda x, y: None if TY[y][x] == 'COBBLE' else (TY[y].__setitem__(x, 'WALL'), GR[y].__setitem__(x, 'FLOOR')))
# palace grounds and palace
PAL_GROUNDS = orect(3196, 3458, 3230, 3504)
px0, py0, px1, py1 = PAL_GROUNDS
for x in range(px0, px1 + 1):
    for y in (py0, py1):
        TY[y][x] = 'WALL'
        GR[y][x] = 'FLOOR'
for y in range(py0, py1 + 1):
    for x in (px0, px1):
        TY[y][x] = 'WALL'
        GR[y][x] = 'FLOOR'
for x in range(cx(3211), cx(3213) + 1):
    setg(x, py1, 'COBBLE')
PALACE = building(3201, 3468, 3225, 3497, 'WALL', 'FLOOR', [('S', 3212)])
road([(3212, 3458), (3212, 3468)], 2, 'COBBLE')
for y in range(PALACE[1] + 2, PALACE[3]):
    for x in range(cx(3211), cx(3213) + 1):
        setg(x, y, 'CARPET')
wall_line(PALACE[0] + 4, PALACE[1], PALACE[0] + 4, PALACE[3], doors=[(PALACE[0] + 4, PALACE[3] - 3)])
wall_line(PALACE[2] - 4, PALACE[1], PALACE[2] - 4, PALACE[3], doors=[(PALACE[2] - 4, PALACE[3] - 3)])
KING = (cx(3212), PALACE[1] + 2)
scatter(3197, 3459, 3229, 3503, 0.10, [('TREE', 2), ('FLOWERS', 5), ('BUSH', 2)], seed=61)
# banks
BANK_VW = building(3180, 3432, 3193, 3448, 'WALL', 'FLOOR', [('E', 3440)])
service(BANK_VW, 'W', 'BOOTH', 'BANKER')
BANK_VE = building(3249, 3416, 3259, 3425, 'WALL', 'FLOOR', [('N', 3253)])
service(BANK_VE, 'S', 'BOOTH', 'BANKER')
# shops
GS_VAR = building(3214, 3406, 3224, 3416, 'HWALL', 'WOODFLOOR', [('N', 3218)])
service(GS_VAR, 'S', 'COUNTER', 'SHOPKEEPER')
SWORD = building(3198, 3392, 3207, 3398, 'HWALL', 'WOODFLOOR', [('N', 3202)])
AUBURY = building(3250, 3402, 3258, 3408, 'HWALL', 'WOODFLOOR', [('W', 3405)])
HORVIK = building(3226, 3434, 3234, 3442, 'HWALL', 'WOODFLOOR', [('S', 3230)])
ZAFF = building(3198, 3432, 3206, 3440, 'HWALL', 'WOODFLOOR', [('S', 3202)])
THESSALIA = building(3200, 3406, 3208, 3415, 'HWALL', 'WOODFLOOR', [('N', 3204)])
LOWE = building(3228, 3418, 3236, 3425, 'HWALL', 'WOODFLOOR', [('N', 3232)])
APOTH = building(3190, 3402, 3197, 3410, 'HWALL', 'WOODFLOOR', [('S', 3193)])
building(3218, 3388, 3234, 3398, 'HWALL', 'WOODFLOOR', [('N', 3226)])       # Blue Moon Inn
CHURCH_V = building(3250, 3470, 3262, 3492, 'WALL', 'FLOOR', [('W', 3476)])
put(cx(3256), CHURCH_V[1] + 1, 'ALTAR')
LAWRENCE = (cx(3256), CHURCH_V[1] + 3)
building(3252, 3440, 3268, 3454, 'WALL', 'FLOOR', [('W', 3446)])             # museum
building(3254, 3384, 3264, 3394, 'WALL', 'FLOOR', [('N', 3259)])             # Zamorak chapel
building(3234, 3460, 3244, 3470, 'WALL', 'FLOOR', [('S', 3238)])             # guards' barracks
building(3236, 3404, 3244, 3412, 'HWALL', 'WOODFLOOR', [('S', 3240)])
building(3236, 3438, 3244, 3448, 'HWALL', 'WOODFLOOR', [('S', 3240)])
building(3178, 3460, 3184, 3470, 'HWALL', 'WOODFLOOR', [('E', 3465)])
building(3190, 3410, 3198, 3420, 'HWALL', 'WOODFLOOR', [('N', 3194)])
building(3260, 3404, 3270, 3414, 'HWALL', 'WOODFLOOR', [('S', 3265)])
building(3262, 3462, 3272, 3470, 'HWALL', 'WOODFLOOR', [('S', 3267)])
building(3178, 3384, 3188, 3394, 'HWALL', 'WOODFLOOR', [('N', 3183)])
# paved city blocks south of the palace, with a few grass plots left
for y in range(cy(3456), cy(3384) + 1):
    for x in range(cx(3176), cx(3274) + 1):
        if TY[y][x] == 'GRASS' and vnoise(x / 3.0, y / 3.0, 64) > 0.3:
            setg(x, y, 'COBBLE')
infill(3176, 3384, 3274, 3456, 14, 'HWALL', 'WOODFLOOR', seed=65)
infill(3176, 3384, 3274, 3456, 6, 'WALL', 'FLOOR', seed=66)
infill(3232, 3456, 3274, 3504, 4, 'WALL', 'FLOOR', seed=67)
infill(3176, 3456, 3196, 3504, 3, 'HWALL', 'WOODFLOOR', seed=68)
scatter(VX0, VY0, VX1, VY1, 0.03, [('TREE', 2), ('FLOWERS', 3), ('BUSH', 1)], seed=63)
# outside the walls: Juliet's house, Grand Exchange, Champions' Guild, mines, stone circle
JULIET_H = building(3154, 3424, 3165, 3437, 'HWALL', 'WOODFLOOR', [('E', 3430)])
JULIET = (JULIET_H[0] + 2, JULIET_H[1] + 2)
gcx, gcy = fpt(3165, 3490)
for y in range(MH):
    for x in range(MW):
        d = max(abs(x + 0.5 - gcx), abs(y + 0.5 - gcy)) * 0.8 + 0.2 * (abs(x + 0.5 - gcx) + abs(y + 0.5 - gcy))
        if d <= 5.5:
            TY[y][x] = 'WALL' if d > 4.6 else 'FLOOR'
            GR[y][x] = 'FLOOR'
reserve(int(gcx) - 7, int(gcy) - 7, int(gcx) + 7, int(gcy) + 7)
for (dx, dy) in ((0, -5), (0, 5), (-5, 0), (5, 0)):
    TY[int(gcy) + dy][int(gcx) + dx] = 'DOOR'
    setg(int(gcx) + dx + (1 if dx > 0 else -1 if dx < 0 else 0), int(gcy) + dy + (1 if dy > 0 else -1 if dy < 0 else 0), 'COBBLE')
GE_BOOTHS = []
for (dx, dy) in ((-1, -1), (0, -1), (1, -1), (-1, 1), (0, 1), (1, 1)):
    put(int(gcx) + dx, int(gcy) + dy, 'BOOTH')
for (dx, dy) in ((-2, -2), (2, 2)):
    SPAWNS.append(('BANKER', int(gcx) + dx, int(gcy) + dy))
CHAMPIONS = building(3186, 3356, 3198, 3366, 'WALL', 'FLOOR', [('N', 3192)])
mine(3172, 3362, 3186, 3376, [('TIN', 4), ('IRON', 3), ('CLAY', 3)], 'PATH', 2)
mine(3278, 3358, 3294, 3374, [('COPPER', 4), ('TIN', 4), ('IRON', 3)], 'PATH', 2)
ccx, ccy = fpt(3225, 3370)
for k in range(8):
    a = k * math.pi / 4
    put(int(ccx + 3.2 * math.cos(a)), int(ccy + 3.2 * math.sin(a)), 'BOULDER')
reserve(int(ccx) - 4, int(ccy) - 4, int(ccx) + 4, int(ccy) + 4)

# ========================================================================================
# AL KHARID and the desert
# ========================================================================================
road([(3292, 3184), (3292, 3150)], 2, 'PATH')
PALACE_AK = building(3280, 3156, 3305, 3180, 'SWALL', 'FLOOR', [('N', 3292)])
for y in range(PALACE_AK[1] + 1, PALACE_AK[3]):
    for x in range(cx(3291), cx(3293) + 1):
        setg(x, y, 'CARPET')
wall_line(PALACE_AK[0] + 4, PALACE_AK[1], PALACE_AK[0] + 4, PALACE_AK[3], 'SWALL', doors=[(PALACE_AK[0] + 4, PALACE_AK[1] + 4)])
wall_line(PALACE_AK[2] - 4, PALACE_AK[1], PALACE_AK[2] - 4, PALACE_AK[3], 'SWALL', doors=[(PALACE_AK[2] - 4, PALACE_AK[1] + 4)])
EMIR = (cx(3292), PALACE_AK[3] - 2)
BANK_AK = building(3267, 3159, 3276, 3174, 'SWALL', 'FLOOR', [('E', 3166)])
service(BANK_AK, 'W', 'BOOTH', 'BANKER')
KARIM = building(3270, 3177, 3278, 3184, 'SWALL', 'FLOOR', [('E', 3180)])
put(KARIM[0] + 1, KARIM[1] + 1, 'RANGE')
building(3274, 3186, 3281, 3193, 'SWALL', 'FLOOR', [('E', 3189)])           # furnace
GS_AK = building(3310, 3174, 3320, 3184, 'SWALL', 'FLOOR', [('W', 3178)])
service(GS_AK, 'E', 'COUNTER', 'SHOPKEEPER')
building(3284, 3186, 3291, 3193, 'SWALL', 'FLOOR', [('S', 3287)])           # Zeke's scimitars
building(3310, 3162, 3318, 3170, 'SWALL', 'FLOOR', [('W', 3166)])           # Louie's legs
building(3282, 3206, 3290, 3213, 'SWALL', 'FLOOR', [('S', 3286)])           # gem trader
building(3294, 3190, 3302, 3197, 'SWALL', 'FLOOR', [('W', 3193)])           # Ellis the tanner
building(3300, 3200, 3308, 3207, 'SWALL', 'FLOOR', [('W', 3203)])           # Ali Morrisane
road([(3292, 3181), (3320, 3186), (3320, 3200)], 1, 'PATH')
road([(3268, 3176), (3280, 3176)], 1, 'PATH')
road([(3276, 3166), (3280, 3166)], 1, 'PATH')
# Al Kharid mine
mine(3292, 3272, 3308, 3320, [('COPPER', 6), ('TIN', 6), ('IRON', 7)], 'DESERT', 4)
opoly([(3309, 3324), (3316, 3326), (3320, 3300), (3318, 3270), (3310, 3268), (3312, 3296)],
      lambda x, y: put(x, y, 'CLIFF') if TY[y][x] == 'DESERT' else None)
opoly([(3286, 3326), (3292, 3326), (3290, 3300), (3288, 3290), (3284, 3300)],
      lambda x, y: put(x, y, 'CLIFF') if TY[y][x] == 'DESERT' and not RES[y][x] else None)
# Duel Arena (partly beyond the eastern map edge)
ARENA = building(3314, 3206, 3343, 3262, 'SWALL', 'DESERT', [('W', 3232)], margin=0)
for (X0, Y0, X1, Y1) in ((3320, 3212, 3334, 3226), (3320, 3238, 3334, 3252)):
    building(X0, Y0, X1, Y1, 'SWALL', 'SAND', [('W', (Y0 + Y1) // 2)])
for y in range(MH):                              # desert sand inside
    for x in range(ARENA[0] + 1, ARENA[2]):
        if ARENA[1] < y < ARENA[3] and TY[y][x] == 'GRASS':
            setg(x, y, 'DESERT')
scatter(3266, 3136, 3344, 3330, 0.035, [('CACTUS', 4), ('BOULDER', 1), ('DEADTREE', 1)], on=('DESERT',), seed=71)

# ========================================================================================
# Countryside: forests, scattered trees, Lumbridge woods
# ========================================================================================
TK = [('TREE', 8), ('OAK', 2)]
forest(3134, 3216, 3200, 3262, 0.75, TK + [('DEADTREE', 1)], seed=81)       # Lumbridge west woods
forest(3196, 3200, 3216, 3240, 0.35, TK, seed=82)
forest(3110, 3256, 3150, 3330, 0.70, TK + [('BUSH', 1)], seed=83)           # Draynor woods
forest(3060, 3180, 3100, 3230, 0.30, [('TREE', 4), ('WILLOW', 1)], seed=84)
forest(2928, 3240, 3000, 3310, 0.55, TK + [('BUSH', 1)], seed=85)           # south of Falador
forest(3000, 3250, 3090, 3330, 0.25, TK + [('BUSH', 1)], seed=86)
forest(3128, 3260, 3250, 3460, 0.30, TK + [('BUSH', 1)], seed=87)           # Lumbridge-Varrock country
forest(3066, 3380, 3175, 3415, 0.40, TK + [('BUSH', 1)], seed=88)
forest(3125, 3440, 3175, 3520, 0.50, TK + [('BUSH', 1)], seed=89)           # west of Varrock
forest(3276, 3330, 3344, 3520, 0.70, TK + [('BUSH', 1)], seed=90)           # Varrock east woods
forest(2928, 3396, 3065, 3518, 0.40, TK + [('BUSH', 1), ('BOULDER', 1)], seed=41)   # north of Falador
forest(3060, 3455, 3125, 3519, 0.40, TK + [('BUSH', 1)], seed=51)          # Edgeville woods
forest(3174, 3505, 3344, 3519, 0.40, TK, seed=95)
forest(3256, 3230, 3265, 3254, 0.10, [('TREE', 1)], seed=91)
forest(3133, 3140, 3256, 3216, 0.35, [('DEADTREE', 3), ('TREE', 1), ('BUSH', 1)], on=('SWAMP',), seed=92)
forest(2928, 3521, 3344, 3552, 0.30, [('DEADTREE', 4), ('BOULDER', 2)], on=('WILD',), seed=93)
scatter(2928, 3136, 3344, 3552, 0.03, [('FLOWERS', 4), ('BUSH', 1)], seed=94)
# willows along the River Lum
for y in range(MH):
    for x in range(MW):
        if TY[y][x] == 'GRASS' and not RES[y][x] and hsh(x, y, 5) % 23 == 0 and \
                any(inb(x + dx, y + dy) and TY[y + dy][x + dx] == 'WATER' for dx in (-1, 1) for dy in (-1, 0, 1)):
            put(x, y, 'WILLOW')

# ========================================================================================
# QUEST OBJECTS (cell coordinates unless noted; checked for reachability below)
# ========================================================================================
QPOS = {}           # named cells for quest scripts ('near NAME R')
QUEST_OBJS = []     # (type, x, y)


def qobj(t, x, y, name=None):
    assert TY[y][x] in WALKSET and TY[y][x] != 'DOOR', ('quest object on a blocked cell', t, x, y, TY[y][x])
    put(x, y, t)
    QUEST_OBJS.append((t, x, y))
    if name:
        QPOS[name] = (x, y)


def qobj_near(t, X, Y, name=None, on=('GRASS', 'FLOWERS')):
    """Place an object on the nearest free open-ground cell to OSRS (X, Y) that keeps a walkable
    neighbour."""
    x0, y0 = cx(X), cy(Y)
    for rad in range(0, 8):
        for dy in range(-rad, rad + 1):
            for dx in range(-rad, rad + 1):
                if max(abs(dx), abs(dy)) != rad:
                    continue
                x, y = x0 + dx, y0 + dy
                if not inb(x, y) or TY[y][x] not in on:
                    continue
                if sum(1 for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1))
                       if inb(x + ax, y + ay) and TY[y + ay][x + ax] in WALKSET) < 3:
                    continue
                qobj(t, x, y, name)
                return (x, y)
    raise SystemExit('no cell for quest object %s near %d,%d' % (t, X, Y))


# Mill Lane Mill: hopper and flour bin (the mill is a single storey here)
MILL = orect(3162, 3302, 3170, 3310)
qobj('HOPPER', MILL[0] + 1, MILL[1] + 1)
qobj('FLOURBIN', MILL[0] + 1, MILL[3] - 1)
# spinning wheel in Lumbridge Castle (the hall west of the throne)
qobj('WHEEL', kx0 + 1, ky0 + 6)
# the restless ghost's coffin among the Lumbridge graves
qobj('COFFIN', cx(3243), cy(3195))
# altar on the Wizards' Tower floor (the basement altar in OSRS)
qobj('ALTAR', int(wcx) + 3, int(wcy), 'TOWER_ALTAR')
# cadava bushes south-west of the Varrock SE mine and south of Varrock
qobj_near('CADAVA', 3268, 3370)
qobj_near('CADAVA', 3272, 3374)
qobj_near('CADAVA', 3205, 3376)
# Hetty's cauldron in Rimmington
HETTY_H = orect(2962, 3208, 2970, 3215)
qobj('CAULDRON', HETTY_H[2] - 1, HETTY_H[1] + 2)
# Draynor Manor: the west wing becomes a locked room reached from the hall; fountain and
# compost heap in the garden
mx0, my0, mx1, my1 = MANOR
TY[my0 + 3][mx0 + 5] = 'WALL'                      # close the west wing's door to the middle room
for x in range(mx0 + 1, mx0 + 5):
    TY[my1 - 3][x] = 'WALL'
TY[my1 - 3][mx0 + 2] = 'LOCKDOOR'
QUEST_OBJS.append(('LOCKDOOR', mx0 + 2, my1 - 3))
# Al Kharid toll gate: 10 coins each way, collected by the border guards on the Lumbridge side
for (x, y) in TOLLGATE:
    TY[y][x] = 'TOLLGATE'
    QUEST_OBJS.append(('TOLLGATE', x, y))
QPOS['TOLLGATE_W'] = (TOLLGATE[0][0] - 1, TOLLGATE[0][1])
for (dx, dy) in ((0, 0), (1, 0), (0, 1), (1, 1)):
    put(mx0 + dx, my1 + 4 + dy, 'FOUNTAIN')
QPOS['MANOR_FOUNTAIN'] = (mx0, my1 + 4)
QUEST_OBJS.append(('FOUNTAIN', mx0, my1 + 4))
qobj('COMPOST', mx1 - 1, my1 + 4)
# Blue Moon Inn chest (Pirate's Treasure)
BLUE_MOON = orect(3218, 3388, 3234, 3398)
qobj('CHEST', BLUE_MOON[2] - 1, BLUE_MOON[3] - 1)
# Black Knights' Fortress: listening grill and the hole above the cauldron (east wing)
qobj('GRILL', BKF[2] - 2, BKF[1] + 1)
qobj('HOLE', BKF[2] - 1, BKF[3] - 2)
BKF_DOOR = (BKF[0], cy(3510))
# Varrock Palace drain (outside the kitchen, east side of the palace)
qobj_near('DRAIN', 3227, 3478, on=('GRASS', 'FLOWERS'))
# crop fields picked by quest scripts
QPOS['WHEAT_FIELD'] = (cx(3158), cy(3292))
QPOS['ONION_FIELD'] = (cx(3203), cy(3290))
QPOS['CABBAGE_FIELD'] = (cx(3048), cy(3280))
QPOS['PARK_DIG'] = (cx(3005), cy(3374))     # just south of the Falador park fountain

# ========================================================================================
# NPCs
# ========================================================================================
def flood():
    from collections import deque
    seen = [[False] * MW for _ in range(MH)]
    q = deque([START])
    seen[START[1]][START[0]] = True
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            xx, yy = x + dx, y + dy
            if inb(xx, yy) and not seen[yy][xx] and (TY[yy][xx] in WALKSET or TY[yy][xx] in ('LOCKDOOR', 'TOLLGATE')):
                seen[yy][xx] = True
                q.append((xx, yy))
    return seen


REACH = flood()
SPAWNS.append(('GUIDE', START[0] + 2, START[1] - 1))
spawn('MAN', 3225, 3240)
spawn('WOMAN', 3232, 3226)
spawn('MAN', 3200, 3250)
spawn('WOMAN', 3215, 3256)
spawn_area('GOBLIN', 7, 3248, 3228, 3264, 3252)
spawn_area('COW', 9, 3255, 3256, 3264, 3297)
spawn_area('CHICKEN', 6, 3221, 3287, 3228, 3299)
spawn_area('CHICKEN', 6, 3169, 3287, 3185, 3295)
spawn_area('COW', 6, 3021, 3297, 3045, 3313)
# Draynor
spawn_area('MAN', 3, 3074, 3244, 3100, 3256)
spawn('GUARD', 3076, 3250)
spawn('GUARD', 3083, 3250)
spawn('WOMAN', 3104, 3240)
# Port Sarim / Rimmington
spawn('MAN', 3036, 3230)
spawn('WOMAN', 3030, 3244)
spawn('MAN', 2960, 3220)
spawn('WOMAN', 2952, 3226)
# Falador
for (X, Y) in ((2965, 3390), (3007, 3328), (3056, 3340), (2995, 3372), (3010, 3362), (2970, 3350),
               (2975, 3340), (2962, 3340), (2944, 3365)):
    spawn('GUARD', X, Y)
spawn_area('MAN', 3, 2950, 3355, 3040, 3365)
spawn_area('WOMAN', 3, 2960, 3366, 3030, 3390)
spawn_area('GOBLIN', 7, 2946, 3490, 2966, 3512)
# Edgeville / Barbarian Village
spawn_area('MAN', 3, 3080, 3480, 3110, 3506)
spawn_area('BARBARIAN', 7, 3066, 3412, 3096, 3450)
spawn_area('BARBARIAN', 3, 3074, 3436, 3084, 3446)
# Varrock
for (X, Y) in ((3176, 3430), (3272, 3430), (3210, 3386), (3214, 3386), (3204, 3424), (3220, 3432),
               (3210, 3460), (3214, 3460), (3240, 3464), (3244, 3502), (3205, 3480), (3219, 3480)):
    spawn('GUARD', X, Y)
spawn_area('MAN', 5, 3180, 3395, 3270, 3460)
spawn_area('WOMAN', 4, 3180, 3395, 3270, 3460)
spawn_area('DARK_WIZARD', 6, 3220, 3365, 3230, 3375)
# Al Kharid
spawn_area('WARRIOR', 8, 3283, 3159, 3302, 3177)
spawn_area('MAN', 3, 3270, 3185, 3320, 3215)
spawn('GUARD', 3270, 3230)
spawn('GUARD', 3270, 3225)

# --- quest NPCs ------------------------------------------------------------------------------
SPAWNS.append(('DUKE', *DUKE))
spawn('FRED', 3188, 3275)
spawn_area('SHEEP', 5, 3197, 3258, 3212, 3266)
spawn('DAIRY_COW', 3256, 3272)
SPAWNS.append(('AERECK', *AERECK))
spawn('URHNEY', 3147, 3183)
spawn('GHOST', cx(3243) - 1, cy(3195) + 1, cell=True)
spawn('ROMEO', 3211, 3423)
SPAWNS.append(('JULIET', *JULIET))
SPAWNS.append(('LAWRENCE', *LAWRENCE))
spawn('APOTHECARY', 3194, 3406)
SPAWNS.append(('DORIC', *DORIC_POS))
spawn('SEDRIDOR', 3104, 3160)
spawn('MIZGOG', 3109, 3166)
spawn('TRAIBORN', 3112, 3156)
spawn('SKELETON', int(wcx) + 2, int(wcy) + 2, cell=True)
spawn('AUBURY', 3254, 3405)
for (X, Y) in ((3236, 3262), (3122, 3272), (3004, 3302), (3292, 3420), (3068, 3398)):
    spawn('IMP', X, Y)
spawn('HETTY', HETTY_H[0] + 2, HETTY_H[1] + 1, cell=True)
spawn_area('RAT', 3, 2950, 3238, 2966, 3252)
spawn('AGGIE', 3082, 3263)
spawn('WARTFACE', 2955, 3500)
spawn('BENTNOZE', 2959, 3500)
spawn('VERONICA', 3110, 3327)
spawn('ODDENSTEIN', mx1 - 2, my0 + 3, cell=True)
spawn('FRANK', 3029, 3226)
spawn('BARTENDER', 3045, 3236)
spawn('AMIK', 2971, 3344)
SPAWNS.append(('BK_GUARD', BKF_DOOR[0] + 1, BKF_DOOR[1]))
spawn('BORDER_GUARD', TOLLGATE[0][0] - 1, TOLLGATE[0][1] - 1, cell=True)
spawn('BORDER_GUARD', TOLLGATE[-1][0] - 1, TOLLGATE[-1][1] + 1, cell=True)
spawn('ARIS', 3203, 3424)
spawn('PRYSIN', PALACE[0] + 2, PALACE[1] + 5, cell=True)
spawn('ROVIN', PALACE[2] - 2, PALACE[1] + 5, cell=True)
spawn('DELRITH', int(ccx), int(ccy), cell=True)

# --- respawning ground items (item, x, y) -----------------------------------------------------
ITEM_SPAWNS = []


def item_spawn(item, x, y):
    assert TY[y][x] in WALKSET and TY[y][x] not in ('DOOR',), (item, x, y, TY[y][x])
    ITEM_SPAWNS.append((item, x, y))


item_spawn('EGG', cx(3231), cy(3298))                       # north chicken coop
item_spawn('EGG', cx(3174), cy(3300))                       # Fred's chicken coop
item_spawn('POT', kx0 + 4, ky1 - 3)                          # castle kitchen
item_spawn('BUCKET', kx0 + 1, ky1 - 3)
FRED_H = orect(3184, 3270, 3192, 3279)
item_spawn('SHEARS', FRED_H[0] + 1, FRED_H[1] + 1)
item_spawn('BUCKET', FRED_H[2] - 1, FRED_H[1] + 1)
item_spawn('TUBE', mx0 + 2, my0 + 2)                         # locked west wing of the manor
item_spawn('OIL_CAN', mx0 + 6, my0 + 1)
item_spawn('POISON', mx0 + 7, my1 - 2)
item_spawn('FISH_FOOD', mx1 - 1, my0 + 1)
item_spawn('SPADE', mx0 + 1, my1 - 1)

# ========================================================================================
# Fishing spots: (kind, OSRS x, y) -> nearest water cell with walkable land beside it
# ========================================================================================
FISH_SPOTS = []


def fish(kind, X, Y):
    x0, y0 = cx(X), cy(Y)
    best = None
    for y in range(y0 - 6, y0 + 7):
        for x in range(x0 - 6, x0 + 7):
            if not inb(x, y) or TY[y][x] != 'WATER':
                continue
            if any((k, xx, yy) for (k, xx, yy) in FISH_SPOTS if (xx, yy) == (x, y)):
                continue
            land = [(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                    if inb(x + dx, y + dy) and TY[y + dy][x + dx] in WALKSET and TY[y + dy][x + dx] != 'BRIDGE']
            if not land:
                continue
            d = (x - x0) ** 2 + (y - y0) ** 2
            if best is None or d < best[0]:
                best = (d, x, y)
    assert best, ('no fishing water near', kind, X, Y)
    FISH_SPOTS.append((kind, best[1], best[2]))


for (X, Y) in ((3086, 3228), (3090, 3228), (3082, 3226)):                   # Draynor shore
    fish('NET', X, Y)
for (X, Y) in ((3242, 3150), (3246, 3146), (3238, 3146)):                   # Lumbridge Swamp
    fish('NET', X, Y)
for (X, Y) in ((3270, 3146), (3276, 3146)):                                 # Al Kharid
    fish('NET', X, Y)
for (X, Y) in ((3104, 3434), (3104, 3428), (3110, 3440)):                   # Barbarian Village (fly)
    fish('ROD', X, Y)
for (X, Y) in ((3238, 3246), (3238, 3256)):                                 # Lumbridge river (fly)
    fish('ROD', X, Y)
for (X, Y) in ((3022, 3222), (3018, 3210)):                                 # Port Sarim
    fish('NET', X, Y)

# ========================================================================================
# Regions for the area banner (first match wins; last entry is the fallback)
# ========================================================================================
REGIONS_O = [
    ('Wilderness', 2928, 3521, 3343, 3552),
    ('Lumbridge Castle', 3199, 3201, 3233, 3235),
    ('Lumbridge Swamp', 3133, 3137, 3260, 3200),
    ('Wizards\' Tower', 3090, 3140, 3130, 3180),
    ('Lumbridge', 3196, 3200, 3266, 3262),
    ('Draynor Manor', 3086, 3330, 3130, 3380),
    ('Draynor Village', 3060, 3220, 3135, 3280),
    ('Port Sarim', 3000, 3180, 3060, 3262),
    ('Rimmington', 2930, 3190, 2999, 3255),
    ('White Knights\' Castle', 2954, 3326, 2988, 3352),
    ('Falador Park', 2988, 3366, 3024, 3389),
    ('Falador', 2934, 3308, 3066, 3396),
    ('Black Knights\' Fortress', 3004, 3500, 3036, 3520),
    ('Ice Mountain', 2986, 3452, 3046, 3510),
    ('Goblin Village', 2940, 3486, 2972, 3518),
    ('Monastery', 3038, 3476, 3064, 3504),
    ('Edgeville', 3066, 3460, 3130, 3520),
    ('Barbarian Village', 3062, 3405, 3100, 3455),
    ('Cooking Guild', 3136, 3436, 3150, 3450),
    ('Grand Exchange', 3150, 3476, 3180, 3505),
    ('Varrock Palace', 3196, 3458, 3230, 3504),
    ('Varrock Square', 3200, 3416, 3226, 3440),
    ('Varrock', 3172, 3380, 3278, 3507),
    ('Champions\' Guild', 3184, 3354, 3200, 3368),
    ('Varrock Mine', 3170, 3358, 3188, 3378),
    ('Varrock Mine', 3276, 3355, 3296, 3378),
    ('Dark Wizards\' Circle', 3216, 3361, 3234, 3379),
    ('Al Kharid Mine', 3286, 3268, 3318, 3325),
    ('Duel Arena', 3312, 3200, 3343, 3266),
    ('Al Kharid', 3266, 3137, 3343, 3240),
    ('Kharidian Desert', 3266, 3137, 3343, 3330),
    ('Cow Field', 3253, 3254, 3266, 3299),
    ('Fred\'s Farm', 3150, 3255, 3215, 3312),
    ('Lumbridge Farms', 3216, 3262, 3266, 3330),
    ('Lumbridge Woods', 3133, 3200, 3196, 3262),
    ('Misthalin', 2928, 3137, 3343, 3552),
]
REGIONS = []
for (n, X0, Y0, X1, Y1) in REGIONS_O:
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    REGIONS.append((n, max(0, x0), max(0, y0), min(MW - 1, x1), min(MH - 1, y1)))

# notable NPC cells (reported for quest placement)
LANDMARKS = {
    'start/respawn (Lumbridge courtyard)': START,
    'Cook (castle kitchen)': LUMB_COOK,
    'Duke Horacio (castle throne room)': DUKE,
    'Father Aereck (Lumbridge church)': AERECK,
    'Doric (house north of Falador)': DORIC_POS,
    'Romeo (Varrock Square)': (cx(3211), cy(3423)),
    'Juliet (house west of Varrock)': JULIET,
    'Father Lawrence (Varrock church)': LAWRENCE,
    'King Roald (Varrock Palace)': KING,
    'Emir/Al Kharid palace': EMIR,
    'Hans (Lumbridge courtyard)': HANS,
}

# ========================================================================================
# Validation: reachability from the start position
# ========================================================================================


def check_reachability():
    from collections import deque
    seen = [[False] * MW for _ in range(MH)]
    q = deque([START])
    seen[START[1]][START[0]] = True
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            xx, yy = x + dx, y + dy
            if inb(xx, yy) and not seen[yy][xx] and (TY[yy][xx] in WALKSET or TY[yy][xx] in ('LOCKDOOR', 'TOLLGATE')):
                seen[yy][xx] = True
                q.append((xx, yy))
    errs = []

    def near(x, y):
        return any(inb(x + dx, y + dy) and seen[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
    for (k, x, y) in FISH_SPOTS:
        if not near(x, y):
            errs.append('fish spot %s %d,%d' % (k, x, y))
    for y in range(MH):
        for x in range(MW):
            if TY[y][x] in ('BOOTH', 'COUNTER', 'RANGE', 'FIREPIT') and not near(x, y):
                errs.append('%s at %d,%d unreachable' % (TY[y][x], x, y))
    for (t, x, y) in QUEST_OBJS:
        if not near(x, y):
            errs.append('quest object %s at %d,%d unreachable' % (t, x, y))
    for (it, x, y) in ITEM_SPAWNS:
        if not seen[y][x]:
            errs.append('item spawn %s at %d,%d unreachable' % (it, x, y))
    for y in range(MH):
        for x in range(MW):
            if TY[y][x] in ('CROPS', 'CLAY') and not near(x, y) and TY[y][x] == 'CLAY':
                errs.append('%s at %d,%d unreachable' % (TY[y][x], x, y))
    for (k, x, y) in SPAWNS:
        if k in ('BANKER', 'SHOPKEEPER'):
            continue
        if not seen[y][x]:
            errs.append('npc %s at %d,%d unreachable' % (k, x, y))
    for name, (x, y) in LANDMARKS.items():
        if not (seen[y][x] or near(x, y)):
            errs.append('landmark %s at %d,%d unreachable' % (name, x, y))
    for (n, x0, y0, x1, y1) in REGIONS[:-1]:
        cnt = sum(seen[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))
        if cnt == 0:
            errs.append('region %s unreachable' % n)
    if errs:
        for e in errs:
            print('MAP ERROR:', e, file=sys.stderr)
        raise SystemExit('map validation failed (%d errors)' % len(errs))
    return sum(sum(r) for r in seen)


CHARS = {'GRASS': '.', 'FLOWERS': ',', 'PATH': '_', 'COBBLE': '=', 'FLOOR': ':', 'WOODFLOOR': 'h', 'CARPET': 'c',
         'BRIDGE': '#', 'SAND': 's', 'DESERT': 'd', 'SWAMP': '~', 'WILD': 'x', 'SNOW': '*', 'DITCH': '-', 'DOOR': 'D',
         'WATER': 'w', 'WALL': 'X', 'HWALL': 'H', 'WWALL': 'O', 'SWALL': 'S', 'FENCE': 'f', 'TREE': 'T', 'OAK': 'K',
         'WILLOW': 'W', 'YEW': 'Y', 'DEADTREE': 't', 'CACTUS': 'j', 'STUMP': 'u', 'COPPER': '1', 'TIN': '2',
         'IRON': '3', 'ROCK_EMPTY': '0', 'BOULDER': 'o', 'BOOTH': 'B', 'RANGE': 'R', 'COUNTER': 'C', 'BUSH': 'b',
         'FOUNTAIN': 'F', 'CROPS': 'r', 'STALL': 'L', 'ALTAR': 'A', 'CLIFF': '^', 'FIREPIT': 'P', 'GRAVE': '+',
         'CLAY': '4', 'HOPPER': 'V', 'FLOURBIN': 'U', 'WHEEL': '@', 'COFFIN': '&', 'CADAVA': '%', 'CAULDRON': 'Q',
         'COMPOST': '$', 'LOCKDOOR': '!', 'GRILL': 'G', 'HOLE': '?', 'DRAIN': 'z', 'CHEST': 'k', 'TOLLGATE': 'g'}


def ascii_map():
    return '\n'.join(''.join(CHARS[t] for t in row) for row in TY)


import os
if os.environ.get("MAPDUMP"):
    open(os.environ["MAPDUMP"], "w").write(ascii_map())
REACHABLE = check_reachability()

# ========================================================================================
# Cell rendering
# ========================================================================================


def T_at(x, y):
    return TY[y][x] if inb(x, y) else None


def G_at(x, y):
    return GR[y][x] if inb(x, y) else None


def grass_px(px, py, var):
    h = hsh(var, px, py) % 100
    if h < 7:
        return WC['G2']
    if h < 17:
        return WC['G1']
    if h < 20 and py % 2 == 0:
        return WC['G3']
    return WC['G0']


def dirt_px(px, py, var):
    h = hsh(var + 50, px, py) % 100
    if h < 8:
        return WC['D2']
    if h < 20:
        return WC['D1']
    return WC['D0']


def water_px(px, py):
    if py % 4 == 1 and (px + 5 * (py // 4)) % 8 in (0, 1, 2):
        return WC['W1']
    if py % 8 == 3 and (px + 3 * (py // 8)) % 16 == 9:
        return WC['W2']
    return WC['W0']


def floor_px(px, py):
    row = py // 8
    sx = (px + (4 if row % 2 else 0)) % 8
    sy = py % 8
    if sx == 7 or sy == 7:
        return WC['F2']
    if sx == 0 or sy == 0:
        return WC['F1']
    return WC['F0']


def woodfloor_px(px, py):
    sy = py % 4
    if sy == 3:
        return WC['H2']
    if (px + 5 * (py // 4)) % 16 == 0:
        return WC['H2']
    if sy == 0:
        return WC['H1']
    return WC['H0']


def cobble_px(px, py):
    r = py // 4
    sx = (px + (3 if r % 2 else 0)) % 6
    sy = py % 4
    if sy == 3 or sx == 5:
        return WC['F2']
    if sy == 0 or sx == 0:
        return WC['C0'] if hsh(11, px // 6, r) % 3 == 0 else WC['F1']
    return WC['F0']


def carpet_px(px, py):
    if (px + py) % 6 == 0 or (px - py) % 6 == 0:
        return WC['CP0']
    return WC['CP1']


def desert_px(px, py, var):
    rip = (py * 2 + int(2.5 * math.sin(px * 0.55 + var * 2.1))) % 9
    if rip == 0:
        return WC['DS1']
    if rip == 1 and hsh(px, py, 3) % 2:
        return WC['DS2']
    h = hsh(var + 90, px, py) % 100
    if h < 4:
        return WC['DS3']
    return WC['DS0']


def swamp_px(px, py, var):
    h = hsh(var + 20, px, py) % 100
    if ((px - 4 - var * 3) ** 2 + (py - 10) ** 2) < 7:
        return WC['SW3'] if h < 70 else WC['SW2']
    if h < 10:
        return WC['SW2']
    if h < 24:
        return WC['SW1']
    return WC['SW0']


def wild_px(px, py, var):
    h = hsh(var + 30, px, py) % 100
    if h < 8:
        return WC['WD2']
    if h < 22:
        return WC['WD1']
    if h < 26 and py % 3 == 0:
        return WC['WD3']
    return WC['WD0']


def snow_px(px, py, var):
    return WC['SN1'] if hsh(var + 40, px, py) % 7 == 0 else WC['SN0']


def sand_px(px, py, var):
    return WC['S0'] if hsh(px, py) % 5 else WC['S1']


GPX = {
    'GRASS': grass_px, 'FLOWERS': grass_px, 'PATH': dirt_px, 'DESERT': desert_px, 'SWAMP': swamp_px,
    'WILD': wild_px, 'SNOW': snow_px, 'SAND': sand_px, 'DITCH': wild_px,
    'COBBLE': lambda px, py, v: cobble_px(px, py), 'FLOOR': lambda px, py, v: floor_px(px, py),
    'DOOR': lambda px, py, v: floor_px(px, py), 'WOODFLOOR': lambda px, py, v: woodfloor_px(px, py),
    'CARPET': lambda px, py, v: carpet_px(px, py), 'BRIDGE': dirt_px, 'WATER': lambda px, py, v: water_px(px, py),
}
RANK = {'COBBLE': 1, 'PATH': 2, 'SAND': 3, 'DESERT': 3, 'SWAMP': 4, 'SNOW': 4, 'WILD': 5, 'GRASS': 6, 'FLOWERS': 6}
DIRS8 = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]


def base_img(g, var):
    f = GPX.get(g, grass_px)
    return [[f(px, py, var) for px in range(16)] for py in range(16)]


def blend(img, nb, me, var):
    """nb: list of neighbour grounds in DIRS8 order. Higher-rank neighbours bleed in."""
    r0 = RANK.get(me)
    if r0 is None:
        return
    others = sorted({g for g in nb if g in RANK and RANK[g] > r0 and g != me}, key=lambda g: RANK[g])
    for n in others:
        f = GPX[n]
        isn = [g == n for g in nb]
        for py in range(16):
            for px in range(16):
                jag = (hsh(7, px, py) % 3 == 0)
                lim = 1 + (1 if jag else 0)
                d = 99
                if px <= lim and isn[0]:
                    d = min(d, px)
                if px >= 15 - lim and isn[1]:
                    d = min(d, 15 - px)
                if py <= lim and isn[2]:
                    d = min(d, py)
                if py >= 15 - lim and isn[3]:
                    d = min(d, 15 - py)
                if px <= lim and py <= lim and isn[4]:
                    d = min(d, max(px, py))
                if px >= 15 - lim and py <= lim and isn[5]:
                    d = min(d, max(15 - px, py))
                if px <= lim and py >= 15 - lim and isn[6]:
                    d = min(d, max(px, 15 - py))
                if px >= 15 - lim and py >= 15 - lim and isn[7]:
                    d = min(d, max(15 - px, 15 - py))
                if d <= lim:
                    img[py][px] = f(px, py, var)


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


def ellipse_fill(img, cx_, cy_, rx, ry, col):
    for y in range(len(img)):
        for x in range(len(img[0])):
            if ((x - cx_) / rx) ** 2 + ((y - cy_) / ry) ** 2 <= 1.0:
                img[y][x] = col


def tree_image(kind):
    img = blank(16, 32)
    trunk = blank(16, 32)
    if kind == 'DEADTREE':
        for y in range(14, 31):
            for x in range(7, 9 + (1 if y > 26 else 0)):
                trunk[y][x] = WC['K1'] if x == 7 else WC['K0']
        for (x0, y0, dx, n) in ((7, 20, -1, 5), (8, 17, 1, 5), (7, 14, -1, 4), (8, 12, 1, 3), (7, 10, 0, 3)):
            x, y = x0, y0
            for i in range(n):
                x += dx
                y -= 1
                if 0 <= x < 16:
                    trunk[y][x] = WC['K0']
        paste(img, outline(trunk, WC['K2']), 0, 0)
        return img
    if kind == 'TREE':
        cx_, cy_, rx, ry = 7.5, 13.0, 7.2, 8.5
        cols = (WC['L2'], WC['L0'], WC['L1'], WC['L3'])
    elif kind == 'OAK':
        cx_, cy_, rx, ry = 7.5, 12.5, 7.6, 10.0
        cols = (WC['O2'], WC['O0'], WC['O1'], WC['O3'])
    elif kind == 'YEW':
        cx_, cy_, rx, ry = 7.5, 11.5, 7.8, 11.0
        cols = (WC['YW2'], WC['YW0'], WC['YW1'], WC['YW3'])
    else:
        cx_, cy_, rx, ry = 7.5, 11.0, 7.4, 7.5
        cols = (WC['Y2'], WC['Y0'], WC['Y1'], WC['Y3'])
    for y in range(18, 31):
        for x in range(6, 10):
            trunk[y][x] = WC['K1'] if x in (7, 8) and y % 3 else WC['K0']
    for x in (5, 10):
        trunk[30][x] = WC['K0']
    paste(img, outline(trunk, WC['K2']), 0, 0)
    canopy = blank(16, 32)
    for y in range(32):
        for x in range(16):
            dx = (x - cx_) / rx
            dy = (y - cy_) / ry
            wob = 0.08 * math.sin(x * 1.7 + y * 0.9) + 0.06 * math.cos(y * 2.3)
            if kind == 'YEW':
                wob += 0.10 * math.sin(y * 1.9)
            if dx * dx + dy * dy <= 1.0 + wob:
                light = -(dx * 0.7 + dy * 0.9) + ((hsh(len(kind), x, y) % 100) - 50) / 160.0
                if kind == 'WILLOW' and x % 2 == 0 and dy > 0.1:
                    light -= 0.35
                if kind == 'YEW' and (y % 4 == 0) and dy > -0.5:
                    light -= 0.4
                c = cols[3] if light > 0.55 else cols[2] if light > 0.05 else cols[1] if light > -0.55 else cols[0]
                canopy[y][x] = c
    if kind == 'WILLOW':
        for x in range(1, 15, 2):
            top = int(cy_ + 2)
            for y in range(top, top + 6 + (hsh(x) % 5)):
                if y < 32:
                    canopy[y][x] = cols[1] if y % 3 else cols[2]
    canopy = outline(canopy, cols[0])
    paste(img, canopy, 0, 0)
    return img


TREE_IMG = {k: tree_image(k) for k in TREES}


def rock_image(ore):
    r = blank(16, 16)
    for (cx_, cy_, rad) in ((8, 9, 5.8), (4.5, 11, 3.8), (11.5, 11, 3.8), (7, 6, 3.5)):
        for y in range(16):
            for x in range(16):
                if (x - cx_) ** 2 + (y - cy_) ** 2 <= rad * rad:
                    r[y][x] = 1
    img = blank(16, 16)
    for y in range(16):
        for x in range(16):
            if r[y][x]:
                up = y == 0 or not r[y - 1][x]
                lt = x == 0 or not r[y][x - 1]
                dn = y == 15 or not r[y + 1][x]
                if up or (lt and y < 10):
                    img[y][x] = WC['R1']
                elif dn or y > 12:
                    img[y][x] = WC['R2']
                else:
                    img[y][x] = WC['R0'] if hsh(9, x, y) % 5 else WC['R2']
    if ore:
        oc, od = {'COPPER': ('CU', 'CUd'), 'TIN': ('TI', 'TId'), 'IRON': ('FE', 'FEd'), 'CLAY': ('S1', 'D1')}[ore]
        for (fx, fy) in ((5, 9), (9, 6), (11, 11), (7, 12), (3, 11)):
            img[fy][fx] = WC[oc]
            img[fy][fx + 1] = WC[od]
            img[fy + 1][fx] = WC[od]
    return outline(img, WC['OUT'])


def stump_image():
    img = blank(16, 16)
    for y in range(9, 15):
        for x in range(5, 11):
            img[y][x] = WC['K1'] if x in (6, 7) else WC['K0']
    ellipse_fill(img, 7.5, 9.5, 3.2, 1.8, WC['H1'])
    img[9][7] = WC['H2']
    img[9][8] = WC['H0']
    img[14][4] = WC['K0']
    img[14][11] = WC['K0']
    return outline(img, WC['K2'])


def bush_image():
    img = blank(16, 16)
    for (cx_, cy_, rr) in ((8, 9, 5.5), (5, 11, 4), (11, 11, 4)):
        for y in range(16):
            for x in range(16):
                if (x - cx_) ** 2 + (y - cy_) ** 2 <= rr * rr:
                    light = -((x - 8) * 0.1 + (y - 9) * 0.15) + (hsh(3, x, y) % 10 - 5) / 20
                    img[y][x] = WC['L3'] if light > 0.6 else WC['L1'] if light > 0 else WC['L0']
    img[7][7] = WC['FR']
    img[10][11] = WC['FR']
    return outline(img, WC['L2'])


def cactus_image():
    img = blank(16, 16)
    for y in range(3, 15):
        for x in range(6, 10):
            img[y][x] = WC['CA1'] if x == 7 else WC['CA0'] if x != 9 else WC['CA2']
    for y in range(6, 10):
        img[y][3] = WC['CA0']
        img[y][4] = WC['CA1']
    for x in range(3, 6):
        img[10][x] = WC['CA0']
    for y in range(4, 9):
        img[y][12] = WC['CA0']
        img[y][11] = WC['CA1']
    for x in range(10, 13):
        img[9][x] = WC['CA0']
    img[3][7] = WC['FR']
    return outline(img, WC['CA2'])


def grave_image():
    img = blank(16, 16)
    for y in range(4, 14):
        for x in range(5, 11):
            if y == 4 and x in (5, 10):
                continue
            img[y][x] = WC['R1'] if x == 5 or y == 5 else WC['R0']
    for x in range(7, 9):
        img[7][x] = WC['R2']
    for y in range(6, 10):
        img[y][7] = WC['R2'] if y != 7 else WC['R2']
    return outline(img, WC['OUT'])


def firepit_image():
    img = blank(16, 16)
    for k in range(10):
        a = k * math.pi / 5
        x, y = int(7.5 + 5.5 * math.cos(a)), int(10 + 3.5 * math.sin(a))
        img[y][x] = WC['R1']
        if x + 1 < 16:
            img[y][x + 1] = WC['R2']
    for x in range(4, 12):
        img[11][x] = WC['K0']
        img[10][x + (1 if x % 2 else 0) - 1] = WC['K1']
    for y in range(3, 10):
        w = (10 - y) // 2
        for x in range(8 - w - 1, 8 + w):
            img[y][x] = WC['FI1'] if abs(x - 7) < w - 1 and y > 5 else WC['FI0']
    return outline(img, WC['OUT'])


def wall_image(t, x, y):
    below = T_at(x, y + 1)
    face = below not in WALLS
    img = blank(16, 16)
    if t == 'HWALL':
        if face:
            for py in range(16):
                for px in range(16):
                    if py < 3:
                        c = WC['K2'] if py == 2 else WC['K0']
                    elif px % 4 == 3 or py == 15:
                        c = WC['K2']
                    else:
                        c = WC['K1'] if px % 4 == 1 else WC['K0']
                    img[py][px] = c
        else:
            for py in range(16):
                for px in range(16):
                    img[py][px] = WC['K0'] if (py % 4) else WC['K2']
        return img
    c0, c1, c2, c3 = {'WALL': ('C0', 'C1', 'C2', 'C3'), 'WWALL': ('WW0', 'WW1', 'WW2', 'WW3'),
                      'SWALL': ('SS0', 'SS1', 'SS2', 'SS3')}[t]
    c0, c1, c2, c3 = WC[c0], WC[c1], WC[c2], WC[c3]
    if face:
        for py in range(16):
            for px in range(16):
                if py < 4:
                    c = c1 if py < 3 else c3
                elif py == 15:
                    c = c3
                else:
                    br = (py - 4) // 4
                    bx = (px + (4 if br % 2 else 0)) % 8
                    if (py - 4) % 4 == 3 or bx == 7:
                        c = c2
                    elif (py - 4) % 4 == 0:
                        c = c1
                    else:
                        c = c0
                img[py][px] = c
    else:
        for py in range(16):
            for px in range(16):
                img[py][px] = c0 if hsh(4, px, py) % 6 else c2
        nb = lambda dx, dy: T_at(x + dx, y + dy) not in WALLS
        for i in range(16):
            if nb(-1, 0):
                img[i][0] = c3
            if nb(1, 0):
                img[i][15] = c3
            if nb(0, -1):
                img[0][i] = c3
    return img


def cliff_image(x, y, base):
    img = [r[:] for r in base]
    snowy = GR[y][x] == 'SNOW'
    nb = lambda dx, dy: T_at(x + dx, y + dy) == 'CLIFF'
    for py in range(16):
        for px in range(16):
            h = hsh(13, px // 2, py // 2, x * 3 + y) % 100
            c = WC['R0'] if h < 55 else WC['R1'] if h < 80 else WC['R2']
            if snowy and h < 35 + (py < 8) * 30:
                c = WC['SN0'] if h % 3 else WC['SN1']
            img[py][px] = c
    if not nb(0, 1):
        for py in range(11, 16):
            for px in range(16):
                img[py][px] = WC['R2'] if (px + py // 2) % 4 else WC['OUT']
    for i in range(16):
        if not nb(-1, 0):
            img[i][0] = WC['R2']
        if not nb(1, 0):
            img[i][15] = WC['R2']
        if not nb(0, -1):
            img[0][i] = WC['R1']
    return img


def fountain_image(x, y):
    isf = lambda dx, dy: T_at(x + dx, y + dy) == 'FOUNTAIN'
    img = [[water_px(px, py) for px in range(16)] for py in range(16)]
    for py in range(16):
        for px in range(16):
            rim = (px < 3 and not isf(-1, 0)) or (px > 12 and not isf(1, 0)) or \
                  (py < 3 and not isf(0, -1)) or (py > 12 and not isf(0, 1))
            if rim:
                img[py][px] = WC['C1'] if (px + py) % 5 else WC['C0']
                if (px == 2 and not isf(-1, 0)) or (px == 13 and not isf(1, 0)) or \
                        (py == 2 and not isf(0, -1)) or (py == 13 and not isf(0, 1)):
                    img[py][px] = WC['C2']
    # central spout at the corner shared with the other fountain cells
    for (ddx, ddy, ox, oy) in ((1, 1, 13, 13), (-1, 1, 0, 13), (1, -1, 13, 0), (-1, -1, 0, 0)):
        if isf(ddx, 0) and isf(0, ddy) and isf(ddx, ddy):
            for py in range(3):
                for px in range(3):
                    img[oy + py][ox + px] = WC['C1'] if py == 0 else WC['C0']
            img[min(15, max(0, oy + (-1 if ddy > 0 else 3)))][ox + 1] = WC['W2']
    return img


def quest_art():
    """Pixel art for the quest objects (world palette colours only)."""
    A = {}
    # hopper: wooden funnel on legs
    im = blank(16, 16)
    for y in range(2, 9):
        w = 7 - (y - 2) * 0.6
        for x in range(int(8 - w), int(8 + w) + 1):
            im[y][x] = WC['K2'] if y == 2 and 4 < x < 12 else (WC['K1'] if x < 8 else WC['K0'])
    for y in range(9, 15):
        im[y][4] = WC['K0']
        im[y][11] = WC['K0']
    for x in range(4, 12):
        im[11][x] = WC['K1']
    for x in range(6, 10):
        im[2][x] = WC['CR0']
    A['HOPPER'] = outline(im, WC['OUT'])
    # flour bin: wooden box heaped with flour
    im = blank(16, 16)
    for y in range(6, 15):
        for x in range(2, 14):
            im[y][x] = WC['K1'] if y == 6 else WC['K2'] if y == 14 or x in (2, 13) else WC['K0']
    ellipse_fill(im, 8, 6, 5, 2.2, WC['WW1'])
    ellipse_fill(im, 7, 5.5, 2.5, 1.2, WC['WW0'])
    A['FLOURBIN'] = outline(im, WC['OUT'])
    # spinning wheel
    im = blank(16, 16)
    for k in range(24):
        a = k * math.pi / 12
        im[int(round(7 + 5 * math.sin(a)))][int(round(7 + 5 * math.cos(a)))] = WC['K1']
    for k in range(4):
        a = k * math.pi / 4
        for r in range(1, 5):
            im[int(round(7 + r * math.sin(a)))][int(round(7 + r * math.cos(a)))] = WC['K0']
            im[int(round(7 - r * math.sin(a)))][int(round(7 - r * math.cos(a)))] = WC['K0']
    for x in range(3, 15):
        im[14][x] = WC['K2']
    for y in range(7, 15):
        im[y][13] = WC['K0']
    im[7][7] = WC['WW1']
    im[6][12] = WC['WW1']
    A['WHEEL'] = outline(im, WC['OUT'])
    # coffin: dark wooden box with a gold cross
    im = blank(16, 16)
    pts = {}
    for y in range(1, 15):
        w = 4 + min(y - 1, 4) * 0.5 if y < 6 else 6 - (y - 6) * 0.25
        for x in range(int(8 - w), int(8 + w)):
            im[y][x] = WC['K2'] if x == int(8 - w) or y in (1, 14) else WC['K0']
    for y in range(4, 11):
        im[y][7] = WC['GOLD']
    for x in range(5, 10):
        im[6][x] = WC['GOLD']
    A['COFFIN'] = outline(im, WC['OUT'])
    # cadava bush: dark bush with red-purple berries
    im = blank(16, 16)
    for (cx_, cy_, rr) in ((8, 9, 5.5), (5, 11, 4), (11, 11, 4)):
        for y in range(16):
            for x in range(16):
                if (x - cx_) ** 2 + (y - cy_) ** 2 <= rr * rr:
                    light = -((x - 8) * 0.1 + (y - 9) * 0.15) + (hsh(5, x, y) % 10 - 5) / 20
                    im[y][x] = WC['L1'] if light > 0.5 else WC['L0']
    for (bx, by) in ((6, 6), (9, 8), (4, 10), (11, 11), (7, 12), (10, 5), (13, 9)):
        im[by][bx] = WC['CP0']
        im[by][bx + 1] = WC['CP2']
        im[by + 1][bx] = WC['CP2']
    A['CADAVA'] = outline(im, WC['L2'])
    # cauldron on a small fire
    im = blank(16, 16)
    ellipse_fill(im, 8, 9, 6, 5, WC['IR0'])
    ellipse_fill(im, 6, 8, 2, 2, WC['IR1'])
    ellipse_fill(im, 8, 5, 5, 1.6, WC['L3'])
    ellipse_fill(im, 7, 5, 2, 0.8, WC['G3'])
    for x in (4, 7, 10):
        im[14][x] = WC['FI0']
        im[13][x + 1] = WC['FI1']
    A['CAULDRON'] = outline(im, WC['OUT'])
    # compost heap
    im = blank(16, 16)
    for y in range(16):
        for x in range(16):
            if ((x - 8) / 7) ** 2 + ((y - 11) / 4.5) ** 2 <= 1:
                h = hsh(17, x, y) % 9
                im[y][x] = WC['L1'] if h == 0 else WC['K1'] if h < 3 else WC['K0'] if h < 7 else WC['K2']
    A['COMPOST'] = outline(im, WC['OUT'])
    # locked door: wooden door in a wall frame with a gold lock
    im = [[WC['F0'] if (x + y) % 7 else WC['F2'] for x in range(16)] for y in range(16)]
    for y in range(1, 16):
        for x in range(3, 13):
            im[y][x] = WC['K2'] if x in (3, 12) or y == 1 else (WC['K1'] if x % 3 else WC['K0'])
    im[9][10] = WC['GOLD']
    im[10][10] = WC['GOLD']
    im[10][9] = WC['OUT']
    A['LOCKDOOR'] = im
    # wall grill
    im = blank(16, 16)
    for y in range(3, 12):
        for x in range(3, 13):
            im[y][x] = WC['IR1'] if (x % 3 == 0 or y % 3 == 0) else WC['OUT']
    A['GRILL'] = outline(im, WC['IR2'])
    # hole in the floor
    im = blank(16, 16)
    ellipse_fill(im, 8, 9, 5.5, 3.5, WC['OUT'])
    ellipse_fill(im, 8, 8.5, 5.5, 3, WC['IR2'])
    ellipse_fill(im, 8, 10, 4, 2, WC['OUT'])
    A['HOLE'] = im
    # drain grate
    im = blank(16, 16)
    for y in range(4, 13):
        for x in range(3, 13):
            im[y][x] = WC['IR1'] if x % 2 == 0 and 4 < y < 12 else WC['IR0']
    A['DRAIN'] = outline(im, WC['OUT'])
    # chest
    im = blank(16, 16)
    for y in range(5, 15):
        for x in range(2, 14):
            im[y][x] = WC['K1'] if y < 8 else WC['K0']
    for y in range(5, 15):
        im[y][4] = WC['GOLD']
        im[y][11] = WC['GOLD']
    for x in range(2, 14):
        im[8][x] = WC['K2']
    im[9][7] = WC['GOLD']
    im[9][8] = WC['GOLD']
    A['CHEST'] = outline(im, WC['OUT'])
    # toll gate: a leaf of planks with iron bands across a north-south fence line
    im = blank(16, 16)
    for y in range(16):
        for x in range(5, 11):
            im[y][x] = WC['K2'] if x in (5, 10) else (WC['K1'] if x % 2 else WC['K0'])
        if y % 5 == 2:
            for x in range(4, 12):
                im[y][x] = WC['IR1']
    im[8][11] = WC['GOLD']
    im[9][11] = WC['GOLD']
    A['TOLLGATE'] = outline(im, WC['OUT'])
    return A


QUEST_ART = quest_art()


def render(x, y):
    t = TY[y][x]
    g = GR[y][x]
    var = hsh(x, y) % 3
    nb = [G_at(x + dx, y + dy) or g for dx, dy in DIRS8]
    nbt = [T_at(x + dx, y + dy) or t for dx, dy in DIRS8]
    over = None
    if t == 'WATER':
        img = [[water_px(px, py) for px in range(16)] for py in range(16)]
        land = [n not in ('WATER', 'BRIDGE') for n in nbt]
        for (pass_w, colf) in ((3, lambda px, py: WC['W2']), (2, lambda px, py: WC['S0'] if hsh(px, py) % 4 else WC['S1'])):
            for py in range(16):
                for px in range(16):
                    jag = (hsh(7, px, py) % 3 == 0)
                    lim = pass_w - 1 + (1 if jag else 0)
                    d = 99
                    if px <= lim and land[0]:
                        d = min(d, px)
                    if px >= 15 - lim and land[1]:
                        d = min(d, 15 - px)
                    if py <= lim and land[2]:
                        d = min(d, py)
                    if py >= 15 - lim and land[3]:
                        d = min(d, 15 - py)
                    if px <= lim and py <= lim and land[4]:
                        d = min(d, max(px, py))
                    if px >= 15 - lim and py <= lim and land[5]:
                        d = min(d, max(15 - px, py))
                    if px <= lim and py >= 15 - lim and land[6]:
                        d = min(d, max(px, 15 - py))
                    if px >= 15 - lim and py >= 15 - lim and land[7]:
                        d = min(d, max(15 - px, 15 - py))
                    if d <= lim:
                        img[py][px] = colf(px, py)
        return img, None
    if t == 'BRIDGE':
        ns = nbt[0] in ('WATER',) and nbt[1] in ('WATER',)
        img = blank(16, 16)
        for py in range(16):
            for px in range(16):
                a, b = (py, px) if ns else (px, py)
                img[py][px] = WC['K2'] if a % 4 == 3 else (WC['K1'] if b % 5 else WC['K0'])
        if ns:
            for py in range(16):
                for px in (0, 15):
                    if T_at(x + (-1 if px == 0 else 1), y) == 'WATER':
                        img[py][px] = WC['K2']
                        img[py][1 if px == 0 else 14] = WC['K0']
        else:
            for px in range(16):
                for py in (0, 15):
                    if T_at(x, y + (-1 if py == 0 else 1)) == 'WATER':
                        img[py][px] = WC['K2']
                        img[1 if py == 0 else 14][px] = WC['K0']
        return img, None
    if t in WALLS:
        return wall_image(t, x, y), None

    img = base_img(g, var)
    if t == g:
        blend(img, nb, g, var)
    if t == 'FLOWERS':
        fl = [WC['FY'], WC['FW'], WC['FR']]
        for i, (fx, fy) in enumerate(((3, 4), (10, 3), (6, 10), (12, 11))):
            c = fl[(var + i) % 3]
            img[fy][fx] = c
            img[fy + 1][fx] = WC['G2']
            img[fy][fx + 1] = c if i % 2 else WC['G3']
    elif t == 'DOOR':
        for px in range(16):
            img[0][px] = WC['F2']
            img[15][px] = WC['F2']
    elif t == 'DITCH':
        for py in range(5, 11):
            for px in range(16):
                img[py][px] = WC['OUT'] if 6 <= py <= 9 else WC['WD2']
        for px in range(0, 16, 3):
            img[5][px] = WC['WD1']
    elif t == 'CARPET':
        isc = lambda dx, dy: T_at(x + dx, y + dy) == 'CARPET'
        for i in range(16):
            if not isc(-1, 0):
                img[i][0] = WC['GOLD']
                img[i][1] = WC['CP2']
            if not isc(1, 0):
                img[i][15] = WC['GOLD']
                img[i][14] = WC['CP2']
    elif t == 'FENCE':
        isf = lambda n: n == 'FENCE'
        L, R, U, D = isf(nbt[0]), isf(nbt[1]), isf(nbt[2]), isf(nbt[3])
        if L or R:
            for px in range(0 if L else 7, (15 if R else 8) + 1):
                for py, c in ((6, 'K1'), (7, 'K2'), (10, 'K1'), (11, 'K2')):
                    img[py][px] = WC[c]
        if U or D:
            for py in range(0 if U else 5, (15 if D else 10) + 1):
                img[py][7] = WC['K1']
                img[py][8] = WC['K2']
        for py in range(4, 13):
            for px in range(6, 10):
                img[py][px] = WC['K2'] if px in (6, 9) or py in (4, 12) else WC['K0']
        img[5][7] = WC['K1']
    elif t in TREES:
        ti = TREE_IMG[t]
        sh = blank(16, 16)
        ellipse_fill(sh, 8, 14.5, 6, 1.8, {'GRASS': WC['G2'], 'DESERT': WC['DS3'], 'SWAMP': WC['SW2'],
                                            'WILD': WC['WD2'], 'SNOW': WC['SN1']}.get(g, WC['G2']))
        paste(img, sh, 0, 0)
        paste(img, ti[16:], 0, 0)
        over = ti[:16]
        if all(c is None for r in over for c in r):
            over = None
    elif t == 'STUMP':
        paste(img, stump_image(), 0, 0)
    elif t in ROCKS or t == 'ROCK_EMPTY':
        paste(img, rock_image(None if t == 'ROCK_EMPTY' else t), 0, 0)
    elif t == 'BOULDER':
        paste(img, rock_image(None), 0, 0)
    elif t == 'BUSH':
        paste(img, bush_image(), 0, 0)
    elif t == 'CACTUS':
        paste(img, cactus_image(), 0, 0)
    elif t == 'GRAVE':
        paste(img, grave_image(), 0, 0)
    elif t == 'FIREPIT':
        paste(img, firepit_image(), 0, 0)
    elif t == 'BOOTH':
        for py in range(3, 16):
            for px in range(16):
                if py < 9:
                    c = WC['K1'] if py > 3 else WC['K2']
                elif py == 9:
                    c = WC['GOLD']
                elif py == 15:
                    c = WC['K2']
                else:
                    c = WC['K0'] if px % 5 else WC['K2']
                img[py][px] = c
        for px in range(5, 11):
            img[5][px] = WC['PAPER']
            img[6][px] = WC['PAPER'] if px % 2 else WC['F1']
    elif t == 'COUNTER':
        for py in range(4, 15):
            for px in range(16):
                img[py][px] = WC['K1'] if py < 9 else (WC['K2'] if py in (9, 14) else WC['K0'])
    elif t == 'RANGE':
        for py in range(1, 15):
            for px in range(1, 15):
                edge = px in (1, 14) or py in (1, 14)
                img[py][px] = WC['IR2'] if edge else (WC['IR1'] if py < 4 else WC['IR0'])
        for (ax, ay) in ((5, 2), (10, 2)):
            img[ay][ax] = WC['IR2']
            img[ay][ax + 1] = WC['IR2']
        for py in range(8, 12):
            for px in range(4, 12):
                img[py][px] = WC['FI1'] if (py == 10 and px % 3 == 1) else WC['FI0']
        for px in range(4, 12):
            img[7][px] = WC['IR2']
    elif t == 'FOUNTAIN':
        img = fountain_image(x, y)
    elif t == 'CROPS':
        for py in range(16):
            for px in range(16):
                if px % 4 == 3:
                    continue
                if py % 8 < 2:
                    img[py][px] = WC['CR0'] if (px + py) % 2 else WC['CR1']
                elif py % 8 < 6:
                    img[py][px] = WC['CR1'] if px % 4 == 1 else WC['CR2']
    elif t == 'STALL':
        for py in range(9, 15):
            for px in range(1, 15):
                img[py][px] = WC['K1'] if py == 9 else WC['K2'] if py == 14 or px in (1, 14) else WC['K0']
        for py in range(1, 7):
            for px in range(0, 16):
                img[py][px] = WC['CP1'] if (px // 3) % 2 == 0 else WC['WW1']
        for px in range(0, 16):
            img[7][px] = WC['CP2'] if (px // 3) % 2 == 0 else WC['WW2']
        for py in range(7, 10):
            img[py][1] = WC['K2']
            img[py][14] = WC['K2']
        img[10][4] = WC['FR']
        img[10][9] = WC['FY']
        img[10][11] = WC['CR0']
    elif t == 'ALTAR':
        for py in range(5, 15):
            for px in range(2, 14):
                img[py][px] = WC['WW1'] if py < 7 else WC['WW2'] if py == 14 or px in (2, 13) else WC['WW0']
        for px in range(4, 12):
            img[7][px] = WC['CP1']
            img[8][px] = WC['CP1'] if px in (4, 11) else WC['CP0']
        for py in range(8, 13):
            img[py][7] = WC['GOLD']
            img[py][8] = WC['GOLD']
        img[10][6] = WC['GOLD']
        img[10][9] = WC['GOLD']
    elif t == 'CLIFF':
        img = cliff_image(x, y, img)
    elif t in QUEST_ART:
        paste(img, QUEST_ART[t], 0, 0)
    elif t in WALKSET:
        pass
    else:
        raise ValueError(t)
    return img, over


# ----------------------------------------------------------------------------------------
# Tile + palette packing helpers
# ----------------------------------------------------------------------------------------


def cut8(img16):
    tiles = []
    for ty in range(0, len(img16), 8):
        for tx in range(0, len(img16[0]), 8):
            tiles.append(tuple(tuple(img16[ty + y][tx + x] for x in range(8)) for y in range(8)))
    return tiles


def cdist(a, b):
    return (a[0] - b[0]) ** 2 * 3 + (a[1] - b[1]) ** 2 * 4 + (a[2] - b[2]) ** 2 * 2


def pack_palettes(tilesets, nbanks, weights=None):
    """Greedy bank packing. Tiles that fit no bank once all banks exist are assigned to the
    bank that represents them best; their missing colours are later remapped to the nearest
    colour in that bank (QUANTIZED counts them)."""
    weights = weights or [1] * len(tilesets)
    banks = []
    order = sorted(range(len(tilesets)), key=lambda i: -weights[i] * len(tilesets[i]))
    assign = [None] * len(tilesets)
    late = []
    for i in order:
        s = tilesets[i]
        best, bestadd = None, 99
        for bi, b in enumerate(banks):
            add = len(s - set(b))
            if len(b) + add <= 15 and add < bestadd:
                best, bestadd = bi, add
        if best is None:
            if len(banks) >= nbanks:
                late.append(i)
                continue
            banks.append([])
            best = len(banks) - 1
        for c in sorted(s):
            if c not in banks[best]:
                banks[best].append(c)
        assign[i] = best
    for i in late:
        s = tilesets[i]
        best, besterr = None, None
        for bi, b in enumerate(banks):
            err = sum(min(cdist(c, d) for d in b) for c in s if c not in b)
            if besterr is None or err < besterr:
                best, besterr = bi, err
        assign[i] = best
    return assign, banks, late


def flips(t):
    h = tuple(tuple(reversed(r)) for r in t)
    v = tuple(reversed(t))
    hv = tuple(tuple(reversed(r)) for r in reversed(t))
    return [(t, 0), (h, 1), (v, 2), (hv, 3)]


# ----------------------------------------------------------------------------------------
# Build metatiles, tiles and palettes
# ----------------------------------------------------------------------------------------
meta_imgs = []
meta_index = {}


def add_meta(img):
    key = tuple(tuple(r) for r in img)
    if key not in meta_index:
        meta_index[key] = len(meta_imgs)
        meta_imgs.append(img)
    return meta_index[key]


add_meta(blank(16, 16))  # meta 0 = transparent (overlay layer)
_cache = {}


def render_cached(x, y):
    t, g = TY[y][x], GR[y][x]
    key = (t, g, hsh(x, y) % 3,
           tuple((T_at(x + dx, y + dy), G_at(x + dx, y + dy)) for dx, dy in DIRS8))
    if t in ('CLIFF',):
        key = key + (x, y)
    r = _cache.get(key)
    if r is None:
        img, over = render(x, y)
        r = (add_meta(img), add_meta(over) if over is not None else 0)
        _cache[key] = r
    return r


map_meta = [[0] * MW for _ in range(MH)]
map_over = [[0] * MW for _ in range(MH)]
for y in range(MH):
    for x in range(MW):
        m, o = render_cached(x, y)
        map_meta[y][x] = m
        if o and y > 0:
            map_over[y - 1][x] = o

# depleted variants of every resource cell (stump / empty rock on that cell's own ground)
RES_ALT = []
for y in range(MH):
    for x in range(MW):
        t = TY[y][x]
        if t in TREES or t in ROCKS:
            TY[y][x] = 'STUMP' if t in TREES else 'ROCK_EMPTY'
            img, _ = render(x, y)
            TY[y][x] = t
            RES_ALT.append((y * MW + x, add_meta(img)))

tile_list = []
tile_lookup = {}
meta_entries = []
for img in meta_imgs:
    ents = []
    for t in cut8(img):
        if t in tile_lookup:
            ents.append(tile_lookup[t])
            continue
        i = len(tile_list)
        tile_list.append(t)
        for ft, f in flips(t):
            tile_lookup.setdefault(ft, (i, f))
        ents.append((i, 0))
    meta_entries.append(ents)

tile_colsets = [frozenset(c for r in t for c in r if c is not None) for t in tile_list]
WORLD_BANK0 = 2
tile_use = [0] * len(tile_list)
_meta_use = [0] * len(meta_imgs)
for y in range(MH):
    for x in range(MW):
        _meta_use[map_meta[y][x]] += 1
        _meta_use[map_over[y][x]] += 1
for mi, ents in enumerate(meta_entries):
    for (ti, f) in ents:
        tile_use[ti] += _meta_use[mi]
assign, wbanks, QUANTIZED = pack_palettes(tile_colsets, 14, tile_use)
idx_tiles = []
idx_lookup = {}
rgb_to_idx = []
for i, t in enumerate(tile_list):
    b = wbanks[assign[i]]
    it = tuple(tuple(0 if c is None else (b.index(c) if c in b else min(range(len(b)), key=lambda k: cdist(c, b[k]))) + 1
                     for c in r) for r in t)
    found = None
    for ft, f in flips(it):
        if ft in idx_lookup:
            found = (idx_lookup[ft], f)
            break
    if found is None:
        idx_lookup[it] = len(idx_tiles)
        idx_tiles.append(it)
        found = (len(idx_tiles) - 1, 0)
    rgb_to_idx.append(found)
assert all(v == 0 for r in idx_tiles[0] for v in r), 'tile 0 must be blank'


def world_entry(rgb_idx, flip):
    ti, f2 = rgb_to_idx[rgb_idx]
    f = flip ^ f2
    attr = ((f & 1) << 10) | (((f >> 1) & 1) << 11) | ((assign[rgb_idx] + WORLD_BANK0) << 12)
    return ti | (attr << 16)


meta_out = [[world_entry(i, f) for (i, f) in ents] for ents in meta_entries]

world_pal = [0] * (14 * 16)
water_slots = []
for bi, b in enumerate(wbanks):
    for ci, c in enumerate(b):
        world_pal[bi * 16 + ci + 1] = gba(c)
    if WC['W1'] in b and WC['W2'] in b:
        water_slots.append((bi + WORLD_BANK0) * 16 + b.index(WC['W1']) + 1)
        water_slots.append((bi + WORLD_BANK0) * 16 + b.index(WC['W2']) + 1)

# ----------------------------------------------------------------------------------------
# Tile-cache budget check: the most unique tiles any camera window (plus the column/row that
# is being replaced while scrolling) can reference. Must stay below the 575 cache slots.
# ----------------------------------------------------------------------------------------
CACHE_SLOTS = 575


def check_window_budget():
    cell_tiles = []
    for y in range(MH):
        row = []
        for x in range(MW):
            s = set(e & 0xFFFF for e in meta_out[map_meta[y][x]])
            s |= set(e & 0xFFFF for e in meta_out[map_over[y][x]])
            s.discard(0)
            row.append(s)
        cell_tiles.append(row)
    # extra variants: depleted cells may appear anywhere a resource is
    alt = {i: m for i, m in RES_ALT}
    WW, WH = 17, 12      # 31x21 tiles -> at most 16x11 cells, +1 for the scroll overlap
    worst = (0, 0, 0)
    from collections import Counter
    for y0 in range(0, MH - WH + 1):
        cnt = Counter()
        for y in range(y0, y0 + WH):
            for x in range(0, WW):
                cnt.update(cell_tiles[y][x])
        for x0 in range(0, MW - WW + 1):
            if x0:
                for y in range(y0, y0 + WH):
                    cnt.subtract(cell_tiles[y][x0 - 1])
                    cnt.update(cell_tiles[y][x0 + WW - 1])
                cnt += Counter()
            n = len(cnt)
            if n > worst[0]:
                worst = (n, x0, y0)
    return worst


WINDOW_WORST = check_window_budget()
print('palette: %d tiles quantized (%d map uses of %d)' % (len(QUANTIZED), sum(tile_use[i] for i in QUANTIZED), sum(tile_use)), file=sys.stderr)
print('world: %dx%d cells, %d metatiles, %d tiles, %d banks, %d resources, worst window %d tiles at cell %d,%d, %d reachable cells'
      % (MW, MH, len(meta_imgs), len(idx_tiles), len(wbanks), len(RES_ALT), WINDOW_WORST[0], WINDOW_WORST[1],
         WINDOW_WORST[2], REACHABLE), file=sys.stderr)
if WINDOW_WORST[0] > CACHE_SLOTS:
    raise SystemExit('tile cache overflow: a camera window needs %d tiles (max %d)' % (WINDOW_WORST[0], CACHE_SLOTS))
assert len(SPAWNS) <= 224, len(SPAWNS)
assert MW <= 255 and MH <= 255

# ----------------------------------------------------------------------------------------
# C emission
# ----------------------------------------------------------------------------------------


def emit_header(h):
    h.append('#define MAP_W %d' % MW)
    h.append('#define MAP_H %d' % MH)
    for n, i in T.items():
        h.append('#define T_%s %d' % (n, i))
    h.append('#define T_WALKABLE_MAX %d' % T['DOOR'])
    h.append('#define WORLD_TILE_COUNT %d' % len(idx_tiles))
    h.append('#define META_COUNT %d' % len(meta_out))
    h.append('#define RES_COUNT %d' % len(RES_ALT))
    h.append('#define WATER_SLOT_COUNT %d' % len(water_slots))
    h.append('#define SPAWN_COUNT %d' % len(SPAWNS))
    h.append('#define FISHSPOT_COUNT %d' % len(FISH_SPOTS))
    h.append('#define REGION_COUNT %d' % len(REGIONS))
    h.append('#define START_X %d' % START[0])
    h.append('#define START_Y %d' % START[1])
    h.append('''
typedef struct { u8 kind, x, y; } Spawn;
typedef struct { u8 kind, x, y; } FishSpotDef;
typedef struct { const char *name; u8 x0, y0, x1, y1; } Region;
extern const u32 world_tiles[WORLD_TILE_COUNT * 8];
extern const u32 meta_tiles[META_COUNT][4];   // low 16: tile id, high 16: flip/palette attrs
extern const u16 map_meta[MAP_W * MAP_H];
extern const u16 map_over[MAP_W * MAP_H];
extern const u8 map_type[MAP_W * MAP_H];
extern const u16 res_cell[RES_COUNT];         // sorted cell indices of trees/rocks
extern const u16 res_alt[RES_COUNT];          // their depleted metatile (stump / empty rock)
extern const u16 world_pal[14 * 16];
extern const u16 water_slots[];
extern const Spawn spawns[SPAWN_COUNT];
extern const FishSpotDef fish_spots[FISHSPOT_COUNT];
extern const Region regions[REGION_COUNT];
''')


def emit_source(c, arr, tile_words, npc_kinds):
    for k, _, _ in SPAWNS:
        assert k in npc_kinds, k
    wt = []
    for t in idx_tiles:
        wt += tile_words(t)
    c.append(arr('u32', 'world_tiles[WORLD_TILE_COUNT * 8]', wt, 8, '0x%08X'))
    c.append('const u32 meta_tiles[META_COUNT][4] = {')
    for m in meta_out:
        c.append('  {0x%08X,0x%08X,0x%08X,0x%08X},' % tuple(m))
    c.append('};')
    c.append(arr('u16', 'map_meta[MAP_W * MAP_H]', [map_meta[y][x] for y in range(MH) for x in range(MW)], 24, '%d'))
    c.append(arr('u16', 'map_over[MAP_W * MAP_H]', [map_over[y][x] for y in range(MH) for x in range(MW)], 32, '%d'))
    c.append(arr('u8', 'map_type[MAP_W * MAP_H]', [T[TY[y][x]] for y in range(MH) for x in range(MW)], 40, '%d'))
    c.append(arr('u16', 'res_cell[RES_COUNT]', [i for i, _ in RES_ALT], 24, '%d'))
    c.append(arr('u16', 'res_alt[RES_COUNT]', [m for _, m in RES_ALT], 24, '%d'))
    c.append(arr('u16', 'world_pal[14 * 16]', world_pal, 16))
    c.append(arr('u16', 'water_slots[]', water_slots or [0], 16, '%d'))
    c.append('const Spawn spawns[SPAWN_COUNT] = {')
    for k, x, y in SPAWNS:
        c.append('  {NPC_%s, %d, %d},' % (k, x, y))
    c.append('};')
    c.append('const FishSpotDef fish_spots[FISHSPOT_COUNT] = {')
    for k, x, y in FISH_SPOTS:
        c.append('  {%d, %d, %d},' % (0 if k == 'NET' else 1, x, y))
    c.append('};')
    c.append('const Region regions[REGION_COUNT] = {')
    for n, x0, y0, x1, y1 in REGIONS:
        c.append('  {"%s", %d, %d, %d, %d},' % (n.replace('"', '\\"'), x0, y0, x1, y1))
    c.append('};')


