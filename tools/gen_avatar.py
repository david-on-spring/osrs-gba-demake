#!/usr/bin/env python3
"""Player avatar art: layered body parts, armour and held items that src/avatar.c composites
into a 32x32 4bpp sprite whenever the player's look changes.

Nothing here lives in OBJ VRAM: layers are ROM tables (one byte per pixel, cropped) and only the
composited result is uploaded. All layers use the player palette bank (OBJ bank 0) slots:

  o outline  s/S skin  h/H head (hair or helm)  t/T body  l/L legs  b feet  a wood/belt
  m/M held item (weapon metal, net, rod line)   n/N shield

Body-part coordinates are relative to the 16x16 human cell (0..15, may spill outside);
held-item layers are relative to their grip pixel.

usage: gen_avatar.py OUTDIR   -> OUTDIR/avatar_art.c, OUTDIR/avatar_art.h
"""
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else 'build'
SLOT = {'.': 0, 'o': 1, 's': 2, 'S': 3, 'h': 4, 'H': 5, 't': 6, 'T': 7, 'l': 8, 'L': 9,
        'b': 10, 'a': 11, 'm': 12, 'M': 13, 'n': 14, 'N': 15, 'g': 11}

LAYERS = []      # (name, x, y, w, h, pixels)
PX = []


def grid(rows):
    return [[SLOT[c] for c in r] for r in rows]


def add_layer(name, pix, x0, y0):
    """pix: 2D list of slots (0 transparent). (x0, y0): position of pix[0][0]. Crops to bbox."""
    pts = [(x, y) for y, r in enumerate(pix) for x, v in enumerate(r) if v]
    if not pts:
        LAYERS.append((name, 0, 0, 0, 0, len(PX)))
        return len(LAYERS) - 1
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    bx, by, ex, ey = min(xs), min(ys), max(xs), max(ys)
    off = len(PX)
    for y in range(by, ey + 1):
        for x in range(bx, ex + 1):
            PX.append(pix[y][x])
    LAYERS.append((name, x0 + bx, y0 + by, ex - bx + 1, ey - by + 1, off))
    return len(LAYERS) - 1


def body(name, rows, y0):
    assert all(len(r) == 16 for r in rows), (name, [len(r) for r in rows])
    return add_layer(name, grid(rows), 0, y0)


# ----------------------------------------------------------------------------------------
# Heads (rows 0-7). Views: 0 = facing down (front), 1 = up (back), 2 = side (facing right)
# ----------------------------------------------------------------------------------------
HEADS = {
    'HAIR': (
        ["................",
         "......oooo......",
         ".....ohhhho.....",
         "....ohhhhhHo....",
         "....ohhsshHo....",
         "....osossoso....",
         "....oSssssSo....",
         ".....oSSSSo....."],
        ["................",
         "......oooo......",
         ".....ohhhho.....",
         "....ohhhhhho....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....oHhhhHHo....",
         ".....oSSSSo....."],
        ["................",
         "......oooo......",
         ".....ohhhho.....",
         "....ohhhhhho....",
         "....ohhhhsso....",
         "....oHhhssos....",
         "....oHHsssso....",
         ".....oSSSSo....."]),
    'MED': (
        ["................",
         "......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "...ohHHHHHHHo...",
         "....o......o....",
         "................",
         "................"],
        ["................",
         "......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "...ohHHHHHHHo...",
         "....o......o....",
         "................",
         "................"],
        ["................",
         "......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "...ohHHHHHHHo...",
         "....o...........",
         "................",
         "................"]),
    'FULL': (
        ["......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "....ohhhhHHo....",
         "....oHHHHHHo....",
         "....ohooooHo....",
         "....oHhooHHo....",
         ".....oHHHHo....."],
        ["......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....oHhhhHHo....",
         ".....oHHHHo....."],
        ["......oooo......",
         ".....ohhhHo.....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....ohhhhhHo....",
         "....ohhhoooo....",
         "....oHHhhhHo....",
         ".....oHHHHo....."]),
}

