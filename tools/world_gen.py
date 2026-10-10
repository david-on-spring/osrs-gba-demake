#!/usr/bin/env python3
"""World map generator: a recreation of the OSRS free-to-play mainland at full scale.

Scale: one map cell = one OSRS tile. The overworld covers OSRS x 2928..3343 and y 3136..3551
(416 x 416 cells); upper floors sit in rows below it. Everything is authored in real OSRS coordinates with drawing primitives
(polygons, polylines, rectangles, buildings, scatter), then rendered into 16x16 cell images,
cut into 8x8 tiles, deduplicated and palette-packed.

Runtime model (see src/gfx.c): the map lives in ROM; the world tilemap is streamed into a
VRAM tile cache of 575 slots as the camera moves, so the number of unique tiles in the whole
world is unbounded. check_window_budget() proves offline that no camera position ever needs
more than the cache holds.

Imported by gen_assets.py; exposes emit_header(h) / emit_source(c, arr, tile_words).
"""
import math
import os
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
# The cell grid holds the overworld (rows 0..415, 1 cell = 1 OSRS tile) and, below it, the
# upper floors (the castle's first and second floors; see ZONES). Authoring primitives work in
# real OSRS coordinates through the current context (origin, scale, cell offset and clip rect).
MW = 416
OW_W = OW_H = 416           # the overworld: cells 0..415 in both directions
ZONE_Y = 420                # first row of the upper floors
MH = 460


class Ctx:
    def __init__(self, OX, OY, S, CX0, CY0, X0, Y0, X1, Y1, excl=()):
        self.OX, self.OY, self.S, self.CX0, self.CY0 = OX, OY, S, CX0, CY0
        self.X0, self.Y0, self.X1, self.Y1 = X0, Y0, X1, Y1
        self.excl = list(excl)      # cell rects inside the clip rect that stay untouched (other towns)


OW = Ctx(2928, 3551, 1, 0, 0, 0, 0, OW_W - 1, OW_H - 1)
CTX = OW


def set_ctx(c):
    global CTX
    CTX = c


def cx(X):
    return CTX.CX0 + (X - CTX.OX) // CTX.S


def cy(Y):
    return CTX.CY0 + (CTX.OY - Y) // CTX.S


def fpt(X, Y):
    return (CTX.CX0 + (X - CTX.OX) / float(CTX.S), CTX.CY0 + (CTX.OY - Y) / float(CTX.S))


def XR():
    return range(CTX.X0, CTX.X1 + 1)


def YR():
    return range(CTX.Y0, CTX.Y1 + 1)


