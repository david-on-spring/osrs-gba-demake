#!/usr/bin/env python3
"""Turn OSRS world-map tiles into a tile-accurate text layout for a 1:1 zone.

usage: ref_extract.py fetch                         download the reference tiles into build/ref/
       ref_extract.py NAME X0 Y0 X1 Y1 [PLANE]      write tools/zones/NAME.ref for that OSRS rectangle

The reference is the OSRS Wiki's rendered world map (maps.runescape.wiki, zoom 3: 8 pixels per tile,
32x32 tiles per image). Only the layout is kept, never pixels: what each tile's ground is, where the
walls, doors and fences run along tile edges, and the map's icons (trees, rocks, ladders). The .ref
files are committed and may be edited by hand; tools/world_gen.py builds the zones from them, so the
images are not needed to build the game.

.ref format (rows run north to south; all rows of a section have the same length):
  ref X0 Y0 X1 Y1               the OSRS tiles covered
  G <row>                        ground per tile:  g grass  p dirt path  c road / paving  f stone floor
                                 w wood floor  k chequered floor  ~ water  s sand  m swamp  # thick black
                                 wall  ? unknown
                                 (floors are tiles enclosed by walls; everything reachable from the
                                 edge of the rectangle without crossing a wall is outdoors)
  N <row>                        edge on the north side of each tile, plus one closing row:
                                 . none  - wall  d door  o doorway (a gap in a wall line)
  W <row>                        edge on the west side of each tile, plus one closing column:
                                 . none  | wall  d door  o doorway
  O <row>                        icons: . none  T tree  D dead tree  R rock  C crops  * fern / plant
  X <row>                        / a diagonal wall crosses the tile
"""
import os
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sheet import read_png  # noqa: E402

REF = os.path.join(ROOT, 'build', 'ref')
URL = 'https://maps.runescape.wiki/osrs/tiles/0_2019-10-31_1/3/%d_%d_%d.png'
_cache = {}


def image(plane, tx, ty):
    k = (plane, tx, ty)
    if k not in _cache:
        p = os.path.join(REF, '%d_%d_%d.png' % k)
        _cache[k] = read_png(p)[2] if os.path.exists(p) else None
    return _cache[k]