# ----------------------------------------------------------------------------------------
# Torsos (rows 8-12), drawn without arms (arms are drawn procedurally by avatar.c)
# ----------------------------------------------------------------------------------------
TORSOS = {
    'SHIRT': (
        ["....otttttto....",
         "....otTttTto....",
         "....otttttto....",
         "....oaaaaaao...."],
        ["....otttttto....",
         "....otttttto....",
         "....otTttTto....",
         "....oaaaaaao...."],
        [".....otttto.....",
         ".....otttTo.....",
         ".....otttTo.....",
         ".....oaaaao....."]),
    'PLATE': (
        ["...ottTTTTtto...",
         "....oTttttTo....",
         "....oTtTTtTo....",
         "....otTTTTto...."],
        ["...ottTTTTtto...",
         "....oTttttTo....",
         "....oTttttTo....",
         "....oTTTTTTo...."],
        ["....otttTTo.....",
         ".....oTttTo.....",
         ".....oTtTTo.....",
         ".....otTTTo....."]),
    'CHAIN': (
        ["....oTttttTo....",
         "....otTtTtTo....",
         "....oTtTtTto....",
         "....otTtTtTo....",
         ".....oTTTTo....."],
        ["....oTttttTo....",
         "....otTtTtTo....",
         "....oTtTtTto....",
         "....otTtTtTo....",
         ".....oTTTTo....."],
        [".....otttTo.....",
         ".....otTtTo.....",
         ".....oTtTto.....",
         ".....otTtTo.....",
         ".....oTTTTo....."]),
    'LEATHER': (
        ["....oTTttTTo....",
         "....otTttTto....",
         "....ottTTtto....",
         "....oaaaaaao...."],
        ["....oTTttTTo....",
         "....otttttto....",
         "....otttttto....",
         "....oaaaaaao...."],
        [".....oTTtto.....",
         ".....otttTo.....",
         ".....otttTo.....",
         ".....oaaaao....."]),
}

# ----------------------------------------------------------------------------------------
# Legs (rows 12-16): [front/back stand, front/back step, side stand, side step]
# ----------------------------------------------------------------------------------------
LEGS = {
    'PANTS': (
        ["....ollllllo....",
         "....olloollo....",
         "....obboobbo....",
         ".....oo..oo....."],
        ["....ollllllo....",
         "....olloollo....",
         "....obboollo....",
         ".....oo.obbo....",
         ".........oo....."],
        [".....ollllo.....",
         ".....ollllo.....",
         ".....obbbbo.....",
         ".....oooooo....."],
        [".....ollllo.....",
         "....olloollo....",
         "...obbo..obbo...",
         "...ooo....ooo..."]),
    'PLATE': (
        ["....ollllllo....",
         "...olLloolLlo...",
         "....obboobbo....",
         ".....oo..oo....."],
        ["....ollllllo....",
         "...olLloolLlo...",
         "....obbooLlo....",
         ".....oo.obbo....",
         ".........oo....."],
        [".....ollllo.....",
         ".....oLllLo.....",
         ".....obbbbo.....",
         ".....oooooo....."],
        [".....ollllo.....",
         "....oLlooLlo....",
         "...obbo..obbo...",
         "...ooo....ooo..."]),
    'SKIRT': (
        ["....ollllllo....",
         "...ollLllLllo...",
         "...oLLLLLLLLo...",
         "....obbo.obbo...",
         ".....oo...oo...."],
        ["....ollllllo....",
         "...ollLllLllo...",
         "...oLLLLLLLLo...",
         "....obbo..ob....",
         ".....oo...obo...",
         "...........o...."],
        [".....ollllo.....",
         "....ollllllo....",
         "....oLLLLLLo....",
         ".....obbbbo.....",
         ".....oooooo....."],
        [".....ollllo.....",
         "....ollllllo....",
         "....oLLLLLLo....",
         "...obbo..obbo...",
         "...ooo....ooo..."]),
    'CHAPS': (
        ["....oLllllLo....",
         "....oLlooLlo....",
         "....obboobbo....",
         ".....oo..oo....."],
        ["....oLllllLo....",
         "....oLlooLlo....",
         "....obbooLlo....",
         ".....oo.obbo....",
         ".........oo....."],
        [".....oLllLo.....",
         ".....olllLo.....",
         ".....obbbbo.....",
         ".....oooooo....."],
        [".....oLllLo.....",
         "....oLlooLlo....",
         "...obbo..obbo...",
         "...ooo....ooo..."]),
}