def wsc(w):
    """A width authored for the old 1:2 overworld, in cells of the current context."""
    return max(1, w * 2 // CTX.S)


WALK = ['GRASS', 'FLOWERS', 'PATH', 'COBBLE', 'FLOOR', 'WOODFLOOR', 'CARPET', 'BRIDGE', 'SAND',
        'DESERT', 'SWAMP', 'WILD', 'SNOW', 'DITCH', 'CHECKER', 'FERN', 'RUG', 'DOOR']
BLOCK = ['WATER', 'WALL', 'HWALL', 'WWALL', 'SWALL', 'FENCE', 'TREE', 'OAK', 'WILLOW', 'YEW',
         'DEADTREE', 'CACTUS', 'STUMP', 'COPPER', 'TIN', 'IRON', 'ROCK_EMPTY', 'BOULDER', 'BOOTH',
         'RANGE', 'COUNTER', 'BUSH', 'FOUNTAIN', 'CROPS', 'STALL', 'ALTAR', 'CLIFF', 'FIREPIT',
         'GRAVE', 'CLAY', 'HOPPER', 'FLOURBIN', 'WHEEL', 'COFFIN', 'CADAVA', 'CAULDRON', 'COMPOST',
         'LOCKDOOR', 'GRILL', 'HOLE', 'DRAIN', 'CHEST', 'TOLLGATE',
         # 1:1 zones: void between zones, edge/stair portals, castle and town furniture
         'VOID', 'STAIRS', 'TABLE', 'BED', 'STATUE', 'PEW', 'HEDGE', 'CRATE',
         'CHAIR', 'BOOKCASE', 'DRAWERS', 'FIREPLACE', 'BARREL', 'WARDROBE', 'POTPLANT', 'BIGSTALL', 'ANVIL',
         'FURNACE', 'SHELF', 'THRONE', 'WELL', 'SACKS', 'FLOWERBED', 'PALM', 'BENCH', 'SUNFLOWER', 'LOWWALL',
         'VFOUNTAIN']
TYPES = WALK + BLOCK
T = {n: i for i, n in enumerate(TYPES)}
WALKSET = set(WALK)
WALLS = {'WALL', 'HWALL', 'WWALL', 'SWALL'}
TREES = {'TREE', 'OAK', 'WILLOW', 'YEW', 'DEADTREE'}
ROCKS = {'COPPER', 'TIN', 'IRON', 'CLAY'}

TY = [['GRASS' if y < OW_H and x < OW_W else 'VOID' for x in range(MW)] for y in range(MH)]   # cell type
GR = [['GRASS' if y < OW_H and x < OW_W else 'VOID' for x in range(MW)] for y in range(MH)]   # ground under it
RES = [[False] * MW for _ in range(MH)]    # reserved: keep scatter away
ROAD = [[False] * MW for _ in range(MH)]   # cells drawn by road()

rng = random.Random(2007)


def inb(x, y):
    return 0 <= x < MW and 0 <= y < MH


def inc(x, y):
    """Inside the current authoring context."""
    return CTX.X0 <= x <= CTX.X1 and CTX.Y0 <= y <= CTX.Y1 and \
        not any(a <= x <= c and b <= y <= d for (a, b, c, d) in CTX.excl)


def setg(x, y, g):
    if inc(x, y):
        TY[y][x] = g
        GR[y][x] = g


def put(x, y, t):
    if inc(x, y):
        TY[y][x] = t


def reserve(x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if inc(x, y):
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
    for y in range(max(CTX.Y0, int(min(ys))), min(CTX.Y1 + 1, int(max(ys)) + 2)):
        yc = y + 0.5
        xs = []
        for i in range(len(P)):
            (x1, y1), (x2, y2) = P[i], P[(i + 1) % len(P)]
            if (y1 <= yc) != (y2 <= yc):
                xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(max(CTX.X0, int(math.ceil(xs[i] - 0.5))), min(CTX.X1 + 1, int(math.floor(xs[i + 1] - 0.5)) + 1)):
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
                    if c not in seen and inc(*c):
                        seen.add(c)
                        fn(*c)


def oellipse(X0, Y0, X1, Y1, fn):
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    rx, ry = (x1 - x0) / 2.0 + 0.5, (y1 - y0) / 2.0 + 0.5
    for y in range(max(y0, CTX.Y0), min(y1, CTX.Y1) + 1):
        for x in range(max(x0, CTX.X0), min(x1, CTX.X1) + 1):
            wob = 0.12 * math.sin(x * 1.3 + y * 0.7)
            if ((x - mx) / rx) ** 2 + ((y - my) / ry) ** 2 <= 1.0 + wob:
                fn(x, y)


# ----------------------------------------------------------------------------------------
# Higher-level authoring primitives


def road(pts, w=2, g='PATH'):
    w = wsc(w)
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
    w = wsc(w)
    opolyline(pts, w, lambda x, y: setg(x, y, 'WATER'))


ROOF = [[0] * MW for _ in range(MH)]     # roof group per cell (0 = open sky)
ROOFS = [None]      # group -> (style, cells); a group's roof hides while the player is under it
ROOF_OF = {'WALL': 'slate', 'HWALL': 'thatch', 'SWALL': 'flat', 'WWALL': 'flatwhite'}


def zc(X, Y):
    return (cx(X), cy(Y))


def zroof(style, cells):
    """Register a roof over a building footprint (OSRS tiles). The south-facing wall row is left
    open: it is the facade seen from the street."""
    g = len(ROOFS)
    cs = set(zc(*c) for c in cells)
    ROOFS.append((style, cs))
    for (x, y) in cs:
        ROOF[y][x] = g
    return g


def building(X0, Y0, X1, Y1, wall='WALL', floor='FLOOR', doors=(), margin=1, roof='auto'):
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    return cbuilding(x0, y0, x1, y1, wall, floor, doors, margin, roof)


def cbuilding(x0, y0, x1, y1, wall='WALL', floor='FLOOR', doors=(), margin=1, roof=None):
    """A walled rectangle (cells). roof: a roof style, 'auto' (by wall material) or None."""
    reserve(x0 - margin, y0 - margin, x1 + margin, y1 + margin)
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            edge = x in (x0, x1) or y in (y0, y1)
            TY[y][x] = wall if edge else floor
            GR[y][x] = floor
    r = (x0, y0, x1, y1)
    for d in doors:
        door(r, *d)
    if roof:
        g = len(ROOFS)
        cs = set((x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if inc(x, y))
        ROOFS.append((ROOF_OF.get(wall, 'slate') if roof == 'auto' else roof, cs))
        for (x, y) in cs:
            ROOF[y][x] = g
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
SHOPS = []          # (SHOP_ id, cell x, cell y): building centre, used to pick the shop by position
SHOP_OWNERS = set()  # NPC kinds that keep a specialty shop
SHOP_SKETCH = set()  # shops still as the countryside sketch placed them
CITIES = []          # the towns rebuilt at full detail, in the order they were laid (see city())


def shop(name, r, side, npc):
    """A shop plus its entry in the shop table. General stores get a counter with staff behind
    it (see service); a specialty shop has one keeper, behind a counter if the room is deep
    enough for staff, counter and customers, else just standing at the back wall."""
    SHOPS[:] = [sh for sh in SHOPS if sh[0] != name]      # a rebuilt town re-adds its shops
    SHOPS.append((name, (r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    (SHOP_SKETCH.discard if CITIES else SHOP_SKETCH.add)(name)
    if npc != 'SHOPKEEPER':
        SHOP_OWNERS.add(npc)
    if npc == 'SHOPKEEPER':
        service(r, side, 'COUNTER', npc)
        return
    x0, y0, x1, y1 = r
    ix0, iy0, ix1, iy1 = x0 + 1, y0 + 1, x1 - 1, y1 - 1
    depth = iy1 - iy0 + 1 if side in 'NS' else ix1 - ix0 + 1
    if side in 'NS':
        sx, sy, ox, oy = (ix0 + ix1) // 2, iy0 if side == 'N' else iy1, 0, 1 if side == 'N' else -1
    else:
        sx, sy, ox, oy = ix0 if side == 'W' else ix1, (iy0 + iy1) // 2, 1 if side == 'W' else -1, 0
    if depth >= 3:
        TY[sy + oy][sx + ox] = 'COUNTER'
    SPAWNS.append((npc, sx, sy))


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
    for y in range(max(CTX.Y0, y0), min(CTX.Y1 + 1, y1 + 1)):
        for x in range(max(CTX.X0, x0), min(CTX.X1 + 1, x1 + 1)):
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


def forest(X0, Y0, X1, Y1, dens, kinds, on=('GRASS',), seed=1, scale=None):
    """Clumped woodland: tree probability follows smooth noise so trees form groves and glades.
    scale (cells) defaults to groves sized as on the old 1:2 map."""
    r = random.Random(seed)
    fine = 2.0
    if scale is None:
        scale, fine = 9.0 / CTX.S, 4.0 / CTX.S
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    tot = sum(w for _, w in kinds)
    for y in range(max(CTX.Y0, y0), min(CTX.Y1 + 1, y1 + 1)):
        for x in range(max(CTX.X0, x0), min(CTX.X1 + 1, x1 + 1)):
            if TY[y][x] not in on or GR[y][x] != TY[y][x] or RES[y][x]:
                continue
            n = 0.65 * vnoise(x / scale, y / scale, seed) + 0.35 * vnoise(x / fine, y / fine, seed + 7)
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
    for rad in range(0, 10):
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

def base_terrain():
    """Large-scale terrain shared by the overworld and the 1:1 zones: wilderness, desert,
    swamp, Ice Mountain, sea, the River Lum and the road network."""
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
    for y in YR():
        for x in XR():
            if TY[y][x] in ('GRASS',) and any(inb(x + dx, y + dy) and TY[y + dy][x + dx] == 'WATER' and y + dy > cy(3260)
                                              for dx in (-1, 0, 1) for dy in (-1, 0, 1)) and y > cy(3245) and hsh(x, y) % 3:
                setg(x, y, 'SAND')

    # --- River Lum -----------------------------------------------------------------------------
    river([(3134, 3552), (3132, 3520), (3127, 3500), (3116, 3472), (3108, 3446), (3107, 3424),
           (3112, 3404), (3128, 3386), (3145, 3369), (3162, 3352), (3186, 3336), (3210, 3321),
           (3234, 3306), (3238, 3280), (3236, 3262), (3239, 3240), (3246, 3225), (3256, 3214),
           (3260, 3198), (3257, 3182), (3255, 3166), (3254, 3140)], 3)
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
    road([(3229, 3226), (3224, 3233), (3219, 3243), (3216, 3252), (3215, 3262), (3205, 3268), (3195, 3282), (3175, 3298),
          (3166, 3301)], 1)
    # Lumbridge -> Draynor
    road([(3219, 3244), (3196, 3245), (3170, 3243), (3140, 3246), (3112, 3248), (3080, 3249)], 2)
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


base_terrain()

# ========================================================================================
# LUMBRIDGE (first sketch): the hand-traced 1:1 town below replaces it. The sketch still names
# a few positions (the toll gate) and gameplay the town re-lands (see migrate_ow_content).
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
# bank (north side of the keep)
for x in range(kx0 + 2, kx0 + 6):
    put(x, ky0 + 2, 'BOOTH')
# throne room carpet (Duke Horacio stands here)
for y in range(ky0 + 4, ky0 + 8):
    for x in range(kx1 - 3, kx1):
        setg(x, y, 'CARPET')
DUKE = (kx1 - 2, ky0 + 5)
HANS = (kx1 + 2, cy(3222))

# general store
GS_LUMB = building(3209, 3242, 3219, 3252, 'HWALL', 'WOODFLOOR', [('S', 3214)])
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
for y in YR():
    for x in XR():
        d = math.hypot(x + 0.5 - wcx, y + 0.5 - wcy)
        if d <= 10.4:
            TY[y][x] = 'WALL' if d > 9.4 else 'FLOOR'
            GR[y][x] = 'FLOOR'
reserve(int(wcx) - 13, int(wcy) - 13, int(wcx) + 13, int(wcy) + 13)
TY[int(wcy) - 10][int(wcx)] = 'DOOR'
setg(int(wcx), int(wcy) - 11, 'PATH')
zroof('tower', [(X, Y) for X in range(3098, 3121) for Y in range(3150, 3173)
                if TY[cy(Y)][cx(X)] in ('WALL', 'FLOOR', 'DOOR') and GR[cy(Y)][cx(X)] == 'FLOOR'])
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
shop('BETTY', building(3046, 3250, 3054, 3258, 'HWALL', 'WOODFLOOR', [('S', 3050)]), 'N', 'BETTY')
building(3040, 3232, 3050, 3240, 'HWALL', 'WOODFLOOR', [('N', 3044)])       # Rusty Anchor
shop('GERRANT', building(3034, 3206, 3042, 3214, 'HWALL', 'WOODFLOOR', [('W', 3210)]), 'E', 'GERRANT')
building(3046, 3214, 3054, 3222, 'WALL', 'FLOOR', [('N', 3050)])            # jail
# Rimmington
building(2950, 3204, 2958, 3211, 'HWALL', 'WOODFLOOR', [('N', 2954)])
building(2962, 3208, 2970, 3215, 'HWALL', 'WOODFLOOR', [('W', 3211)])
shop('RIMMINGTON', building(2946, 3216, 2954, 3223, 'HWALL', 'WOODFLOOR', [('E', 3220)]), 'W', 'SHOPKEEPER')
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
shop('FALADOR', GS_FAL, 'N', 'SHOPKEEPER')
shop('CASSIE', building(2970, 3380, 2980, 3390, 'HWALL', 'WOODFLOOR', [('W', 3385)]), 'E', 'CASSIE')
building(2943, 3350, 2951, 3358, 'HWALL', 'WOODFLOOR', [('E', 3354)])       # Herquin's gems
building(2970, 3360, 2980, 3368, 'WALL', 'FLOOR', [('S', 2975)])            # furnace
building(2954, 3373, 2963, 3380, 'HWALL', 'WOODFLOOR', [('S', 2958)])       # Rising Sun Inn
PARTY = building(3036, 3368, 3056, 3386, 'WALL', 'WOODFLOOR', [('S', 3046)])
building(3016, 3333, 3024, 3343, 'WALL', 'FLOOR', [('W', 3338)])            # Mining Guild
building(3032, 3330, 3042, 3340, 'HWALL', 'WOODFLOOR', [('N', 3037)])
building(2992, 3328, 3000, 3338, 'HWALL', 'WOODFLOOR', [('E', 3333)])
building(3042, 3348, 3052, 3358, 'HWALL', 'WOODFLOOR', [('W', 3353)])
shop('WAYNE', building(2968, 3312, 2978, 3320, 'HWALL', 'WOODFLOOR', [('N', 2973)]), 'S', 'WAYNE')
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
shop('NURMOF', building(3028, 3448, 3036, 3455, 'WALL', 'FLOOR', [('S', 3032)]), 'N', 'NURMOF')   # Dwarven Mine entrance
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
shop('PEKSA', building(3068, 3426, 3075, 3432, 'HWALL', 'WOODFLOOR', [('E', 3429)]), 'W', 'PEKSA')
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
shop('EDGEVILLE', GS_EDGE, 'N', 'SHOPKEEPER')
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
shop('VARROCK', GS_VAR, 'S', 'SHOPKEEPER')
SWORD = building(3198, 3392, 3207, 3398, 'HWALL', 'WOODFLOOR', [('N', 3202)])
shop('SWORDSHOP', SWORD, 'S', 'SWORD_KEEPER')
AUBURY = building(3250, 3402, 3258, 3408, 'HWALL', 'WOODFLOOR', [('W', 3405)])
HORVIK = building(3226, 3434, 3234, 3442, 'HWALL', 'WOODFLOOR', [('S', 3230)])
shop('HORVIK', HORVIK, 'N', 'HORVIK')
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
for y in YR():
    for x in XR():
        d = max(abs(x + 0.5 - gcx), abs(y + 0.5 - gcy)) * 0.8 + 0.2 * (abs(x + 0.5 - gcx) + abs(y + 0.5 - gcy))
        if d <= 10:
            TY[y][x] = 'WALL' if d > 9.1 else 'FLOOR'
            GR[y][x] = 'FLOOR'
reserve(int(gcx) - 12, int(gcy) - 12, int(gcx) + 12, int(gcy) + 12)
for (dx, dy) in ((0, -9), (0, 9), (-9, 0), (9, 0)):
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
    put(int(ccx + 6.4 * math.cos(a)), int(ccy + 6.4 * math.sin(a)), 'BOULDER')
reserve(int(ccx) - 8, int(ccy) - 8, int(ccx) + 8, int(ccy) + 8)

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
shop('ALKHARID', GS_AK, 'E', 'SHOPKEEPER')
shop('ZEKE', building(3284, 3186, 3291, 3193, 'SWALL', 'FLOOR', [('S', 3287)]), 'N', 'ZEKE')
shop('LOUIE', building(3310, 3162, 3318, 3170, 'SWALL', 'FLOOR', [('W', 3166)]), 'E', 'LOUIE')
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
ARENA = building(3314, 3206, 3343, 3262, 'SWALL', 'DESERT', [('W', 3232)], margin=0, roof=None)
for (X0, Y0, X1, Y1) in ((3320, 3212, 3334, 3226), (3320, 3238, 3334, 3252)):
    building(X0, Y0, X1, Y1, 'SWALL', 'SAND', [('W', (Y0 + Y1) // 2)], roof=None)
for y in YR():                              # desert sand inside
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
for y in YR():
    for x in XR():
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
    for rad in range(0, 16):
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
# altar on the Wizards' Tower floor (the basement altar in OSRS)
qobj('ALTAR', int(wcx) + 6, int(wcy), 'TOWER_ALTAR')
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
# TOWNS at full detail, and the castle's upper floors
# ========================================================================================
# A town is rebuilt in place over the countryside sketch above: city() clears its rectangle plus a
# scenery margin (keeping clear of the towns laid before it) and the town's script lays it out
# again, by hand (Lumbridge, below) or from a trace (tools/zones/*.py). The castle floors sit
# below the overworld and are joined to it by the spiral staircases (quest scripts 'obj STAIRS'
# in tools/quests.txt).
LUM_R = (3196, 3188, 3261, 3254)        # Lumbridge proper (OSRS X0, Y0, X1, Y1); Al Kharid starts at 3262
ZF1 = Ctx(3195, 3236, 1, 0, ZONE_Y, 0, ZONE_Y, 31, ZONE_Y + 36)          # OSRS 3195..3226 x 3200..3236
ZF2 = Ctx(3195, 3236, 1, 36, ZONE_Y, 36, ZONE_Y, 67, ZONE_Y + 36)
# (name, context, palette set, plane). The camera stays inside the player's zone. The floors use
# Lumbridge's palette set (filled in by assign_palette_sets()).
ZONES = [('OVERWORLD', OW, 0, 0), ('CASTLE_1', ZF1, None, 1), ('CASTLE_2', ZF2, None, 2)]
LINKS = {}          # stair cell -> [dest cells] (for the reachability checks)
VIEW = {}           # upper-floor cell -> ground-floor cell it shows (looking down from the castle)


class City:
    """A town at full detail. R is the town proper in OSRS tiles; ctx clips the town's script to R
    plus a scenery margin."""

    def __init__(self, name, R, ctx):
        self.name, self.R, self.ctx = name, R, ctx
        self.pset = None


def town_ctx(X0, Y0, X1, Y1, excl=()):
    """Overworld context clipped to OSRS (X0, Y0)..(X1, Y1)."""
    return Ctx(OW.OX, OW.OY, 1, 0, 0, max(0, cx(X0)), max(0, cy(Y1)), min(OW_W - 1, cx(X1)), min(OW_H - 1, cy(Y0)), excl)


def city(name, R, margin=9, ctx=None, keep=()):
    """Register a town and clear its area of the countryside sketch: ground, scenery, roads and
    roofs. Earlier towns' rectangles, and the OSRS rects in keep (countryside landmarks the
    margin would cut in two), are kept out of its margin."""
    global SKETCH_SPAWNS, SKETCH_QPOS
    if not CITIES:                      # what the countryside sketch made (see migrate_ow_content)
        SKETCH_SPAWNS = set(SPAWNS)
        SKETCH_QPOS = dict(QPOS)
    set_ctx(OW)
    excl = [orect(*c.R) for c in CITIES] + [orect(*k) for k in keep]
    if ctx is None:
        ctx = town_ctx(R[0] - margin, R[1] - margin, R[2] + margin, R[3] + margin, excl)
    else:
        ctx.excl = excl
    c = City(name, R, ctx)
    CITIES.append(c)
    set_ctx(ctx)
    for y in YR():
        for x in XR():
            if inc(x, y):
                TY[y][x] = GR[y][x] = 'GRASS'
                RES[y][x] = ROAD[y][x] = False
                g = ROOF[y][x]
                if g:
                    ROOFS[g][1].discard((x, y))
                    ROOF[y][x] = 0
    return c


def town_at(x, y):
    """The town whose rectangle holds overworld cell (x, y), or None."""
    if not (0 <= x < OW_W and 0 <= y < OW_H):
        return None
    X, Y = OW.OX + x, OW.OY - y
    for c in CITIES:
        if c.R[0] <= X <= c.R[2] and c.R[1] <= Y <= c.R[3]:
            return c
    return None


def owner_of(x, y):
    """The town that laid overworld cell (x, y) (its rectangle or margin), or None."""
    for c in reversed(CITIES):
        z = c.ctx
        if z.X0 <= x <= z.X1 and z.Y0 <= y <= z.Y1 and not any(a <= x <= cc and b <= y <= d for (a, b, cc, d) in z.excl):
            return c
    return None


def zset(X, Y, t, g=None):
    x, y = zc(X, Y)
    if inc(x, y):
        TY[y][x] = t
        if g:
            GR[y][x] = g


def zfill(X0, Y0, X1, Y1, g):
    for Y in range(min(Y0, Y1), max(Y0, Y1) + 1):
        for X in range(min(X0, X1), max(X0, X1) + 1):
            zset(X, Y, g, g)


def zput(X, Y, t):
    zset(X, Y, t)


def zwalls(interior, wall, floor):
    """Floor every cell of 'interior' (OSRS tiles) and wall every tile around it."""
    inside = set(interior)
    for (X, Y) in inside:
        zset(X, Y, floor, floor)
    for (X, Y) in inside:
        for dX in (-1, 0, 1):
            for dY in (-1, 0, 1):
                n = (X + dX, Y + dY)
                if n not in inside:
                    zset(n[0], n[1], wall, floor)
    return inside


def tiles(X0, Y0, X1, Y1):
    return [(X, Y) for X in range(X0, X1 + 1) for Y in range(Y0, Y1 + 1)]


def zbuild(X0, Y0, X1, Y1, wall, floor, doors=(), roof=None, outside='PATH'):
    inside = zwalls(tiles(X0 + 1, Y0 + 1, X1 - 1, Y1 - 1), wall, floor)
    for (X, Y) in doors:
        zset(X, Y, 'DOOR')
        for (dX, dY) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (X + dX, Y + dY)
            if n in inside or not (X0 <= n[0] <= X1 and Y0 <= n[1] <= Y1):
                x, y = zc(*n)
                if inc(x, y) and TY[y][x] not in WALKSET:
                    setg(x, y, outside)
    reserve(*orect(X0 - 1, Y0 - 1, X1 + 1, Y1 + 1))
    if roof:
        zroof(roof, tiles(X0, Y0, X1, Y1))
    return (X0, Y0, X1, Y1)


def zline(pts, w, t, g=None, keep=()):
    def f(x, y):
        if TY[y][x] in keep:
            return
        TY[y][x] = t
        if g:
            GR[y][x] = g
    opolyline(pts, w, f)


# ---- keep floor plans (cells are OSRS tiles; walls take a whole tile) ------------------------
def castle_towers():
    """NW and SW stair towers (shared by all three floors). Returns the stair cells."""
    stairs = []
    for (Yb, Ys) in ((3227, 3229), (3204, 3205)):          # tower bottom row, first stair row
        for X in range(3202, 3207):
            for Y in range(Yb, Yb + 5):
                zset(X, Y, 'WALL', 'FLOOR')
        for X in range(3203, 3206):
            for Y in range(Yb + 1, Yb + 4):
                zset(X, Y, 'FLOOR', 'FLOOR')
        for X in (3203, 3204):
            for Y in (Ys, Ys + 1):
                zset(X, Y, 'STAIRS', 'FLOOR')
                stairs.append(zc(X, Y))
    return stairs


def keep_ground():
    inner = tiles(3205, 3208, 3216, 3227)
    castle_towers()
    zwalls(inner, 'WALL', 'FLOOR')
    for (X, Y) in ((3205, 3228), (3204, 3228), (3205, 3207), (3204, 3207)):   # tower doorways
        zset(X, Y, 'FLOOR', 'FLOOR')
    # north corridor / dining hall / kitchen / south corridor, west of the great hall
    for X in range(3205, 3212):
        zset(X, 3226, 'WALL', 'FLOOR')
        zset(X, 3217, 'WALL', 'FLOOR')
        zset(X, 3210, 'WALL', 'FLOOR')
    for Y in range(3210, 3227):
        zset(3212, Y, 'WALL', 'FLOOR')
    zset(3207, 3217, 'DOOR')                     # dining hall <-> kitchen
    zset(3212, 3221, 'DOOR')                     # dining hall <-> great hall
    zset(3212, 3222, 'DOOR')
    zfill(3213, 3210, 3216, 3226, 'CHECKER')     # the chequered great hall
    for Y in (3218, 3219):
        zset(3217, Y, 'DOOR')                    # front doors onto the courtyard
    zset(3204, 3214, 'DOOR')                     # kitchen back door
    zfill(3202, 3214, 3203, 3214, 'PATH')
    for X in range(3207, 3211):                  # the long dining table
        for Y in (3221, 3222):
            zset(X, Y, 'TABLE')
    zset(3206, 3216, 'RANGE')
    zset(3211, 3216, 'CRATE')
    zset(3205, 3211, 'CRATE')
    zfill(3213, 3218, 3216, 3219, 'CARPET')      # runner from the doors


def keep_first():
    inner = tiles(3205, 3208, 3216, 3227)
    castle_towers()
    zwalls(inner, 'WALL', 'FLOOR')
    for (X, Y) in ((3205, 3228), (3204, 3228), (3205, 3207), (3204, 3207)):
        zset(X, Y, 'FLOOR', 'FLOOR')
    zwalls(tiles(3209, 3218, 3213, 3225), 'WALL', 'CARPET')     # Duke Horacio's bedroom
    zwalls(tiles(3209, 3211, 3213, 3216), 'WALL', 'WOODFLOOR')  # spinning wheel room
    zset(3208, 3222, 'DOOR')
    zset(3208, 3214, 'DOOR')
    zset(3212, 3225, 'BED')
    zset(3213, 3225, 'BED')
    zset(3209, 3218, 'TABLE')


def keep_second():
    inner = tiles(3205, 3208, 3207, 3227) + tiles(3208, 3212, 3213, 3225)
    castle_towers()
    zwalls(inner, 'WALL', 'FLOOR')
    for (X, Y) in ((3205, 3228), (3204, 3228), (3205, 3207), (3204, 3207)):
        zset(X, Y, 'FLOOR', 'FLOOR')
    # the bank: tellers inside an enclosure in the middle of the room, booths on its west face
    for X in range(3207, 3212):
        zset(X, 3223, 'WALL', 'FLOOR')
        zset(X, 3214, 'WALL', 'FLOOR')
    for Y in range(3214, 3224):
        zset(3211, Y, 'WALL', 'FLOOR')
    for Y in range(3215, 3223):
        zset(3207, Y, 'BOOTH' if Y in (3216, 3217, 3220, 3221) else 'WALL', 'FLOOR')
    zfill(3208, 3215, 3210, 3222, 'CARPET')


# ---- Lumbridge ground floor -------------------------------------------------------------
ZL0 = town_ctx(3187, 3180, 3272, 3263)      # Lumbridge and its scenery margin
LUMBRIDGE = city('LUMBRIDGE', LUM_R, ctx=ZL0)
# swamp to the south (same outline as the overworld), lakes and the River Lum at full scale
opoly([(3133, 3140), (3133, 3203), (3145, 3216), (3190, 3214), (3197, 3200), (3214, 3199), (3236, 3192),
       (3247, 3189), (3256, 3188), (3256, 3140)], lambda x, y: setg(x, y, 'SWAMP'))
oellipse(3209, 3176, 3222, 3185, lambda x, y: setg(x, y, 'WATER'))
RIVER_L = [(3235, 3268), (3236, 3260), (3238, 3250), (3239, 3240), (3242, 3232), (3246, 3226), (3251, 3220),
           (3256, 3214), (3259, 3206), (3260, 3198), (3258, 3190), (3255, 3180)]
opolyline(RIVER_L, 7, lambda x, y: setg(x, y, 'WATER'))
opolyline(RIVER_L[4:8], 9, lambda x, y: setg(x, y, 'WATER'))
# roads: grey Lumbridge paths
road([(3227, 3218), (3236, 3218), (3237, 3224)], 1)                               # castle gate -> bridge road
road([(3229, 3225), (3256, 3225)], 1)                                             # over the bridge
road([(3256, 3225), (3259, 3231), (3260, 3242), (3257, 3250), (3252, 3256), (3251, 3266)], 1)   # to Varrock
road([(3259, 3227), (3268, 3227)], 1)                                             # to the toll gate
road([(3236, 3218), (3235, 3201), (3238, 3199), (3252, 3199)], 1)                 # south past Bob's
road([(3229, 3226), (3224, 3233), (3219, 3243), (3216, 3252), (3215, 3266)], 1)   # north road
road([(3219, 3244), (3205, 3244), (3196, 3245), (3186, 3246)], 1)                 # west to Draynor
road([(3237, 3210), (3236, 3210)], 1)                                             # church door
road([(3201, 3217), (3195, 3217), (3186, 3220)], 1)                               # west gate track
# the castle: curtain wall around the courtyard, gate towers, keep
CURTAIN = [(3201, 3204), (3201, 3232), (3204, 3235), (3213, 3235), (3216, 3232), (3223, 3232), (3227, 3228),
           (3227, 3208), (3223, 3204), (3216, 3204), (3213, 3201), (3204, 3201)]
opoly(CURTAIN, lambda x, y: (setg(x, y, 'GRASS'), RES[y].__setitem__(x, True)))
zline(CURTAIN + [CURTAIN[0]], 1, 'WALL', 'GRASS')
for Y in (3217, 3218):
    zset(3201, Y, 'PATH', 'PATH')                                                 # west gate
for Y in (3218, 3219):
    zset(3227, Y, 'PATH', 'PATH')                                                 # main gate
for (Y0, Y1, Yd) in ((3221, 3225, 3223), (3211, 3215, 3214)):                     # gate towers, doors on the courtyard
    for X in range(3226, 3231):
        for Y in range(Y0, Y1 + 1):
            if (X, Y) not in ((3226, Y0), (3230, Y0), (3226, Y1), (3230, Y1)):
                zset(X, Y, 'WALL', 'FLOOR')
    zfill(3227, Y0 + 1, 3229, Y1 - 1, 'FLOOR')
    zset(3226, Yd, 'DOOR', 'FLOOR')
    zroof('tower', [(X, Y) for X in range(3226, 3231) for Y in range(Y0, Y1 + 1)])
# courtyard: cross paths, fountains, hedges and statues
zfill(3221, 3208, 3222, 3229, 'PATH')
zfill(3218, 3218, 3226, 3219, 'PATH')
zfill(3213, 3218, 3217, 3219, 'PATH')
for (X, Y) in ((3221, 3226), (3221, 3210)):
    zfill(X - 1, Y - 1, X + 2, Y + 2, 'PATH')
    for dX in (0, 1):
        for dY in (0, 1):
            zput(X + dX, Y + dY, 'FOUNTAIN')
for X in range(3218, 3221):
    zput(X, 3221, 'HEDGE')
    zput(X, 3216, 'HEDGE')
zput(3224, 3222, 'STATUE')
zput(3224, 3215, 'STATUE')
for (X, Y) in ((3218, 3230), (3215, 3233), (3225, 3206), (3218, 3205), (3226, 3230)):
    zput(X, Y, 'FLOWERS')
set_ctx(ZL0)
keep_ground()
KEEP_CELLS = [(X, Y) for X in range(3202, 3218) for Y in range(3204, 3232)
              if TY[cy(Y)][cx(X)] not in ('GRASS', 'FLOWERS', 'PATH')]
zroof('keep', KEEP_CELLS)
STAIRS_L0 = {'N': zc(3203, 3230), 'S': zc(3203, 3206)}     # top-left cell of each 2x2 staircase
LUMB_COOK = zc(3208, 3213)
HANS = zc(3219, 3223)
START = zc(3222, 3218)
# town buildings
GS_LUMB = zbuild(3208, 3241, 3215, 3251, 'WALL', 'WOODFLOOR', [(3215, 3245)], roof='red')
for (X, Y) in ((3208, 3241), (3208, 3251)):
    zset(X, Y, 'GRASS', 'GRASS')
BOB = zbuild(3227, 3200, 3233, 3205, 'WALL', 'WOODFLOOR', [(3233, 3203)], roof='red')
zbuild(3230, 3193, 3237, 3198, 'WALL', 'WOODFLOOR', [(3235, 3198)], roof='red')
zbuild(3226, 3235, 3233, 3242, 'WALL', 'WOODFLOOR', [(3226, 3240), (3230, 3235)], roof='red')
zbuild(3221, 3251, 3230, 3257, 'WALL', 'FLOOR', [(3225, 3251), (3226, 3251), (3221, 3254)], roof='slate')
zbuild(3243, 3243, 3250, 3248, 'HWALL', 'WOODFLOOR', [(3246, 3243), (3250, 3245), (3249, 3248)], roof='thatch')   # goblin house
zbuild(3191, 3229, 3194, 3232, 'WALL', 'WOODFLOOR', [], roof='red')                         # closed in OSRS
# church and its graveyard; the graveyard's south gate opens onto the swamp
CHURCH_L = zbuild(3239, 3203, 3249, 3215, 'WALL', 'FLOOR', [(3239, 3210), (3243, 3215), (3244, 3215)], roof='slate')
zput(3248, 3210, 'ALTAR')
for Y in (3205, 3207, 3212):
    for X in range(3242, 3246):
        zput(X, Y, 'PEW')
zfill(3240, 3210, 3247, 3210, 'CARPET')
AERECK = zc(3246, 3210)
GRAVEYARD = (3237, 3189, 3254, 3201)
zline([(3237, 3201), (3237, 3189), (3254, 3189), (3254, 3201), (3250, 3201)], 1, 'FENCE', keep=('WATER',))
zline([(3237, 3201), (3238, 3201)], 1, 'FENCE')
fill_cells(cx(3238), cy(3200), cx(3253), cy(3190), 'GRASS')
for X in (3243, 3244):
    zset(X, 3189, 'PATH', 'PATH')                                                 # south gate (to the swamp)
for X in (3239, 3249):
    zset(X, 3201, 'PATH', 'PATH')                                                 # north gates
road([(3243, 3189), (3243, 3196), (3240, 3199)], 1)
road([(3243, 3184), (3243, 3189)], 1)
SHED = zbuild(3247, 3190, 3253, 3196, 'WALL', 'FLOOR', [(3247, 3193)], roof='slate')    # the ghost's crypt
for (X, Y) in ((3239, 3197), (3241, 3197), (3239, 3193), (3241, 3193), (3245, 3195), (3245, 3192)):
    zput(X, Y, 'GRAVE')
# fences: Al Kharid border, cow field, sheep pen (scenery beyond the walkable town)
zline([(3267, 3188), (3267, 3268)], 1, 'FENCE', keep=('PATH',))
zset(3267, 3226, 'TOLLGATE')
zset(3267, 3227, 'TOLLGATE')
zline([(3253, 3255), (3264, 3255), (3264, 3268)], 1, 'FENCE', keep=('PATH', 'WATER'))
zline([(3194, 3256), (3214, 3256), (3214, 3268)], 1, 'FENCE', keep=('PATH',))
# a clearing west of the castle with one tree (the skills test chops and lights a fire here)
fill_cells(cx(3197), cy(3224), cx(3200), cy(3220), 'GRASS')
reserve(cx(3197), cy(3225), cx(3200), cy(3219))
zput(3197, 3222, 'TREE')
# woods, scattered trees, the goblin plain's dead trees
forest(3187, 3200, 3199, 3263, 0.55, [('TREE', 8), ('OAK', 2)], seed=181, scale=3.0)
forest(3200, 3236, 3236, 3263, 0.12, [('TREE', 6), ('OAK', 1)], seed=182, scale=2.5)
forest(3244, 3228, 3266, 3254, 0.12, [('TREE', 3), ('DEADTREE', 2)], seed=183, scale=2.5)
forest(3196, 3180, 3236, 3200, 0.25, [('DEADTREE', 3), ('TREE', 1), ('BUSH', 1)], on=('SWAMP',), seed=184, scale=2.5)
forest(3228, 3180, 3272, 3263, 0.10, [('TREE', 2), ('BUSH', 1)], seed=185, scale=2.5)
scatter(3187, 3180, 3272, 3263, 0.025, [('FLOWERS', 4), ('BUSH', 1)], seed=186)
for y in YR():
    for x in XR():
        if TY[y][x] in TREES and ROOF[y - 1][x]:
            setg(x, y, 'GRASS')     # no canopy over a roof

# ---- castle first and second floors ---------------------------------------------------------
for (ctx, plan) in ((ZF1, keep_first), (ZF2, keep_second)):
    set_ctx(ctx)
    plan()
    for y in YR():
        for x in XR():
            if TY[y][x] == 'VOID':
                X, Y = ctx.OX + (x - ctx.CX0), ctx.OY - (y - ctx.CY0)
                VIEW[(x, y)] = (ZL0.CX0 + X - ZL0.OX, ZL0.CY0 + ZL0.OY - Y)
set_ctx(ZF1)
DUKE = zc(3211, 3222)
STAIRS_F1 = {'N': zc(3203, 3230), 'S': zc(3203, 3206)}
set_ctx(ZF2)
STAIRS_F2 = {'N': zc(3203, 3230), 'S': zc(3203, 3206)}

# named cells for the staircase scripts: the stair's corner and where the player arrives
for (fl, st) in ((0, STAIRS_L0), (1, STAIRS_F1), (2, STAIRS_F2)):
    for k, (x, y) in st.items():
        QPOS['STAIRS_%s%d' % (k, fl)] = (x, y)
        QPOS['ARRIVE_%s%d' % (k, fl)] = (x + 2, y + 1)
        QUEST_OBJS.append(('STAIRS', x + 1, y + 1))
for k in 'NS':
    for (a, b) in ((0, 1), (1, 2), (1, 0), (2, 1)):
        sa = (STAIRS_L0, STAIRS_F1, STAIRS_F2)[a][k]
        for dx in (0, 1):
            for dy in (0, 1):
                LINKS.setdefault((sa[0] + dx, sa[1] + dy), []).append(QPOS['ARRIVE_%s%d' % (k, b)])


def walk_ok(x, y):
    return inb(x, y) and TY[y][x] in WALKSET and TY[y][x] != 'DOOR'


def nearest_walk(x, y, ok, rmax=10):
    for r in range(rmax + 1):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) == r and ok(x + dx, y + dy):
                    return (x + dx, y + dy)
    return None


# ---- towns traced from the OSRS map (tools/zones/*.ref, see tools/ref_extract.py) ------------
REF_GROUND = {'g': 'GRASS', 'p': 'PATH', 'c': 'COBBLE', 'f': 'FLOOR', 'w': 'WOODFLOOR', 'k': 'CHECKER', '~': 'WATER',
              's': 'SAND', 'm': 'SWAMP', '#': 'WALL', '?': 'GRASS'}


def load_ref(name):
    r = {'G': [], 'N': [], 'W': [], 'O': [], 'X': []}
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zones', name + '.ref')):
        if line.startswith('ref '):
            r['rect'] = tuple(int(v) for v in line.split()[1:5])
        elif line[:2] in ('G ', 'N ', 'W ', 'O ', 'X '):
            r[line[0]].append(line[2:].rstrip('\n'))
    return r


def ensure_door(tiles, force=False):
    """Give a building (OSRS tiles) a door if none of its doors joins its floor to the outside.
    Prefers the middle of a wall run on the south side, facing paving or a path."""
    T_ = lambda X, Y: TY[cy(Y)][cx(X)] if inc(cx(X), cy(Y)) else 'VOID'
    inside = lambda X, Y: (X, Y) in tiles
    good = lambda X, Y: T_(X, Y) in WALKSET and T_(X, Y) != 'DOOR' and not inside(X, Y)
    for (X, Y) in ([] if force else tiles):
        # an open gap in the wall (floor straight onto the street) is the entrance, as in OSRS
        if T_(X, Y) in WALKSET and T_(X, Y) != 'DOOR' and any(good(X + u, Y + v) and (X + u, Y + v) not in tiles
                                                                for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            return
        if T_(X, Y) == 'DOOR':
            for (u, v) in ((0, 1), (1, 0)):
                a, b = (X - u, Y - v), (X + u, Y + v)
                if (inside(*a) and T_(*a) in WALKSET and good(*b)) or (inside(*b) and T_(*b) in WALKSET and good(*a)):
                    return
    best = None
    for (X, Y) in tiles:
        if T_(X, Y) not in WALLS:
            continue
        for (u, v, pref) in ((0, -1, 0), (1, 0, 1), (-1, 0, 1), (0, 1, 2)):     # outward: south first
            o, i = (X + u, Y + v), (X - u, Y - v)
            if good(*o) and inside(*i) and T_(*i) in WALKSET and T_(*i) != 'DOOR':
                side = [(X + v, Y + u), (X - v, Y - u)]
                if not all(T_(*q) in WALLS for q in side):
                    continue
                score = (pref, 0 if T_(*o) in ('COBBLE', 'PATH') else 1, hsh(X, Y) % 7)
                if best is None or score < best[0]:
                    best = (score, X, Y)
    if best:
        zset(best[1], best[2], 'DOOR')
        INVENTED_DOORS.append((best[1], best[2]))


INVENTED_DOORS = []     # doors ensure_door() had to add (the trace had none that worked)
CUR_REF = None          # the trace build_from_ref() laid last (stamp() reads its doors)


def ref_doors(X0, Y0, X1, Y1, kinds='d'):
    """Door tiles on a rectangle's outline where the current trace has a door mark crossing it
    (the tile just inside the outline), so stamped buildings keep OSRS's entrances."""
    ref = CUR_REF
    if ref is None:
        return []
    rx0, ry0, rx1, ry1 = ref['rect']
    N = lambda X, Y: ref['N'][ry1 - Y][X - rx0] if rx0 <= X <= rx1 and ry0 <= Y <= ry1 + 1 else '.'
    W = lambda X, Y: ref['W'][ry1 - Y][X - rx0] if rx0 <= X <= rx1 + 1 and ry0 <= Y <= ry1 else '.'
    out = []
    # an outline tile's door mark may be on its outer or its inner edge
    for X in range(X0, X1 + 1):
        for (Y, e) in ((Y1, N(X, Y1)), (Y1, N(X, Y1 - 1)), (Y0, N(X, Y0 - 1)), (Y0, N(X, Y0))):
            if e in kinds:
                out.append((X, Y))
    for Y in range(Y0, Y1 + 1):
        for (X, e) in ((X0, W(X0, Y)), (X0, W(X0 + 1, Y)), (X1, W(X1 + 1, Y)), (X1, W(X1, Y))):
            if e in kinds:
                out.append((X, Y))
    corners = {(X0, Y0), (X0, Y1), (X1, Y0), (X1, Y1)}
    return sorted(set(t for t in out if t not in corners))


def build_from_ref(ref, style):
    """Lay a traced town into the current zone context. OSRS walls run along tile edges; here a
    wall fills a whole cell, so each wall edge takes the tile on its indoor side (between two rooms,
    the tile of the larger room; between two outdoor tiles, the northern/western one).

    Steps: ground per tile; map lines inside a building that run only a tile or two are furniture
    (counters, booths, tables) and are dropped; rooms are floor tiles joined across open edges;
    rooms of a few tiles join their neighbour; a building is the rooms joined through doors and
    doorways, with one wall material (by its floors) and one roof.

    style: 'stone'/'wood' wall types for buildings with stone/wood floors, 'road' (ground for 'c'),
    'sand' (ground for 's'), 'fence' (walls between outdoor tiles), 'city' (thick black city
    walls), 'roof_stone'/'roof_wood' (roof styles), 'tree', 'oaks', 'desert', 'crops'."""
    global CUR_REF
    CUR_REF = ref
    X0, Y0, X1, Y1 = ref['rect']
    Wd, Ht = X1 - X0 + 1, Y1 - Y0 + 1
    G = ref['G']
    at = lambda c, r: G[r][c] if 0 <= c < Wd and 0 <= r < Ht else 'g'
    indoor = lambda c, r: at(c, r) in 'fwk'
    ground = dict(REF_GROUND, c=style.get('road', 'COBBLE'), s=style.get('sand', 'SAND'))
    for r in range(Ht):
        for c in range(Wd):
            ch = at(c, r)
            if ch == '#':
                zset(X0 + c, Y1 - r, style.get('city', 'WALL'), style.get('city_ground', 'GRASS'))
            else:
                zfill(X0 + c, Y1 - r, X0 + c, Y1 - r, ground[ch])
    rawN = lambda c, r: ref['N'][r][c] if 0 <= r <= Ht and 0 <= c < Wd else '.'
    rawW = lambda c, r: ref['W'][r][c] if 0 <= r < Ht and 0 <= c <= Wd else '.'
    # short interior runs (furniture lines)
    stub = set()
    for (kind, along, E) in (('N', (1, 0), rawN), ('W', (0, 1), rawW)):
        part = lambda c, r, E=E: E(c, r) in '-|'
        inner = lambda c, r, kind=kind: (indoor(c, r - 1) and indoor(c, r)) if kind == 'N' else (indoor(c - 1, r) and indoor(c, r))
        for r in range(Ht + 1):
            for c in range(Wd + 1):
                if not (part(c, r) and inner(c, r)) or (part(c - along[0], r - along[1]) and inner(c - along[0], r - along[1])):
                    continue
                run = []
                a, b = c, r
                while part(a, b) and inner(a, b):
                    run.append((a, b))
                    a, b = a + along[0], b + along[1]
                if len(run) <= 2:
                    stub.update((kind,) + t for t in run)
    edgeN = lambda c, r: '.' if ('N', c, r) in stub else rawN(c, r)
    edgeW = lambda c, r: '.' if ('W', c, r) in stub else rawW(c, r)
    nbrs = lambda a, b: ((0, -1, edgeN(a, b)), (0, 1, edgeN(a, b + 1)), (-1, 0, edgeW(a, b)), (1, 0, edgeW(a + 1, b)))
    # rooms
    room = {}
    sizes = []
    for r in range(Ht):
        for c in range(Wd):
            if not indoor(c, r) or (c, r) in room:
                continue
            k = len(sizes)
            stack, n = [(c, r)], 0
            room[(c, r)] = k
            while stack:
                a, b = stack.pop()
                n += 1
                for (da, db, e) in nbrs(a, b):
                    t = (a + da, b + db)
                    if e == '.' and indoor(*t) and t not in room:
                        room[t] = k
                        stack.append(t)
            sizes.append(n)
    parent = list(range(len(sizes)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    rsize = lambda k: sum(sizes[j] for j in range(len(sizes)) if find(j) == find(k))
    # tiny rooms (closets, booth bays) open into a neighbouring room: drop the walls between them
    merged = set()
    for (a, b), k in list(room.items()):
        if sizes[k] > 4:
            continue
        for (da, db, e) in nbrs(a, b):
            t = (a + da, b + db)
            if t in room and room[t] != k and e in '-|':
                merged.add(((a, b), t))
                parent[find(k)] = find(room[t])
    size_of = {}
    for j in range(len(sizes)):
        size_of[find(j)] = size_of.get(find(j), 0) + sizes[j]
    # buildings: rooms joined through doors and doorways (or merged above)
    bpar = {}

    def bfind(i):
        bpar.setdefault(i, i)
        while bpar[i] != i:
            bpar[i] = bpar[bpar[i]]
            i = bpar[i]
        return i
    for r in range(Ht):
        for c in range(Wd):
            for (e, n) in ((edgeN(c, r + 1), (c, r + 1)), (edgeW(c + 1, r), (c + 1, r))):
                if e in 'do' and (c, r) in room and n in room:
                    bpar[bfind(find(room[(c, r)]))] = bfind(find(room[n]))
    bld_of = lambda t: bfind(find(room[t]))
    bwood = {}
    for t in room:
        k = bld_of(t)
        w_, n_ = bwood.get(k, (0, 0))
        bwood[k] = (w_ + (at(*t) == 'w'), n_ + 1)
    is_wood = lambda t: t in room and bwood[bld_of(t)][0] * 2 > bwood[bld_of(t)][1]
    out = []        # buildings: dict(tiles, wood, roof, rect (OSRS), special)
    host = {}       # tile -> 'wall' / 'door' / 'fence' / 'gate'
    wall_out = set()  # walls pushed onto the outdoor tile beside a narrow passage
    ext_doors = {}    # indoor tile -> kinds of exterior entrance edges ('d' door, 'o' gap)

    def run_len(t, horizontal):
        """Floor tiles in a straight line through t (across the room) without crossing an edge."""
        n = 1
        for sgn in (-1, 1):
            a = t
            while True:
                b = (a[0] + sgn, a[1]) if horizontal else (a[0], a[1] + sgn)
                e = (edgeW(max(a[0], b[0]), a[1]) if horizontal else edgeN(a[0], max(a[1], b[1])))
                if not indoor(*b) or e != '.':
                    break
                n += 1
                a = b
        return n

    def place(t, kind):
        old = host.get(t)
        if old in ('door', 'gate') or (old == 'wall' and kind == 'fence'):
            return
        host[t] = kind

    for r in range(Ht + 1):
        for c in range(Wd + 1):
            for (e, a, b) in ((edgeN(c, r) if c < Wd else '.', (c, r - 1), (c, r)),
                              (edgeW(c, r) if r < Ht else '.', (c - 1, r), (c, r))):
                if e == '.' or (a, b) in merged or (b, a) in merged:
                    continue
                door = e in 'do'
                ia, ib = indoor(*a), indoor(*b)
                if ia != ib:
                    t_in, t_out = (a, b) if ia else (b, a)
                    if door:
                        ext_doors.setdefault(t_in, set()).add(e)
                    vert = a[1] == b[1]                    # the edge runs north-south
                    if not door and run_len(t_in, vert) <= 2 and 0 <= t_out[0] < Wd and 0 <= t_out[1] < Ht:
                        place(t_out, 'wall')               # keep a narrow passage open
                        wall_out.add(t_out)
                    else:
                        place(t_in, 'door' if door else 'wall')
                elif ia and ib:
                    if find(room[a]) == find(room[b]) and not door:
                        continue                  # inside a merged room
                    t = a if size_of[find(room[a])] >= size_of[find(room[b])] else b
                    place(t, 'door' if door else 'wall')
                else:
                    t = a
                    if at(*a) in 'cp' and at(*b) == 'g':
                        t = b
                    if not (0 <= t[0] < Wd and 0 <= t[1] < Ht):
                        t = b if t == a else a
                    place(t, 'gate' if door else 'fence')
    for r in range(Ht):
        for c in range(Wd):
            if ref['X'][r][c] == '/':
                near_in = any(indoor(c + dc, r + dr) for dc in (-1, 0, 1) for dr in (-1, 0, 1))
                place((c, r), 'wall' if near_in else 'fence')
    for (c, r), kind in host.items():
        if not (0 <= c < Wd and 0 <= r < Ht):
            continue
        X, Y = X0 + c, Y1 - r
        if kind == 'wall':
            t = (c, r) if (c, r) in room else next(((c + u, r + v) for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1))
                                                    if (c + u, r + v) in room), None)
            zset(X, Y, style['wood'] if (t and is_wood(t)) else style['stone'],
                 None if (c, r) in room else ('FLOOR' if not (t and is_wood(t)) else 'WOODFLOOR'))
        elif kind == 'door':
            zset(X, Y, 'DOOR')
        elif kind == 'fence':
            if TY[cy(Y)][cx(X)] not in WALLS:
                zset(X, Y, style.get('fence', 'FENCE'))
        # a gate in a fence stays open ground
    for r in range(Ht):
        for c in range(Wd):
            ic = ref['O'][r][c]
            X, Y = X0 + c, Y1 - r
            if (c, r) in host or indoor(c, r):
                continue
            if ic in 'TDR' and at(c, r) in 'gpms':
                wet = any(at(c + dc, r + dr) == '~' for dc in range(-3, 4) for dr in range(-3, 4))
                tree = 'WILLOW' if wet else style.get('tree', 'TREE')
                if style.get('desert'):
                    tree = 'CACTUS' if at(c, r) == 's' and not wet else 'PALM'
                if ic == 'T' and style.get('oaks') and hsh(c, r, 7) % 4 == 0:
                    tree = 'OAK'
                zput(X, Y, {'T': tree, 'D': 'DEADTREE', 'R': 'BOULDER'}[ic])
            elif ic == 'C' and at(c, r) in 'gp':
                zput(X, Y, style.get('crops', 'CROPS'))
            elif ic == '*' and at(c, r) == 'g':
                zput(X, Y, 'FERN')
    # entrances follow the map: door marks and the gaps in wall lines (OSRS archways) both stay open
    groups = {}
    for t in room:
        groups.setdefault(bld_of(t), []).append(t)
    for t in wall_out:
        nb = next(((t[0] + u, t[1] + v) for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (t[0] + u, t[1] + v) in room), None)
        if nb is not None and t not in room:
            groups[bld_of(nb)].append(t)
    for comp in groups.values():
        if len(comp) < 4:
            continue
        wood = is_wood(comp[0])
        tiles_ = [(X0 + a, Y1 - b) for (a, b) in comp]
        # no door is invented here: a building the map draws closed is closed in OSRS too
        g = zroof(style['roof_wood'] if wood else style['roof_stone'], tiles_)
        xs, ys = [t_[0] for t_ in tiles_], [t_[1] for t_ in tiles_]
        out.append(dict(tiles=set(tiles_), wood=wood, roof=g, rect=(min(xs), min(ys), max(xs), max(ys)), special=False))
    reserve(cx(X0), cy(Y1), cx(X1), cy(Y0))
    return out


def building_at(blds, X, Y):
    """The building with tile (X, Y) (or the smallest whose rectangle holds it); marks it as
    special so the generic furnishing leaves it alone."""
    hit = [b for b in blds if (X, Y) in b['tiles']] or \
        sorted((b for b in blds if b['rect'][0] <= X <= b['rect'][2] and b['rect'][1] <= Y <= b['rect'][3]),
               key=lambda b: len(b['tiles']))
    if not hit:
        raise SystemExit('no building at %d,%d' % (X, Y))
    hit[0]['special'] = True
    return hit[0]


def interior_of(b):
    """Walkable floor tiles of a building (OSRS coordinates)."""
    return [t for t in sorted(b['tiles']) if TY[cy(t[1])][cx(t[0])] in WALKSET and TY[cy(t[1])][cx(t[0])] != 'DOOR']


def stamp(blds, X0, Y0, X1, Y1, wall, floor, doors=None, roof='slate', shape=None):
    """Replace whatever the trace made of a rectangle with a clean building (walls on its outline,
    floor inside, the given door tiles), with its own roof. shape(X, Y) -> False cuts tiles out of
    the rectangle (chamfered corners, L shapes). Returns the building record."""
    tiles_ = [(X, Y) for X in range(X0, X1 + 1) for Y in range(Y0, Y1 + 1) if shape is None or shape(X, Y)]
    ts = set(tiles_)
    found = ref_doors(X0, Y0, X1, Y1) or ref_doors(X0, Y0, X1, Y1, 'o')
    if found:
        doors = found               # the map's own entrances win over a guess
    doors = doors or ()
    for (X, Y) in tiles_:
        x, y = cx(X), cy(Y)
        g = ROOF[y][x]
        if g:
            ROOFS[g][1].discard((x, y))
            ROOF[y][x] = 0
    for b in list(blds):
        if b['tiles'] & ts:
            # what the trace made of this spot goes entirely (its leftovers become open ground)
            for (X, Y) in b['tiles'] - ts:
                x, y = cx(X), cy(Y)
                g = ROOF[y][x]
                if g:
                    ROOFS[g][1].discard((x, y))
                    ROOF[y][x] = 0
                if TY[y][x] in WALLS or TY[y][x] in ('DOOR',) or GR[y][x] in ('FLOOR', 'WOODFLOOR', 'CHECKER', 'RUG'):
                    zset(X, Y, 'PATH', 'PATH')
            blds.remove(b)
    inner = [t for t in tiles_ if all((t[0] + u, t[1] + v) in ts for u in (-1, 0, 1) for v in (-1, 0, 1))]
    for (X, Y) in tiles_:
        zset(X, Y, wall, floor)
    for (X, Y) in inner:
        zset(X, Y, floor, floor)
    for (X, Y) in doors:
        zset(X, Y, 'DOOR', floor)
        for (u, v) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (X + u, Y + v)
            if n not in ts and TY[cy(n[1])][cx(n[0])] not in WALKSET:
                zset(n[0], n[1], 'PATH', 'PATH')
    g = zroof(roof, tiles_)
    b = dict(tiles=ts, wood=floor == 'WOODFLOOR', roof=g, rect=(X0, Y0, X1, Y1), special=True)
    blds.append(b)
    return b


def zshop(name, b, npc, _retry=False):
    """A shop in a traced building: the keeper stands at the floor tile farthest from the door,
    with a counter one step toward the door when the room stays connected."""
    from collections import deque
    floor = set(interior_of(b))
    doors = [t for t in b['tiles'] if TY[cy(t[1])][cx(t[0])] == 'DOOR']
    if floor and not doors and not _retry:
        ensure_door(b['tiles'])         # a shop the map draws without a door: open the street side
        return zshop(name, b, npc, True)
    assert floor and doors, ('shop building without floor or door', name, b['rect'])
    dist = {}
    q = deque()
    for d in doors:
        dist[d] = 0
        q.append(d)
    while q:
        a = q.popleft()
        for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (a[0] + u, a[1] + v)
            if n in floor and n not in dist:
                dist[n] = dist[a] + 1
                q.append(n)
    reach = [t for t in floor if t in dist]
    if not reach:
        assert not _retry, ('shop %s: no way in' % name, b['rect'])
        ensure_door(b['tiles'], force=True)
        return zshop(name, b, npc, True)
    K = max(reach, key=lambda t: (dist[t], -abs(t[0] - (b['rect'][0] + b['rect'][2]) / 2.0)))
    cand = sorted((n for n in ((K[0] + u, K[1] + v) for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1))) if n in dist),
                  key=lambda n: dist[n])
    for C in cand:
        rest = set(reach) - {C}
        seen, st = set(), [d for d in doors]
        seen.update(st)
        while st:
            a = st.pop()
            for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (a[0] + u, a[1] + v)
                if n in rest and n not in seen:
                    seen.add(n)
                    st.append(n)
        if rest <= seen:
            zput(C[0], C[1], 'COUNTER')
            break
    SHOPS[:] = [sh for sh in SHOPS if sh[0] != name]
    SHOPS.append((name,) + zc((b['rect'][0] + b['rect'][2]) // 2, (b['rect'][1] + b['rect'][3]) // 2))
    SHOP_SKETCH.discard(name)
    if npc != 'SHOPKEEPER':
        SHOP_OWNERS.add(npc)
    SPAWNS.append((npc,) + zc(*K))
    b['special'] = True


def deco(X, Y, t, on=('GRASS', 'FLOWERS', 'FERN', 'PATH', 'COBBLE', 'DESERT', 'SAND')):
    """Put a decoration on an open ground tile (skipped if something is already there)."""
    x, y = cx(X), cy(Y)
    if inc(x, y) and TY[y][x] in on and not ROOF[y][x]:
        TY[y][x] = t
        return True
    return False


def deco_near(X, Y, t, r=4, on=('GRASS', 'FLOWERS', 'FERN')):
    for d in range(r + 1):
        for (u, v) in [(u, v) for u in range(-d, d + 1) for v in range(-d, d + 1) if max(abs(u), abs(v)) == d]:
            if deco(X + u, Y + v, t, on):
                return (X + u, Y + v)
    return None


def ruin(blds, b, gap=3):
    """Turn a traced building into roofless ruins: grass floor, broken walls, no roof."""
    for (X, Y) in b['tiles']:
        x, y = cx(X), cy(Y)
        g = ROOF[y][x]
        if g:
            ROOFS[g][1].discard((x, y))
            ROOF[y][x] = 0
        if TY[y][x] in WALLS:
            TY[y][x] = 'WALL' if hsh(X, Y, 3) % gap else 'GRASS'
            GR[y][x] = 'GRASS'
        elif TY[y][x] in WALKSET:
            zset(X, Y, 'GRASS', 'GRASS')
    blds.remove(b)


def clear_inside(b):
    """Remove a building's inner walls (keeps its outline) to make one hall."""
    for (X, Y) in b['tiles']:
        if TY[cy(Y)][cx(X)] in WALLS and all((X + u, Y + v) in b['tiles'] for u in (-1, 0, 1) for v in (-1, 0, 1)):
            zset(X, Y, GR[cy(Y)][cx(X)] if GR[cy(Y)][cx(X)] in WALKSET else 'FLOOR')


def bank(b, side, booths=None):
    """Booths with bankers behind them along one wall of a building."""
    clear_inside(b)
    x0, y0, x1, y1 = orect(*b['rect'])
    service((x0, y0, x1, y1), side, 'BOOTH', 'BANKER')


def furnish(b, kit=None, seed=0):
    """Fill a house with OSRS-style furniture against its walls, keeping every floor tile it does
    not take reachable from the doors."""
    kit = kit or ['BED', 'TABLE', 'CHAIR', 'DRAWERS', 'BOOKCASE', 'FIREPLACE', 'BARREL', 'CHAIR', 'POTPLANT']
    r = random.Random(hsh(*b['rect'], seed))
    floor = interior_of(b)
    fl = set(floor)
    doors = [t for t in b['tiles'] if TY[cy(t[1])][cx(t[0])] == 'DOOR']
    near_door = set((d[0] + u, d[1] + v) for d in doors for u in (-1, 0, 1) for v in (-1, 0, 1))
    walls = lambda t: sum(TY[cy(t[1] + v)][cx(t[0] + u)] in WALLS for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)))
    spots = [t for t in floor if walls(t) >= 1 and t not in near_door]
    r.shuffle(spots)
    spots.sort(key=lambda t: -walls(t))

    def ncomp(free):
        graph = free | set(doors)
        seen, n = set(), 0
        for t0 in free:
            if t0 in seen:
                continue
            n += 1
            seen.add(t0)
            st = [t0]
            while st:
                a = st.pop()
                for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    q = (a[0] + u, a[1] + v)
                    if q in graph and q not in seen:
                        seen.add(q)
                        st.append(q)
        return n
    comps0 = ncomp(fl)
    connected = lambda free: ncomp(free) <= comps0
    door_need = [d for d in doors if any((d[0] + u, d[1] + v) in fl for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    budget = max(1, len(floor) // 5)
    placed = 0
    for item in kit:
        if placed >= budget:
            break
        for t in spots:
            if t not in fl:
                continue
            if item == 'FIREPLACE' and TY[cy(t[1] + 1)][cx(t[0])] not in WALLS:
                continue                        # against the north wall
            if item == 'TABLE' and walls(t) >= 2:
                continue
            if connected(fl - {t}) and all(any((d[0] + u, d[1] + v) in fl - {t} for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                                           for d in door_need):
                zput(t[0], t[1], item)
                fl.discard(t)
                placed += 1
                break
    # a rug in the middle of bigger rooms
    if len(fl) >= 12:
        mx = sum(t[0] for t in fl) / len(fl)
        my = sum(t[1] for t in fl) / len(fl)
        for t in sorted(fl, key=lambda t: (t[0] - mx) ** 2 + (t[1] - my) ** 2)[:4]:
            zset(t[0], t[1], 'RUG', 'RUG')


def migrate_ow_content():
    """Gameplay the countryside sketch placed inside a town is re-landed on the town as rebuilt:
    NPCs, ground items, quest objects, fishing spots and named positions move to the nearest
    cell that suits them. Shop and bank staff are left to the towns' scripts (they need their
    counters)."""
    global SPAWNS, ITEM_SPAWNS, QUEST_OBJS, FISH_SPOTS
    taken = set((x, y) for (_, x, y) in SPAWNS)
    land = lambda x, y, ok: nearest_walk(x, y, ok, 12)
    open_ok = lambda a, b: walk_ok(a, b) and TY[b][a] != 'BRIDGE' and REACH[b][a] and (a, b) not in taken
    sp = []
    for (k, x, y) in SPAWNS:
        if not town_at(x, y):
            sp.append((k, x, y))
            continue
        if k in ('BANKER', 'SHOPKEEPER') or k in SHOP_OWNERS:
            if (k, x, y) not in SKETCH_SPAWNS:
                sp.append((k, x, y))        # placed by the town's script
            continue
        taken.discard((x, y))
        d = (x, y) if open_ok(x, y) else land(x, y, open_ok)
        assert d, ('no town cell for', k, x, y)
        taken.add(d)
        sp.append((k,) + d)
    SPAWNS = sp
    ITEM_SPAWNS = [(it, x, y) if not town_at(x, y) or (walk_ok(x, y) and REACH[y][x]) else
                   (it,) + land(x, y, lambda a, b: walk_ok(a, b) and REACH[b][a]) for (it, x, y) in ITEM_SPAWNS]
    qo = []
    for (t, x, y) in QUEST_OBJS:
        if not town_at(x, y) or TY[y][x] == t:
            qo.append((t, x, y))
            continue
        if t == 'TOLLGATE':
            continue                # the town's script places its own gate
        d = land(x, y, lambda a, b: walk_ok(a, b) and REACH[b][a] and (a, b) not in taken and
                 sum(walk_ok(a + u, b + v) for u, v in ((1, 0), (-1, 0), (0, 1), (0, -1))) >= 2)
        assert d, ('no town cell for quest object', t, x, y)
        TY[d[1]][d[0]] = t
        taken.add(d)
        qo.append((t,) + d)
    QUEST_OBJS = qo
    fs = []
    for (k, x, y) in FISH_SPOTS:
        if not town_at(x, y) or TY[y][x] == 'WATER':
            fs.append((k, x, y))
            continue
        fish(k, OW.OX + x, OW.OY - y, lambda a, b: REACH[b][a])
        fs.append(FISH_SPOTS.pop())
    FISH_SPOTS = fs
    for n, (x, y) in list(QPOS.items()):
        if SKETCH_QPOS.get(n) == (x, y) and town_at(x, y) and not walk_ok(x, y):
            QPOS[n] = land(x, y, lambda a, b: walk_ok(a, b))


set_ctx(ZF1)
qobj('WHEEL', *zc(3210, 3212))                              # spinning wheel next to the Duke's room
set_ctx(ZL0)
qobj('COFFIN', *zc(3250, 3193))                             # the restless ghost's coffin, in the crypt
set_ctx(ZL0)
shop('LUMBRIDGE', orect(*GS_LUMB), 'W', 'SHOPKEEPER')
shop('BOB', orect(*BOB), 'W', 'BOB')
set_ctx(OW)

# the other towns: tools/zones/<town>.py, run in this module's namespace (see tools/zones/README)
TOWNS = []
for _town in ('draynor', 'varrock', 'alkharid', 'falador', 'edgeville'):   # noqa
    _path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zones', _town + '.py')
    if os.path.exists(_path):
        exec(compile(open(_path).read(), _path, 'exec'))
        TOWNS.append(_town)
        set_ctx(OW)
def join_roads():
    """Where a countryside road reaches a town's edge off the town's own streets, pave the gap to
    the nearest street (both follow the OSRS map, but not tile for tile)."""
    set_ctx(OW)
    street = lambda x, y: TY[y][x] in ('PATH', 'COBBLE', 'BRIDGE')
    ground = ('GRASS', 'FLOWERS', 'SAND', 'DESERT', 'SWAMP', 'FERN', 'SNOW', 'WILD')
    for c in CITIES:
        z = c.ctx
        x0, y0, x1, y1 = z.X0 - 1, z.Y0 - 1, z.X1 + 1, z.Y1 + 1
        ring = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
                if (x in (x0, x1) or y in (y0, y1)) and inb(x, y) and y < OW_H]
        for (x, y) in ring:
            if not (ROAD[y][x] and street(x, y)) or owner_of(x, y):
                continue
            ix = x + (1 if x == x0 else -1 if x == x1 else 0)
            iy = y + (1 if y == y0 else -1 if y == y1 else 0)
            if street(ix, iy):
                continue
            best = None
            for ay in range(-12, 13):
                for ax in range(-12, 13):
                    a, b = ix + ax, iy + ay
                    if z.X0 <= a <= z.X1 and z.Y0 <= b <= z.Y1 and street(a, b) and owner_of(a, b) is c:
                        d = ax * ax + ay * ay
                        if best is None or d < best[0]:
                            best = (d, a, b)
            if best is None:
                continue
            _, a, b = best
            n = max(abs(a - x), abs(b - y))
            for k in range(n + 1):
                px, py = round(x + (a - x) * k / n), round(y + (b - y) * k / n)
                for (qx, qy) in ((px, py), (px + 1, py), (px, py + 1)):
                    if inb(qx, qy) and TY[qy][qx] in ground and not ROOF[qy][qx]:
                        TY[qy][qx] = GR[qy][qx] = 'PATH'


join_roads()
for _y in range(1, OW_H):              # no canopy over a roof (canopies are drawn on the cell above)
    for _x in range(OW_W):
        if TY[_y][_x] in TREES and ROOF[_y - 1][_x]:
            TY[_y][_x] = GR[_y][_x] = 'GRASS'


# ========================================================================================
# NPCs
# ========================================================================================
def flood():
    """Cells reachable from START on foot, through doors, gates and staircases."""
    from collections import deque
    seen = [[False] * MW for _ in range(MH)]
    q = deque([START])
    seen[START[1]][START[0]] = True

    def visit(x, y):
        if not seen[y][x]:
            seen[y][x] = True
            q.append((x, y))
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            xx, yy = x + dx, y + dy
            if not inb(xx, yy) or seen[yy][xx]:
                continue
            if (xx, yy) in LINKS:
                for d in LINKS[(xx, yy)]:
                    visit(*d)
            elif TY[yy][xx] in WALKSET or TY[yy][xx] in ('LOCKDOOR', 'TOLLGATE'):
                visit(xx, yy)
    return seen


REACH = flood()
spawn('WOMAN', 3215, 3256)
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
spawn('FRED', 3188, 3275)
spawn_area('SHEEP', 5, 3197, 3258, 3212, 3266)
spawn('DAIRY_COW', 3256, 3272)
spawn('URHNEY', 3147, 3183)
spawn('ROMEO', 3211, 3423)
SPAWNS.append(('JULIET', *JULIET))
SPAWNS.append(('LAWRENCE', *LAWRENCE))
spawn('APOTHECARY', 3194, 3406)
SPAWNS.append(('DORIC', *DORIC_POS))
spawn('SEDRIDOR', 3104, 3160)
spawn('MIZGOG', 3109, 3166)
spawn('TRAIBORN', 3112, 3156)
spawn('SKELETON', int(wcx) + 4, int(wcy) + 4, cell=True)
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

# --- Lumbridge (1:1 zone) ---------------------------------------------------------------------
set_ctx(ZL0)
SPAWNS.append(('GUIDE', START[0] + 3, START[1] - 2))
SPAWNS.append(('COOK', *LUMB_COOK))
SPAWNS.append(('HANS', *HANS))
spawn('GUARD', 3215, 3224)
spawn('GUARD', 3215, 3212)
spawn('GUARD', 3229, 3220)
spawn('MAN', 3225, 3238)
spawn('WOMAN', 3232, 3228)
spawn('MAN', 3204, 3246)
spawn('WOMAN', 3236, 3206)
spawn_area('GOBLIN', 7, 3246, 3230, 3259, 3252)
SPAWNS.append(('AERECK', *AERECK))
SPAWNS.append(('GHOST', *zc(3249, 3192)))                  # beside the coffin
set_ctx(ZF1)
SPAWNS.append(('DUKE', *DUKE))
spawn('GUARD', 3215, 3220)
set_ctx(ZF2)
SPAWNS.append(('BANKER', *zc(3208, 3216)))
SPAWNS.append(('BANKER', *zc(3208, 3220)))
set_ctx(OW)

# --- respawning ground items (item, x, y) -----------------------------------------------------
ITEM_SPAWNS = []


def item_spawn(item, x, y):
    assert TY[y][x] in WALKSET and TY[y][x] not in ('DOOR',), (item, x, y, TY[y][x])
    ITEM_SPAWNS.append((item, x, y))


item_spawn('EGG', cx(3231), cy(3298))                       # north chicken coop
item_spawn('EGG', cx(3174), cy(3300))                       # Fred's chicken coop
set_ctx(ZL0)
item_spawn('POT', *zc(3210, 3212))                          # castle kitchen
item_spawn('BUCKET', *zc(3206, 3212))
set_ctx(ZF2)
item_spawn('LOGS', *zc(3212, 3224))                         # the bank floor's log spawns
item_spawn('LOGS', *zc(3212, 3213))
set_ctx(OW)
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


def fish(kind, X, Y, land_ok=None):
    x0, y0 = cx(X), cy(Y)
    best = None
    for y in range(y0 - 12, y0 + 13):
        for x in range(x0 - 12, x0 + 13):
            if not inb(x, y) or TY[y][x] != 'WATER':
                continue
            if any((k, xx, yy) for (k, xx, yy) in FISH_SPOTS if (xx, yy) == (x, y)):
                continue
            land = [(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                    if inb(x + dx, y + dy) and TY[y + dy][x + dx] in WALKSET and TY[y + dy][x + dx] != 'BRIDGE'
                    and (land_ok is None or land_ok(x + dx, y + dy))]
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
for (X, Y) in ((3022, 3222), (3018, 3210)):                                 # Port Sarim
    fish('NET', X, Y)
set_ctx(ZL0)
for (X, Y) in ((3238, 3244), (3238, 3249)):                                 # Lumbridge river (fly)
    fish('ROD', X, Y)
set_ctx(OW)
migrate_ow_content()
for (n, x, y) in SHOPS:
    assert n not in SHOP_SKETCH or not town_at(x, y), ('shop %s is only in the sketch of a rebuilt town: its town must re-add it' % n)

# ========================================================================================
# Regions for the area banner (first match wins; last entry is the fallback)
# ========================================================================================
REGIONS_O = [
    ('Wilderness', 2928, 3521, 3343, 3552),
    ('Lumbridge Swamp', 3133, 3137, 3260, 3200),
    ('Wizards\' Tower', 3090, 3140, 3130, 3180),
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
for (ctx, n, X0, Y0, X1, Y1) in ((ZF1, 'Lumbridge Castle', 3195, 3200, 3226, 3236),
                                 (ZF2, 'Lumbridge Castle', 3195, 3200, 3226, 3236),
                                 (ZL0, 'Lumbridge Castle', 3199, 3200, 3231, 3236),
                                 (ZL0, 'Lumbridge Swamp', 3187, 3180, 3236, 3199),
                                 (ZL0, 'Lumbridge', 3187, 3180, 3261, 3263)):
    set_ctx(ctx)
    REGIONS.append((n,) + orect(X0, Y0, X1, Y1))
set_ctx(OW)
for (n, X0, Y0, X1, Y1) in REGIONS_O:
    x0, y0, x1, y1 = orect(X0, Y0, X1, Y1)
    REGIONS.append((n, max(0, x0), max(0, y0), min(OW_W - 1, x1), min(OW_H - 1, y1)))

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
for _n, (_x, _y) in list(LANDMARKS.items()):      # the sketch's positions inside a rebuilt town
    if town_at(_x, _y) and not (walk_ok(_x, _y) and REACH[_y][_x]):
        LANDMARKS[_n] = nearest_walk(_x, _y, lambda a, b: walk_ok(a, b) and REACH[b][a], 12)

# ========================================================================================
# Validation: reachability from the start position
# ========================================================================================


def check_reachability():
    seen = flood()
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
    # the swamp must be a short walk from the graveyard (its south gate)
    from collections import deque
    set_ctx(ZL0)
    g0 = zc(3244, 3195)
    dist = {g0: 0}
    q = deque([g0])
    found = None
    while q and found is None:
        c = q.popleft()
        if dist[c] > 12:
            break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + dx, c[1] + dy)
            if not inb(*n) or n in dist or TY[n[1]][n[0]] not in WALKSET:
                continue
            dist[n] = dist[c] + 1
            if GR[n[1]][n[0]] == 'SWAMP':
                found = n
            q.append(n)
    set_ctx(OW)
    if not found:
        errs.append('the swamp is not within 12 steps of the Lumbridge graveyard (south gate)')
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


CHARS.update({'CHECKER': '#', 'VOID': ' ', 'STAIRS': '/', 'TABLE': 'T', 'BED': 'b', 'STATUE': 's',
              'PEW': 'n', 'HEDGE': 'h', 'CRATE': 'x', 'FERN': '.', 'RUG': 'r', 'CHAIR': 'c', 'BOOKCASE': 'k',
              'DRAWERS': 'd', 'FIREPLACE': 'F', 'BARREL': 'o', 'WARDROBE': 'w', 'POTPLANT': 'p', 'BIGSTALL': 'L',
              'ANVIL': 'a', 'FURNACE': 'u', 'SHELF': 'k', 'THRONE': 'T', 'WELL': 'O', 'SACKS': 's', 'FLOWERBED': ',',
              'PALM': 'P', 'BENCH': 'n', 'SUNFLOWER': '*', 'LOWWALL': 'l', 'VFOUNTAIN': 'F'})


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


DIRS8 = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]


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


LEGACY_OBJS = set(ROCKS) | {'ROCK_EMPTY', 'BOULDER', 'FIREPIT', 'STALL', 'DITCH'}


def legacy_obj(x, y, img):
    """Countryside objects hd_art has no art of its own for, drawn over the cell's ground."""
    t = TY[y][x]
    img = [r[:] for r in img]
    if t == 'DITCH':
        for py in range(5, 11):
            for px in range(16):
                img[py][px] = WC['OUT'] if 6 <= py <= 9 else WC['WD2']
        for px in range(0, 16, 3):
            img[5][px] = WC['WD1']
    elif t in ROCKS or t == 'ROCK_EMPTY':
        paste(img, rock_image(None if t == 'ROCK_EMPTY' else t), 0, 0)
    elif t == 'BOULDER':
        paste(img, rock_image(None), 0, 0)
    elif t == 'FIREPIT':
        paste(img, firepit_image(), 0, 0)
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
    # this art is in the old map colours: use the nearest colours of the HD palette instead
    pal = set(hd_art.P.values())
    near = {}
    for r in img:
        for i, c in enumerate(r):
            if c is not None and c not in pal:
                if c not in near:
                    near[c] = min(pal, key=lambda d: cdist(c, d))
                r[i] = near[c]
    return img


# ----------------------------------------------------------------------------------------
# Tile + palette packing helpers
# ----------------------------------------------------------------------------------------


import hd_art  # noqa: E402


def theme_of(x, y):
    if y >= OW_H:
        return 'LUMBRIDGE'          # the castle's upper floors
    c = OWNER[y][x]
    return c.name if c else 'OVERWORLD'


def render(x, y):
    hd_art.set_theme(theme_of(x, y))
    return hd_art.render(sys.modules[__name__], x, y, QUEST_ART)


def cut8(img16):
    tiles = []
    for ty in range(0, len(img16), 8):
        for tx in range(0, len(img16[0]), 8):
            tiles.append(tuple(tuple(img16[ty + y][tx + x] for x in range(8)) for y in range(8)))
    return tiles


def cdist(a, b):
    return (a[0] - b[0]) ** 2 * 3 + (a[1] - b[1]) ** 2 * 4 + (a[2] - b[2]) ** 2 * 2


def pack_palettes_search(tilesets, nbanks, weights, trials=int(os.environ.get('WG_TRIALS', 800)), seed=1):
    """Greedy packing of the maximal colour sets in many orders, keeping the banks that leave the
    fewest map uses to quantize. Tiles that fit no bank go to the bank that represents them best;
    their missing colours are later remapped to the nearest colour in that bank (QUANTIZED).
    WG_TRIALS=100 in the environment makes a quicker (slightly worse) build while iterating."""
    from collections import Counter
    use = Counter()
    for s_, w in zip(tilesets, weights):
        use[s_] += w
    uniq = list(use)
    maximal = [a for a in uniq if not any(a < b for b in uniq)]
    r = random.Random(seed)
    best = None
    for trial in range(trials):
        order = sorted(maximal, key=lambda a: (-len(a), r.random())) if trial % 2 else r.sample(maximal, len(maximal))
        banks = []
        for s_ in order:
            fit = None
            for bi, b in enumerate(banks):
                u = len(b | s_)
                if u <= 15 and (fit is None or u - len(b) < fit[1]):
                    fit = (bi, u - len(b))
            if fit is not None:
                banks[fit[0]] |= s_
            elif len(banks) < nbanks:
                banks.append(set(s_))
        cost = sum(use[a] for a in uniq if not any(a <= b for b in banks))
        if best is None or cost < best[0]:
            best = (cost, [set(b) for b in banks])
    banks = [sorted(b) for b in best[1]]
    assign, late = [], []
    for i, s_ in enumerate(tilesets):
        home = [bi for bi, b in enumerate(banks) if s_ <= set(b)]
        if home:
            assign.append(home[0])
            continue
        err = [sum(min(cdist(c, d) for d in b) for c in s_ if c not in b) for b in banks]
        assign.append(err.index(min(err)))
        late.append(i)
    return assign, banks, late


def flips(t):
    h = tuple(tuple(reversed(r)) for r in t)
    v = tuple(reversed(t))
    hv = tuple(tuple(reversed(r)) for r in reversed(t))
    return [(t, 0), (h, 1), (v, 2), (hv, 3)]


# ----------------------------------------------------------------------------------------
# Build metatiles, tiles and palettes
# ----------------------------------------------------------------------------------------
# ----------------------------------------------------------------------------------------
# Palette sets. Banks 0..SHARED-1 are the same in every set and hold the countryside; the rest
# belong to the towns. A town's set is loaded while the camera is near it (pal_regions in
# src/world.c), early enough that its private banks are off screen when the swap happens; towns
# whose regions overlap share one set.
# ----------------------------------------------------------------------------------------
OWNER = [[owner_of(x, y) for x in range(OW_W)] + [None] * (MW - OW_W) for y in range(OW_H)]
CAM_W, CAM_H, CAM_PAD = 16, 11, 2       # camera window in whole cells (minus one), prefetch margin


def assign_palette_sets():
    """Group towns whose camera regions overlap; returns [(cam rect, set)]."""
    reg = []
    for c in CITIES:
        z = c.ctx
        reg.append([z.X0 - CAM_W - CAM_PAD, z.Y0 - CAM_H - CAM_PAD, z.X1 + CAM_PAD, z.Y1 + CAM_PAD])
    grp = list(range(len(CITIES)))

    def root(i):
        while grp[i] != i:
            i = grp[i]
        return i
    for i in range(len(reg)):
        for j in range(i):
            a, b = reg[i], reg[j]
            if not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1]):
                grp[root(i)] = root(j)
    sets = {}
    for i, c in enumerate(CITIES):
        c.pset = sets.setdefault(root(i), len(sets) + 1)
    return [(tuple(max(0, v) for v in reg[i]), c.pset) for i, c in enumerate(CITIES)]


PAL_REGIONS = assign_palette_sets()
ZONES = [(n, z, LUMBRIDGE.pset if ps is None else ps, pl_) for (n, z, ps, pl_) in ZONES]
NSETS = max(c.pset for c in CITIES) + 1   # world palette sets: 0 countryside only, then the town groups
SHARED = int(os.environ.get('WG_SHARED', 9))   # world banks shared by every set
meta_imgs = []
meta_set = []       # palette set of each metatile
meta_index = {}


_PSET = [[OWNER[y][x].pset if y < OW_H and OWNER[y][x] else 0 for x in range(MW)] for y in range(MH)]
for _z in ZONES[1:]:
    for _y in range(_z[1].Y0, _z[1].Y1 + 1):
        for _x in range(_z[1].X0, _z[1].X1 + 1):
            _PSET[_y][_x] = _z[2]


def pset_of(x, y):
    return _PSET[y][x]


def add_meta(img, ps=0):
    key = (ps, tuple(tuple(r) for r in img))
    if key not in meta_index:
        meta_index[key] = len(meta_imgs)
        meta_imgs.append(img)
        meta_set.append(ps)
    return meta_index[key]


add_meta(blank(16, 16))  # meta 0 = transparent (overlay layer)
_cache = {}


def render_cached(x, y):
    t, g = TY[y][x], GR[y][x]
    ps = pset_of(x, y)
    # everything hd_art.render() looks at: the 3x3 neighbourhood with roofs, two rows further up
    # and down (shadows, banks, facades), the cell's parity (8x8 tile patterns) and its hashes
    key = (ps, theme_of(x, y), t, g,
           tuple((T_at(x + dx, y + dy), G_at(x + dx, y + dy)) for dx, dy in DIRS8)) + \
        tuple((T_at(x + dx, y + dy), G_at(x + dx, y + dy), ROOF[y + dy][x + dx] if inb(x + dx, y + dy) else 0)
              for dx in (-1, 0, 1) for dy in (-3, -2, 2, 3)) + \
        tuple(ROOF[y + dy][x + dx] if inb(x + dx, y + dy) else 0 for dx, dy in DIRS8 + [(0, 0)]) + \
        (hsh(x, y) % 6, x & 1, y & 1, hsh(53, x, y) % 6)
    if t in ('FLOWERS', 'STAIRS', 'FERN', 'BIGSTALL', 'FLOWERBED', 'VFOUNTAIN', 'SUNFLOWER'):
        key = key + (x, y)
    if t == 'CLIFF':
        key = key + (hsh(71, x, y) % 4,)        # hd_art.obj_cliff's variant
    r = _cache.get(key)
    if r is None:
        img, over = render(x, y)
        r = (add_meta(img, ps), add_meta(over, ps) if over is not None else 0)
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
# roofs on the overlay layer (above sprites), one image per covered cell
for g in range(1, len(ROOFS)):
    if ROOFS[g][1]:
        hd_art.set_theme(theme_of(*next(iter(ROOFS[g][1]))))
    for (x, y), img in hd_art.roof_images(sys.modules[__name__], g).items():
        map_over[y][x] = add_meta(img, pset_of(x, y))
# upper castle floors: outside the keep the player looks down onto the ground floor
for (x, y), (sx, sy) in VIEW.items():
    map_meta[y][x] = map_meta[sy][sx]
    map_over[y][x] = map_over[sy][sx]

# depleted variants of every resource cell (stump / empty rock on that cell's own ground)
RES_ALT = []
for y in range(MH):
    for x in range(MW):
        t = TY[y][x]
        if t in TREES or t in ROCKS:
            TY[y][x] = 'STUMP' if t in TREES else 'ROCK_EMPTY'
            img, _ = render(x, y)
            TY[y][x] = t
            RES_ALT.append((y * MW + x, add_meta(img, pset_of(x, y))))

tile_list = []          # (palette set, rgb tile)
tile_lookup = {}
meta_entries = []
for mi, img in enumerate(meta_imgs):
    ps = meta_set[mi]
    ents = []
    for t in cut8(img):
        if (ps, t) in tile_lookup:
            ents.append(tile_lookup[(ps, t)])
            continue
        i = len(tile_list)
        tile_list.append((ps, t))
        for ft, f in flips(t):
            tile_lookup.setdefault((ps, ft), (i, f))
        ents.append((i, 0))
    meta_entries.append(ents)

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
assign = [0] * len(tile_list)
wbanks = []             # per set: 14 banks of up to 15 colours (the first SHARED_N are the shared ones)
QUANTIZED = []
_cols = lambda i: frozenset(c for r in tile_list[i][1] for c in r if c is not None)
_ids = [i for i, (s_, t) in enumerate(tile_list) if s_ == 0]
_a, SHARED_BANKS, _late = pack_palettes_search([_cols(i) for i in _ids], SHARED, [tile_use[i] for i in _ids])
SHARED_N = len(SHARED_BANKS)
for k, i in enumerate(_ids):
    assign[i] = _a[k]
QUANTIZED += [_ids[k] for k in _late]
wbanks.append(SHARED_BANKS)
for ps in range(1, NSETS):
    ids = []
    for i, (s_, t) in enumerate(tile_list):
        if s_ != ps:
            continue
        home = [bi for bi, b in enumerate(SHARED_BANKS) if _cols(i) <= set(b)]
        if home:
            assign[i] = home[0]
        else:
            ids.append(i)
    a_, banks, late = pack_palettes_search([_cols(i) for i in ids], 14 - SHARED_N, [tile_use[i] for i in ids])
    for k, i in enumerate(ids):
        assign[i] = SHARED_N + a_[k]
    wbanks.append(SHARED_BANKS + banks)
    QUANTIZED += [ids[k] for k in late]
idx_tiles = []
idx_lookup = {}
rgb_to_idx = []
for i, (ps, t) in enumerate(tile_list):
    b = wbanks[ps][assign[i]]
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

world_pal = [[0] * (14 * 16) for _ in range(NSETS)]
water_slots = [[] for _ in range(NSETS)]
for ps in range(NSETS):
    w1, w2 = hd_art.WATER_SWAP
    for bi, b in enumerate(wbanks[ps]):
        for ci, c in enumerate(b):
            world_pal[ps][bi * 16 + ci + 1] = gba(c)
        if w1 in b and w2 in b:
            water_slots[ps].append((bi + WORLD_BANK0) * 16 + b.index(w1) + 1)
            water_slots[ps].append((bi + WORLD_BANK0) * 16 + b.index(w2) + 1)

# ----------------------------------------------------------------------------------------
# Tile-cache budget check: the most unique tiles any camera window (plus the column/row that
# is being replaced while scrolling) can reference. Must stay below the 575 cache slots.
# ----------------------------------------------------------------------------------------
CACHE_SLOTS = 575


def check_window_budget():
    """Windows are checked inside each zone only: the camera never leaves the player's zone (the
    overworld is one zone)."""
    from collections import Counter
    cell_tiles = {}

    def tiles_at(x, y):
        k = (x, y)
        if k not in cell_tiles:
            st = set(e & 0xFFFF for e in meta_out[map_meta[y][x]])
            st |= set(e & 0xFFFF for e in meta_out[map_over[y][x]])
            st.discard(0)
            cell_tiles[k] = st
        return cell_tiles[k]
    WW, WH = 17, 12      # 31x21 tiles -> at most 16x11 cells, +1 for the scroll overlap
    worst = (0, 0, 0)
    for (n, z, ps, pl_) in ZONES:
        for y0 in range(z.Y0, max(z.Y0, z.Y1 - WH + 1) + 1):
            cnt = Counter()
            live = 0

            def add(st, k):
                nonlocal live
                for t in st:
                    v = cnt[t]
                    live += (v == 0) - (v + k == 0)
                    cnt[t] = v + k
            for y in range(y0, min(y0 + WH, MH)):
                for x in range(z.X0, z.X0 + WW):
                    add(tiles_at(x, y), 1)
            for x0 in range(z.X0, max(z.X0, z.X1 - WW + 1) + 1):
                if x0 > z.X0:
                    for y in range(y0, min(y0 + WH, MH)):
                        add(tiles_at(x0 - 1, y), -1)
                        add(tiles_at(x0 + WW - 1, y), 1)
                if live > worst[0]:
                    worst = (live, x0, y0)
    return worst


WINDOW_WORST = check_window_budget()
print('palette: %d tiles quantized (%d map uses of %d; by set %s)' % (
    len(QUANTIZED), sum(tile_use[i] for i in QUANTIZED), sum(tile_use),
    ' '.join('%d:%d/%d' % (ps, sum(tile_use[i] for i in QUANTIZED if tile_list[i][0] == ps),
                          sum(tile_use[i] for i in range(len(tile_list)) if tile_list[i][0] == ps)) for ps in range(NSETS))), file=sys.stderr)
print('world: %dx%d cells, %d metatiles, %d tiles, %s banks, %d resources, worst window %d tiles at cell %d,%d, %d reachable cells'
      % (MW, MH, len(meta_imgs), len(idx_tiles), '+'.join(str(len(b)) for b in wbanks), len(RES_ALT), WINDOW_WORST[0], WINDOW_WORST[1],
         WINDOW_WORST[2], REACHABLE), file=sys.stderr)
if WINDOW_WORST[0] > CACHE_SLOTS:
    raise SystemExit('tile cache overflow: a camera window needs %d tiles (max %d)' % (WINDOW_WORST[0], CACHE_SLOTS))
assert len(SPAWNS) <= 256, len(SPAWNS)   # MAX_NPCS in src/game.h
assert len(meta_out) < 65536, ('map_meta is u16', len(meta_out))
assert MW <= 65535 and MH <= 65535

# ----------------------------------------------------------------------------------------
# C emission
# ----------------------------------------------------------------------------------------
# The game works in absolute OSRS tiles (X east, Y north, plane); the cell grid above is only how
# the generator lays the map out. osrs_of() maps a cell to its tile, through the zone it lies in.
WORLD_X0, WORLD_Y1 = OW.OX, OW.OY       # pixel origin: px = (X - WORLD_X0) * 16, py = (WORLD_Y1 - Y) * 16


def osrs_of(x, y):
    """Grid cell -> absolute OSRS tile (X, Y, plane)."""
    for (n, z, ps, p) in ZONES:
        if z.X0 <= x <= z.X1 and z.Y0 <= y <= z.Y1:
            return (z.OX + (x - z.CX0) * z.S, z.OY - (y - z.CY0) * z.S, p)
    raise ValueError('cell %d,%d lies in no zone' % (x, y))


def osrs_rect(x0, y0, x1, y1):
    """Cell rect -> OSRS (X0, Y0, X1, Y1, plane), with Y0 the south edge."""
    a, b = osrs_of(x0, y1), osrs_of(x1, y0)
    assert a[2] == b[2], ('rect spans planes', x0, y0, x1, y1)
    return (a[0], a[1], b[0], b[1], a[2])


# the grid rows each plane occupies (one zone per plane; the cell grid is still the map storage)
PLANE_GRID = {}
for (_n, _z, _ps, _p) in ZONES:
    assert _z.S == 1 and _p not in PLANE_GRID, ('one 1:1 zone per plane', _n)
    PLANE_GRID[_p] = (_z.CX0 - _z.OX, _z.CY0 + _z.OY)    # cell = (X + gx, gy - Y)
NPLANES = max(PLANE_GRID) + 1


# ----------------------------------------------------------------------------------------
# Regions: the map as OSRS map squares (64x64 tiles, per plane). Each region holds sixteen
# 16x16-tile blocks, each block LZ77-compressed in the GBA BIOS format (tools/lz77.py) with
# its cells as meta u16[256], over u16[256], type u8[256], roof u8[256] (index (y & 15) * 16 +
# (x & 15)), plus an uncompressed walkability bitmap so NPCs can move anywhere without
# decompressing. Blocks wholly outside the playable bounds are not stored; neither are
# regions outside them.
import lz77  # noqa: E402

REGION, BLOCK = 64, 16
BOUNDS = {}         # plane -> OSRS (X0, Y0, X1, Y1): the zone of that floor
for (_n, _z, _ps, _p) in ZONES:
    BOUNDS[_p] = osrs_rect(_z.X0, _z.Y0, _z.X1, _z.Y1)[:4]
RF_EDGE, RF_ROOFS, RF_OVERLAY = 1, 2, 4


def cell_of(X, Y, p):
    """OSRS tile -> grid cell, or None outside the floor's bounds."""
    X0, Y0, X1, Y1 = BOUNDS[p]
    if not (X0 <= X <= X1 and Y0 <= Y <= Y1):
        return None
    gx, gy = X + PLANE_GRID[p][0], PLANE_GRID[p][1] - Y
    return (gx, gy) if 0 <= gx < MW and 0 <= gy < MH else None


def build_regions():
    """-> list of (plane, rx, ry, pal_set, flags, [block stream or None] * 16, walk words)."""
    from collections import Counter
    regs = []
    for p in sorted(BOUNDS):
        X0, Y0, X1, Y1 = BOUNDS[p]
        for rx in range(X0 // REGION, X1 // REGION + 1):
            for ry in range(Y0 // REGION, Y1 // REGION + 1):
                blocks, walk = [], [0] * (REGION * REGION // 32)
                psets, flags = Counter(), 0
                for b in range(16):
                    bx0, by0 = rx * REGION + (b & 3) * BLOCK, ry * REGION + (b >> 2) * BLOCK
                    meta, over, typ, roof = [0] * 256, [0] * 256, [T['WATER']] * 256, [0] * 256
                    inside = 0
                    for ly in range(BLOCK):
                        for lx in range(BLOCK):
                            c = cell_of(bx0 + lx, by0 + ly, p)
                            if c is None:
                                flags |= RF_EDGE
                                continue
                            gx, gy = c
                            i = ly * BLOCK + lx
                            inside += 1
                            meta[i], over[i], typ[i], roof[i] = map_meta[gy][gx], map_over[gy][gx], T[TY[gy][gx]], ROOF[gy][gx]
                            psets[pset_of(gx, gy)] += 1
                            flags |= (RF_ROOFS if roof[i] else 0) | (RF_OVERLAY if over[i] else 0)
                            if typ[i] <= T['DOOR']:      # T_WALKABLE_MAX
                                rl, rr = (b >> 2) * BLOCK + ly, (b & 3) * BLOCK + lx
                                walk[rl * 2 + (rr >> 5)] |= 1 << (rr & 31)
                    if not inside:
                        blocks.append(None)
                        continue
                    raw = b''.join(v.to_bytes(2, 'little') for v in meta) + b''.join(v.to_bytes(2, 'little') for v in over) + \
                        bytes(typ) + bytes(roof)
                    blocks.append(lz77.compress(raw))
                if any(blocks):
                    regs.append((p, rx, ry, psets.most_common(1)[0][0], flags, blocks, walk))
    return regs


MAP_REGIONS = build_regions()
assert all(rx < 256 and ry < 256 for (_, rx, ry, *_r) in MAP_REGIONS)


def emit_header(h):
    h.append('#define MAP_REGION_COUNT %d' % len(MAP_REGIONS))
    for n, i in T.items():
        h.append('#define T_%s %d' % (n, i))
    h.append('#define T_WALKABLE_MAX %d' % T['DOOR'])
    h.append('#define WORLD_TILE_COUNT %d' % len(idx_tiles))
    h.append('#define META_COUNT %d' % len(meta_out))
    h.append('#define RES_COUNT %d' % len(RES_ALT))
    h.append('#define WORLD_PAL_SETS %d' % NSETS)
    h.append('#define WATER_SLOT_MAX %d' % max(len(w) for w in water_slots))
    h.append('#define SPAWN_COUNT %d' % len(SPAWNS))
    h.append('#define FISHSPOT_COUNT %d' % len(FISH_SPOTS))
    h.append('#define REGION_COUNT %d' % len(REGIONS))
    h.append('#define PAL_REGION_COUNT %d' % len(PAL_REGIONS))
    h.append('#define ZONE_COUNT %d' % len(ZONES))
    h.append('enum { %s };' % ', '.join('ZONE_' + z[0] for z in ZONES))
    h.append('enum { %s };' % ', '.join('SHOP_' + n for n, _, _ in SHOPS))
    h.append('#define SHOP_COUNT %d' % len(SHOPS))
    h.append('#define WORLD_X0 %d   // pixel origin (OSRS tile at px 0)' % WORLD_X0)
    h.append('#define WORLD_Y1 %d   // OSRS tile row at py 0 (Y grows north, py south)' % WORLD_Y1)
    h.append('#define PLANE_COUNT %d' % NPLANES)
    for n, cell in [('START', START)] + [(k, QPOS[k]) for k in sorted(k for k in QPOS if k.startswith('TELE_'))]:
        X, Y, P = osrs_of(*cell)        # start / respawn and the teleport spells' arrival tiles
        h.append('#define %s_X %d' % (n, X))
        h.append('#define %s_Y %d' % (n, Y))
        h.append('#define %s_P %d' % (n, P))
    h.append('''
// positions are absolute OSRS tiles: x east, y north, plane 0..3; rects are inclusive, y0 south
typedef struct { u8 kind, plane; u16 x, y; } Spawn;
typedef struct { u8 kind, plane; u16 x, y; } FishSpotDef;
typedef struct { const char *name; u16 x0, y0, x1, y1; u8 plane; } Region;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set, plane; } Zone;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set; } PalRegion;   // camera tile (top-left) rect -> palette set
typedef struct { u16 x, y; u8 plane; } ShopPos;
extern const u32 world_tiles[WORLD_TILE_COUNT * 8];
extern const u32 meta_tiles[META_COUNT][4];   // low 16: tile id, high 16: flip/palette attrs
// The map: OSRS map squares (64x64 tiles) of each plane, sorted by (plane, rx, ry). A block is
// 16x16 tiles, LZ77-compressed in the GBA BIOS format: meta u16[256], over u16[256], type
// u8[256], roof u8[256] (roof group, 0 none: hidden while the player is under it), cell
// (x & 15, y & 15) at index (y & 15) * 16 + (x & 15). See docs/regions.md.
enum { RF_EDGE = 1, RF_ROOFS = 2, RF_OVERLAY = 4 };   // region flags: partly outside the bounds, has roofs, overlay
typedef struct {
    u8 rx, ry, plane, pal_set;    // map square (x >> 6, y >> 6), floor, palette set of most of its cells
    u8 flags;                     // RF_*
    const u32 *blk[16];           // block (x >> 4 & 3, y >> 4 & 3) at [(y >> 4 & 3) * 4 + (x >> 4 & 3)]; 0 = outside the map
    const u32 *walk;              // 64x64 bits, word (y & 63) * 2 + (x >> 5 & 1), bit x & 31: walkable cells
} MapRegion;
extern const MapRegion map_regions[MAP_REGION_COUNT];
extern const u16 map_bounds[PLANE_COUNT][4];   // x0, y0, x1, y1 of each floor (x0 > x1: no such floor)
extern const u32 res_cell[RES_COUNT];         // sorted keys ((plane << 28) | (y << 14) | x) of trees/rocks
extern const u16 res_alt[RES_COUNT];          // their depleted metatile (stump / empty rock)
extern const u16 world_pal[WORLD_PAL_SETS][14 * 16];
extern const u16 water_slots[WORLD_PAL_SETS][WATER_SLOT_MAX + 1];   // palette indices, 0-terminated
extern const Spawn spawns[SPAWN_COUNT];
extern const FishSpotDef fish_spots[FISHSPOT_COUNT];
extern const Region regions[REGION_COUNT];
extern const ShopPos shop_pos[SHOP_COUNT];      // tile at the centre of each shop's building
extern const Zone zones[ZONE_COUNT];
extern const PalRegion pal_regions[PAL_REGION_COUNT];
''')


def emit_regions(c, arr):
    streams, walks = {}, {}      # identical blocks / bitmaps are stored once
    for (p, rx, ry, ps, fl, blocks, walk) in MAP_REGIONS:
        for b in blocks:
            if b is not None and b not in streams:
                streams[b] = len(streams)
                w = [int.from_bytes(b[i:i + 4], 'little') for i in range(0, len(b), 4)]
                c.append(arr('u32', 'mb_%d[%d]' % (streams[b], len(w)), w, 12, '0x%08X').replace('const u32', 'static const u32'))
        k = tuple(walk)
        if k not in walks:
            walks[k] = len(walks)
            c.append(arr('u32', 'mw_%d[128]' % walks[k], walk, 8, '0x%08X').replace('const u32', 'static const u32'))
    c.append('const MapRegion map_regions[MAP_REGION_COUNT] = {')
    for (p, rx, ry, ps, fl, blocks, walk) in MAP_REGIONS:
        c.append('  {%d, %d, %d, %d, %d, {%s}, mw_%d},' % (rx, ry, p, ps, fl, ', '.join(
            'mb_%d' % streams[b] if b is not None else '0' for b in blocks), walks[tuple(walk)]))
    c.append('};')
    nb = sum(1 for r in MAP_REGIONS for b in r[5] if b is not None)
    print('regions: %d (planes %s), %d blocks (%d stored), %d bytes compressed of %d, %d walk bitmaps' % (
        len(MAP_REGIONS), ','.join(str(p) for p in sorted(BOUNDS)), nb, len(streams), sum(len(b) for b in streams),
        nb * 1536, len(walks)), file=sys.stderr)


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
    emit_regions(c, arr)
    res = sorted(((lambda X, Y, P: (P << 28) | (Y << 14) | X)(*osrs_of(i % MW, i // MW)), m) for i, m in RES_ALT)
    c.append(arr('u32', 'res_cell[RES_COUNT]', [k for k, _ in res], 12, '0x%08X'))
    c.append(arr('u16', 'res_alt[RES_COUNT]', [m for _, m in res], 24, '%d'))
    c.append('const u16 world_pal[WORLD_PAL_SETS][14 * 16] = {')
    for wp in world_pal:
        c.append('  {' + ','.join('0x%04X' % v for v in wp) + '},')
    c.append('};')
    c.append('const u16 water_slots[WORLD_PAL_SETS][WATER_SLOT_MAX + 1] = {')
    for ws in water_slots:
        c.append('  {' + ','.join('%d' % v for v in ws + [0] * (max(len(w) for w in water_slots) + 1 - len(ws))) + '},')
    c.append('};')
    c.append('const Spawn spawns[SPAWN_COUNT] = {')
    for k, x, y in SPAWNS:
        X, Y, P = osrs_of(x, y)
        c.append('  {NPC_%s, %d, %d, %d},' % (k, P, X, Y))
    c.append('};')
    c.append('const FishSpotDef fish_spots[FISHSPOT_COUNT] = {')
    for k, x, y in FISH_SPOTS:
        X, Y, P = osrs_of(x, y)
        c.append('  {%d, %d, %d, %d},' % (0 if k == 'NET' else 1, P, X, Y))
    c.append('};')
    c.append('const ShopPos shop_pos[SHOP_COUNT] = {')
    for n, x, y in SHOPS:
        c.append('  {%d, %d, %d},   // %s' % (osrs_of(x, y) + (n,)))
    c.append('};')
    c.append('const u16 map_bounds[PLANE_COUNT][4] = {')
    for p in range(NPLANES):
        c.append('  {%d, %d, %d, %d},' % BOUNDS.get(p, (1, 0, 0, 0)))
    c.append('};')
    c.append('const Region regions[REGION_COUNT] = {')
    for n, x0, y0, x1, y1 in REGIONS:
        c.append('  {"%s", %d, %d, %d, %d, %d},' % ((n.replace('"', '\\"'),) + osrs_rect(x0, y0, x1, y1)))
    c.append('};')
    c.append('const PalRegion pal_regions[PAL_REGION_COUNT] = {')
    for (x0, y0, x1, y1), ps in PAL_REGIONS:
        X0, Y0, X1, Y1, P = osrs_rect(x0, y0, min(x1, OW_W - 1), min(y1, OW_H - 1))
        assert P == 0
        c.append('  {%d, %d, %d, %d, %d},' % (X0, Y0, X1, Y1, ps))
    c.append('};')
    c.append('const Zone zones[ZONE_COUNT] = {')
    for n, z, ps, pl_ in ZONES:
        X0, Y0, X1, Y1, P = osrs_rect(z.X0, z.Y0, z.X1, z.Y1)
        assert P == pl_
        c.append('  {%d, %d, %d, %d, %d, %d},   // %s' % (X0, Y0, X1, Y1, ps, pl_, n))
    c.append('};')