def tile_px(plane, X, Y):
    r = image(plane, X // 32, Y // 32)
    if r is None:
        return None
    oy, ox = (31 - (Y % 32)) * 8, (X % 32) * 8
    return [[tuple(r[oy + j][(ox + i) * 3:(ox + i) * 3 + 3]) for i in range(8)] for j in range(8)]


def white(c):
    return min(c) > 200


def red(c):
    return c[0] > 150 and c[1] < 90 and c[2] < 90


def black(c):
    return max(c) < 14


def tree_px(c):
    r, g, b = c
    return g > 40 and g > r * 1.45 and g > b * 1.6


def trunk_px(c):
    r, g, b = c
    return r > g > b and r - b > 30 and max(c) < 0x60 and g < r * 0.85


def colour_class(c):
    r, g, b = c
    v = max(c)
    if b > r + 18 and b > g:
        return 'W'                                         # water
    if b > r + 4 and g > r + 10 and v < 0x80:
        return 'M'                                         # swamp (teal)
    if abs(r - g) < 14 and abs(g - b) < 16 and abs(r - b) < 18:
        return 'S'                                         # grey: road or stone floor
    if r > 0x90 and g > 0x80 and b < g * 0.8:
        return 'Y'                                         # sand
    if g >= r - 2 and g > b:
        return 'G'                                         # grass greens and olives
    if r > g > b and v >= 0x60:
        return 'T'                                         # tan dirt
    if r > g > b and g < r * 0.86:
        return 'B'                                         # brown (wood)
    return 'T' if v >= 0x58 else 'G'


def classify(t):
    """(colour class, dominant colour, icon) for one tile's pixels."""
    flat = [c for r_ in t for c in r_]
    if sum(black(c) for c in flat) > 40:
        return 'K', (0, 0, 0), '.'
    cnt = Counter(c for c in flat if not (white(c) or red(c) or black(c)))
    if not cnt:
        return '?', (0, 0, 0), '.'
    dom = cnt.most_common(1)[0][0]
    other = [c for c in flat if c != dom and not (white(c) or red(c) or black(c))]
    trees = sum(tree_px(c) and colour_class(c) == 'G' and c[1] > dom[1] + 12 for c in other)
    trunks = sum(trunk_px(c) and abs(c[0] - dom[0]) > 16 for c in other)
    greys = sum(colour_class(c) == 'S' and max(c) > 0x60 and abs(max(c) - max(dom)) > 18 for c in other)
    crops = sum(c[0] > 0xb0 and 0x50 < c[1] < 0xb0 and c[2] < 0x60 for c in flat)
    ferns = sum(c[1] > dom[1] + 20 and c[1] > c[0] and abs(c[0] - c[2]) < 0x30 and not tree_px(c) for c in other)
    icon = 'C' if crops >= 12 else 'T' if trees >= 8 else 'D' if trunks >= 10 and colour_class(dom) == 'G' else \
        'R' if greys >= 10 else '*' if ferns >= 4 else '.'
    return colour_class(dom), dom, icon


def extract(plane, X0, Y0, X1, Y1):
    W, H = X1 - X0 + 1, Y1 - Y0 + 1
    px = {}
    for Y in range(Y0 - 1, Y1 + 2):
        for X in range(X0 - 1, X1 + 2):
            px[(X, Y)] = tile_px(plane, X, Y)
    cls, icon, diag, dom_of = {}, {}, {}, {}
    N, Wd = {}, {}               # edges: N[(col,row)] north side of tile, Wd[(col,row)] west side
    for row in range(H + 1):
        Y = Y1 - row
        for col in range(W + 1):
            X = X0 + col
            t, up, left = px.get((X, Y)), px.get((X, Y + 1)), px.get((X - 1, Y))
            if col < W and row < H:
                if t is None:
                    cls[(col, row)], icon[(col, row)], diag[(col, row)] = '?', '.', False
                    dom_of[(col, row)] = None
                else:
                    c, dom, ic = classify(t)
                    cls[(col, row)], icon[(col, row)] = c, ic
                    dom_of[(col, row)] = dom
                    diag[(col, row)] = sum(white(t[j][i]) for j in range(1, 7) for i in range(1, 7)) >= 4
            if col < W:
                a, b = (t[0] if t else []), (up[7] if up else [])
                nr = sum(red(c) for c in list(a) + list(b))
                nw = max(sum(white(c) for c in a), sum(white(c) for c in b))
                N[(col, row)] = 'd' if nr >= 3 else '-' if nw >= 5 else '.'
            if row < H:
                a = [t[j][0] for j in range(8)] if t else []
                b = [left[j][7] for j in range(8)] if left else []
                nr = sum(red(c) for c in a + b)
                nw = max(sum(white(c) for c in a), sum(white(c) for c in b))
                Wd[(col, row)] = 'd' if nr >= 3 else '|' if nw >= 5 else '.'
    # doorways: a gap of one or two edges in a straight wall line counts as closed (an opening)
    for (E, along, wallc) in ((N, (1, 0), '-'), (Wd, (0, 1), '|')):
        for k, v in list(E.items()):
            if v != '.':
                continue
            for run in (1, 2, 3):
                before = (k[0] - along[0], k[1] - along[1])
                after = (k[0] + along[0] * run, k[1] + along[1] * run)
                gap = [(k[0] + along[0] * i, k[1] + along[1] * i) for i in range(run)]
                if all(E.get(g_) == '.' for g_ in gap) and E.get(before) in (wallc, 'd') and E.get(after) in (wallc, 'd'):
                    for g_ in gap:
                        E[g_] = 'o'
                    break
    # indoors: regions of one exact ground colour, joined across edges without walls, that are
    # mostly bounded by walls, doors and doorways (roads touch grass, floors touch walls). An
    # enclosed patch of the colour around it is a walled yard, not a room.
    blocked = lambda e: e in ('-', '|', 'd', 'o')
    edge_of = lambda c, r, dc, dr: (N.get((c, r)) if dr < 0 else N.get((c, r + 1)) if dr > 0 else
                                    Wd.get((c, r)) if dc < 0 else Wd.get((c + 1, r)))
    comp_of = {}
    comps = []
    for t in cls:
        if t in comp_of:
            continue
        k = len(comps)
        comp, stack = [t], [t]
        comp_of[t] = k
        while stack:
            c, r = stack.pop()
            for (dc, dr) in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                n = (c + dc, r + dr)
                if n in cls and n not in comp_of and dom_of[n] == dom_of[(c, r)] and not blocked(edge_of(c, r, dc, dr)) \
                        and not diag[n] and not diag[(c, r)]:
                    comp_of[n] = k
                    comp.append(n)
                    stack.append(n)
        comps.append(comp)
    # chequered floors make one-tile regions: join tiny regions of the same class that touch across
    # open edges into one
    par = list(range(len(comps)))

    def fnd(i):
        while par[i] != i:
            par[i] = par[par[i]]
            i = par[i]
        return i
    for k, comp in enumerate(comps):
        if len(comp) > 2:
            continue                # (only one-or-two-tile regions start a merge)
        for (c, r) in comp:
            for (dc, dr) in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                n = (c + dc, r + dr)
                if n in cls and len(comps[comp_of[n]]) <= 12 and cls[n] not in 'GWK' and cls[(c, r)] not in 'GWK' and \
                        not blocked(edge_of(c, r, dc, dr)) and not diag[n]:
                    par[fnd(k)] = fnd(comp_of[n])
    merged_ = {}
    for k, comp in enumerate(comps):
        merged_.setdefault(fnd(k), []).extend(comp)
    comps = list(merged_.values())
    comp_of = {t: k for k, comp in enumerate(comps) for t in comp}
    out = set()
    checker = set()
    for k, comp in enumerate(comps):
        per = walled = 0
        across = Counter()
        border = False
        for (c, r) in comp:
            for (dc, dr) in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                n = (c + dc, r + dr)
                if n not in cls:
                    border = True
                    continue
                if comp_of[n] == k:
                    continue
                per += 1
                if blocked(edge_of(c, r, dc, dr)) or diag[n] or cls[n] == 'K':
                    walled += 1
                else:
                    across[cls[n]] += 1
        mine = Counter(cls[t_] for t_ in comp).most_common(1)[0][0]
        if len(set(dom_of[t_] for t_ in comp)) >= 2 and len(comp) > 8:
            checker.update(comp)
        indoor = per > 0 and walled >= per * 0.6 and len(comp) <= 400 and mine not in 'WK' and not (border and len(comp) > 40)
        if indoor and mine in 'GM':
            # a green/tan/sand patch is a floor only if it differs from the ground outside its walls
            outside = Counter()
            for (c, r) in comp:
                for (dc, dr) in ((0, -1), (0, 1), (-1, 0), (1, 0), (0, -2), (0, 2), (-2, 0), (2, 0)):
                    n = (c + dc, r + dr)
                    if n in cls and comp_of[n] != k:
                        outside[cls[n]] += 1
            if outside and outside.most_common(1)[0][0] == mine:
                indoor = False
        if not indoor:
            out.update(comp)
    G, O, D = [], [], []
    for r in range(H):
        g = ''
        for c in range(W):
            k = cls[(c, r)]
            if k == 'K':
                g += '#'
            elif (c, r) in out:
                g += {'G': 'g', 'T': 'p', 'S': 'c', 'B': 'p', 'W': '~', 'Y': 's', 'M': 'm'}.get(k, '?')
            else:
                g += 'k' if (c, r) in checker else 'w' if k == 'B' else '~' if k == 'W' else 'f'
        G.append(g)
        O.append(''.join(icon[(c, r)] for c in range(W)))
        D.append(''.join('/' if diag[(c, r)] else '.' for c in range(W)))
    Nr = [''.join(N[(c, r)] for c in range(W)) for r in range(H + 1)]
    Wr = [''.join(Wd[(c, r)] for c in range(W + 1)) for r in range(H)]
    return G, Nr, Wr, O, D


def write(name, plane, X0, Y0, X1, Y1):
    G, N, Wl, O, D = extract(plane, X0, Y0, X1, Y1)
    os.makedirs(os.path.join(ROOT, 'tools', 'zones'), exist_ok=True)
    out = os.path.join(ROOT, 'tools', 'zones', name + '.ref')
    with open(out, 'w') as f:
        f.write('# %s: layout traced from the OSRS world map by tools/ref_extract.py (edit freely)\n' % name)
        f.write('ref %d %d %d %d\n' % (X0, Y0, X1, Y1))
        for k, rows in (('G', G), ('N', N), ('W', Wl), ('O', O), ('X', D)):
            for r_ in rows:
                f.write('%s %s\n' % (k, r_))
    print('wrote', out)


def fetch(tiles):
    import urllib.request
    os.makedirs(REF, exist_ok=True)
    for (p, x, y) in tiles:
        dst = os.path.join(REF, '%d_%d_%d.png' % (p, x, y))
        if not os.path.exists(dst):
            try:
                # the tile server refuses urllib's default user agent (403)
                req = urllib.request.Request(URL % (p, x, y), headers={'User-Agent': 'Mozilla/5.0 osrs-gba-demake ref'})
                with urllib.request.urlopen(req) as r_, open(dst, 'wb') as f:
                    f.write(r_.read())
            except Exception as e:
                print('missing', p, x, y, e)


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == 'fetch':
        fetch([(p, x, y) for p in range(3) for x in range(91, 105) for y in range(97, 111)])
    else:
        write(a[0], int(a[5]) if len(a) > 5 else 0, *map(int, a[1:5]))