def with_boots(rows):
    """Boots: raise the outer edge of each foot one row as a cuff over the shin."""
    g = [list(r) for r in rows]
    for y in range(1, len(g)):
        for x in range(1, 15):
            if rows[y][x] != 'b' or rows[y - 1][x] not in 'lL':
                continue
            outer = rows[y][x - 1] == 'o' if x < 8 else rows[y][x + 1] == 'o'
            if outer:
                g[y - 1][x] = 'b'
    return [''.join(r) for r in g]


# ----------------------------------------------------------------------------------------
# Shields: face art (seen from the front) and edge art (seen side-on)
# placements: 0 front view (left arm, screen right, in front), 1 back view (screen left, behind
# body), 2 side near arm (face, in front of torso), 3 side far arm (edge, behind body)
# ----------------------------------------------------------------------------------------
SHIELDS = {
    'SQ': (["nnnnN",
            "nnnnN",
            "nnNnN",
            "nnnnN",
            "nnnnN",
            "NNNNN"],
           ["nN", "nN", "nN", "nN", "nN", "NN"]),
    'KITE': (["nnnnN",
              "nnNnN",
              "nNNNN",
              "nnNnN",
              ".nNN.",
              ".nnN.",
              "..N.."],
             ["nN", "nN", "nN", "nN", "nN", ".N", ".N"]),
    'ROUND': ([".nnN.",
               "nnnnN",
               "nnNnN",
               "nnnNN",
               ".NNN."],
              ["nN", "nN", "nN", "nN", "NN"]),
}
SHIELD_POS = {  # (x, y) of the face/edge art per placement and type
    'SQ': ((11, 8), (0, 8), (5, 8), (10, 8)),
    'KITE': ((11, 8), (0, 8), (5, 8), (10, 8)),
    'ROUND': ((11, 9), (0, 9), (5, 9), (10, 9)),
}


def outline(pix):
    """Add a 1px 4-neighbour outline around filled pixels (grows the grid by 1 on each side)."""
    h, w = len(pix), len(pix[0])
    g = [[0] * (w + 2) for _ in range(h + 2)]
    for y in range(h):
        for x in range(w):
            g[y + 1][x + 1] = pix[y][x]
    o = [r[:] for r in g]
    for y in range(h + 2):
        for x in range(w + 2):
            if g[y][x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w + 2 and 0 <= yy < h + 2 and g[yy][xx]:
                    o[y][x] = 1
                    break
    return o


# ----------------------------------------------------------------------------------------
# Held items. Drawn pointing up (N) with grip 'g'; NE art is optional (else derived by a 45
# degree pixel rotation). All 8 directions come from 90-degree rotations of these two.
# ----------------------------------------------------------------------------------------
WEAPONS = {
    'SWORD': ([".m.",
               ".m.",
               ".m.",
               ".m.",
               ".m.",
               "MMM",
               ".g.",
               ".M."],
              ["......m",
               ".....m.",
               "....m..",
               "...m...",
               "M.m....",
               ".M.....",
               "g.M....",
               ], True),
    'LONG': ([".m.",
              ".m.",
              ".m.",
              ".m.",
              ".m.",
              ".m.",
              ".m.",
              "MMM",
              ".g.",
              ".M."],
             ["........m",
              ".......m.",
              "......m..",
              ".....m...",
              "....m....",
              "...m.....",
              "M.m......",
              ".M.......",
              "g.M......",
              ], True),
    'SCIM': (["..m",
              ".mM",
              ".mM",
              "mM.",
              "mM.",
              "m..",
              "MMM",
              ".g.",
              ".M."],
             ["......m",
              ".....mm",
              "....mM.",
              "...mM..",
              "M.m....",
              ".M.....",
              "g.M....",
              ], True),
    'DAGGER': ([".m.",
                ".m.",
                ".m.",
                "MMM",
                ".g.",
                ".M."],
               ["....m",
                "...m.",
                "M.m..",
                ".M...",
                "g.M..",
                ], True),
    'MACE': (["mmM",
              "mMM",
              ".M.",
              ".a.",
              ".a.",
              ".a.",
              ".g.",
              ".M."],
             ["...mM",
              "..mmM",
              "..aM.",
              ".a...",
              "a....",
              "g....",
              ], True),
    'AXE': ([".M..",
             ".Mmm",
             ".Mmm",
             ".Mm.",
             ".a..",
             ".a..",
             ".a..",
             ".g.."],
            ["..mm..",
             ".Mmmm.",
             "..Mmm.",
             ".a.M..",
             "a.....",
             "g.....",
             ], True),
    'PICK': (["..mmM..",
              ".m.a.M.",
              "m..a..M",
              "...a...",
              "...a...",
              "...a...",
              "...g..."],
             ["..mmm.",
              ".m..am",
              "...a.m",
              "..a..M",
              ".a....",
              "g.....",
              ], True),
    'NET': ([".MMM.",
             "Mm.mM",
             "M.m.M",
             "Mm.mM",
             ".MMM.",
             "..a..",
             "..a..",
             "..a..",
             "..g.."],
            ["..MMM.",
             ".Mm.mM",
             ".M.m.M",
             ".Mm.mM",
             "a.MMM.",
             "a.....",
             "g.....",
             ], False),
    'ROD': (["a",
             "a",
             "a",
             "a",
             "a",
             "a",
             "a",
             "a",
             "M",
             "g"],
            ["......a",
             ".....a.",
             "....a..",
             "...a...",
             "..a....",
             ".M.....",
             "g......",
             ], False),
    # bow held upright by the middle of the stave, the string on the far side
    'BOW': ([".m.",
             "m.o",
             "m.o",
             "g.o",
             "m.o",
             "m.o",
             ".m."],
            None, False),
    # the spellbook read during the home teleport
    'BOOK': (["MMM",
              "Mmm",
              "Mmm",
              "MgM"],
             None, True),
    'STAFF': ([".M.",
               "MmM",
               ".M.",
               ".a.",
               ".a.",
               ".a.",
               ".a.",
               ".a.",
               ".g.",
               ".a."],
              None, True),
}


def find_grip(rows):
    for y, r in enumerate(rows):
        x = r.find('g')
        if x >= 0:
            return x, y
    raise ValueError(rows)


def rot45(pix, gx, gy):
    """45-degree clockwise pixel rotation around the grip: (x, y) -> (x - y, x + y), then holes
    with 3+ filled orthogonal neighbours are filled with the most common neighbour."""
    pts = {}
    for y, r in enumerate(pix):
        for x, v in enumerate(r):
            if v:
                dx, dy = x - gx, y - gy
                pts[(dx - dy, dx + dy)] = v
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, y0 = min(xs), min(ys)
    w, h = max(xs) - x0 + 1, max(ys) - y0 + 1
    g = [[0] * w for _ in range(h)]
    for (x, y), v in pts.items():
        g[y - y0][x - x0] = v
    for y in range(h):
        for x in range(w):
            if g[y][x]:
                continue
            nb = [g[yy][xx] for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                  if 0 <= xx < w and 0 <= yy < h and g[yy][xx]]
            if len(nb) >= 3:
                g[y][x] = max(set(nb), key=nb.count)
    return g, -x0, -y0


def rot90(pix, gx, gy):
    """Rotate 90 degrees clockwise; returns (pix, gx, gy)."""
    h, w = len(pix), len(pix[0])
    g = [[pix[h - 1 - x][y] for x in range(h)] for y in range(w)]
    return g, h - 1 - gy, gx


def tip_of(pix, gx, gy):
    """Farthest filled pixel from the grip (used for fishing line anchoring)."""
    best, bd = (gx, gy), -1
    for y, r in enumerate(pix):
        for x, v in enumerate(r):
            if v and v != 1:
                d = (x - gx) ** 2 + (y - gy) ** 2
                if d > bd:
                    best, bd = (x, y), d
    return best[0] - gx, best[1] - gy


WEAPON_ORDER = ['SWORD', 'LONG', 'SCIM', 'DAGGER', 'MACE', 'AXE', 'PICK', 'NET', 'ROD', 'BOW', 'STAFF', 'BOOK']
wpn_tab = []     # [type][angle] -> layer
tip_tab = []
for wname in WEAPON_ORDER:
    n_rows, ne_rows, do_outline = WEAPONS[wname]
    gx, gy = find_grip(n_rows)
    npix = grid(n_rows)
    if ne_rows:
        ex, ey = find_grip(ne_rows)
        ne = (grid(ne_rows), ex, ey)
    else:
        ne = rot45(npix, gx, gy)
    base = [(npix, gx, gy), ne]
    angles = [None] * 8
    for k in range(4):
        for j in range(2):
            p, x, y = base[j]
            for _ in range(k):
                p, x, y = rot90(p, x, y)
            angles[k * 2 + j] = (p, x, y)
    row, trow = [], []
    for a, (p, x, y) in enumerate(angles):
        tx, ty = tip_of(p, x, y)
        if do_outline:
            p = outline(p)
            x, y = x + 1, y + 1
        row.append(add_layer('W_%s_%d' % (wname, a), p, -x, -y))
        trow.append((tx, ty))
    wpn_tab.append(row)
    tip_tab.append(trow)

# ----------------------------------------------------------------------------------------
HEAD_ORDER = ['HAIR', 'MED', 'FULL']
TORSO_ORDER = ['SHIRT', 'PLATE', 'CHAIN', 'LEATHER']
LEG_ORDER = ['PANTS', 'PLATE', 'SKIRT', 'CHAPS']
SHIELD_ORDER = ['SQ', 'KITE', 'ROUND']

head_tab = [[body('H_%s_%d' % (n, v), HEADS[n][v], 0) for v in range(3)] for n in HEAD_ORDER]
torso_tab = [[body('T_%s_%d' % (n, v), TORSOS[n][v], 8) for v in range(3)] for n in TORSO_ORDER]
# legs[type][boots][frame] frame: 0 front stand, 1 front step, 2 side stand, 3 side step
legs_tab = []
for n in LEG_ORDER:
    per = []
    for boots in range(2):
        fr = []
        for f in range(4):
            rows = LEGS[n][f]
            if boots and n != 'SKIRT':
                rows = with_boots(rows)
            fr.append(body('L_%s_%d_%d' % (n, boots, f), rows, 12))
        per.append(fr)
    legs_tab.append(per)
shield_tab = []
for n in SHIELD_ORDER:
    face, edge = SHIELDS[n]
    fp = outline(grid(face))
    ep = outline(grid(edge))
    pos = SHIELD_POS[n]
    row = [add_layer('S_%s_0' % n, fp, pos[0][0] - 1, pos[0][1] - 1),
           add_layer('S_%s_1' % n, fp, pos[1][0] - 1, pos[1][1] - 1),
           add_layer('S_%s_2' % n, fp, pos[2][0] - 1, pos[2][1] - 1),
           add_layer('S_%s_3' % n, ep, pos[3][0] - 1, pos[3][1] - 1)]
    shield_tab.append(row)

# ----------------------------------------------------------------------------------------
# Emit
# ----------------------------------------------------------------------------------------
os.makedirs(OUT, exist_ok=True)
h = ['// generated by tools/gen_avatar.py - do not edit', '#pragma once', '#include "gba.h"', '',
     'typedef struct { s8 x, y; u8 w, h; u16 off; } AvLayer;']
for pre, order in (('AVH', HEAD_ORDER), ('AVT', TORSO_ORDER), ('AVG', LEG_ORDER),
                   ('AVS', SHIELD_ORDER), ('AVW', WEAPON_ORDER)):
    for i, n in enumerate(order):
        h.append('#define %s_%s %d' % (pre, n, i))
    h.append('#define %s_COUNT %d' % (pre, len(order)))
h.append('#define AVL_COUNT %d' % len(LAYERS))
h.append('''extern const AvLayer av_layers[AVL_COUNT];
extern const u8 av_px[];
extern const u16 av_head[AVH_COUNT][3];
extern const u16 av_torso[AVT_COUNT][3];
extern const u16 av_legs[AVG_COUNT][2][4];
extern const u16 av_shield[AVS_COUNT][4];
extern const u16 av_wpn[AVW_COUNT][8];
extern const s8 av_tip[AVW_COUNT][8][2];
''')
c = ['// generated by tools/gen_avatar.py - do not edit', '#include "avatar_art.h"', '']
c.append('const AvLayer av_layers[AVL_COUNT] = {')
for name, x, y, w, hh, off in LAYERS:
    c.append('  {%d, %d, %d, %d, %d},  // %s' % (x, y, w, hh, off, name))
c.append('};')
c.append('const u8 av_px[%d] = {' % max(1, len(PX)))
for i in range(0, len(PX), 32):
    c.append('  ' + ','.join(str(v) for v in PX[i:i + 32]) + ',')
c.append('};')


def tab2(name, t):
    c.append('const u16 %s = {' % name)
    for r in t:
        c.append('  {' + ', '.join(str(v) for v in r) + '},')
    c.append('};')


tab2('av_head[AVH_COUNT][3]', head_tab)
tab2('av_torso[AVT_COUNT][3]', torso_tab)
c.append('const u16 av_legs[AVG_COUNT][2][4] = {')
for per in legs_tab:
    c.append('  {' + ', '.join('{' + ', '.join(str(v) for v in fr) + '}' for fr in per) + '},')
c.append('};')
tab2('av_shield[AVS_COUNT][4]', shield_tab)
tab2('av_wpn[AVW_COUNT][8]', wpn_tab)
c.append('const s8 av_tip[AVW_COUNT][8][2] = {')
for r in tip_tab:
    c.append('  {' + ', '.join('{%d, %d}' % t for t in r) + '},')
c.append('};')
with open(os.path.join(OUT, 'avatar_art.h'), 'w') as f:
    f.write('\n'.join(h) + '\n')
with open(os.path.join(OUT, 'avatar_art.c'), 'w') as f:
    f.write('\n'.join(c) + '\n')
print('avatar: %d layers, %d bytes' % (len(LAYERS), len(PX)), file=sys.stderr)

if os.environ.get('AVPREVIEW'):
    # ASCII dump of every layer for quick inspection
    inv = {v: k for k, v in SLOT.items() if k != 'g'}
    for name, x, y, w, hh, off in LAYERS:
        if not name.startswith(os.environ['AVPREVIEW']):
            continue
        print(name, x, y, file=sys.stderr)
        for r in range(hh):
            print('  ' + ''.join(inv[PX[off + r * w + k]] for k in range(w)), file=sys.stderr)
