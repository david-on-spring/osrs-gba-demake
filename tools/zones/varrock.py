# Varrock at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
VARROCK = city('VARROCK', (3175, 3380, 3290, 3505), keep=[(3144, 3468, 3185, 3512)])   # the Grand Exchange
set_ctx(VARROCK.ctx)
B = build_from_ref(load_ref('varrock'), dict(stone='WALL', wood='WALL', road='COBBLE', fence='FENCE', city='WALL',
                                             roof_stone='slate', roof_wood='slate', oaks=True, exact=True))
# banks
bank(building_at(B, 3185, 3440), 'W')                      # west bank
eb = stamp(B, 3250, 3416, 3257, 3424, 'WALL', 'FLOOR', doors=[(3253, 3424), (3254, 3424)])   # east bank
service(orect(3250, 3416, 3257, 3424), 'S', 'BOOTH', 'BANKER')
# shops
zshop('VARROCK', building_at(B, 3216, 3415), 'SHOPKEEPER')
zshop('SWORDSHOP', building_at(B, 3205, 3399), 'SWORD_KEEPER')
zshop('HORVIK', building_at(B, 3229, 3437), 'HORVIK')
zshop('LOWE', building_at(B, 3232, 3421), 'LOWE')
zshop('ZAFF', building_at(B, 3202, 3433), 'ZAFF')
zshop('AUBURY', building_at(B, 3253, 3402), 'AUBURY')     # his quest script opens the shop
# Varrock Square fountain: four pools around the statues
for X in range(3210, 3214):
    for Y in range(3427, 3431):
        zput(X, Y, 'VFOUNTAIN')
# Gypsy Aris's striped tent west of the square
zfill(3200, 3421, 3206, 3427, 'GRASS')
tent = stamp(B, 3202, 3422, 3205, 3425, 'HWALL', 'RUG', doors=[(3203, 3422)], roof='tent')
# the church (its floor on the OSRS map): the nave from the south doors, an alcove in the north-west
# corner and the altar room in the north-east, through a door
church = stamp(B, 3252, 3471, 3259, 3488, 'WALL', 'FLOOR', doors=[(3255, 3471), (3256, 3471)], roof='slate')
for X in range(3256, 3260):
    if X != 3258:
        EDGES[ekey(cx(X), cy(3484), 0, -1)] = ('wall', 'WALL')
EDGES[ekey(cx(3258), cy(3484), 0, -1)] = ('door', 'WALL')
for Y in range(3485, 3489):
    EDGES[ekey(cx(3256), cy(Y), -1, 0)] = ('wall', 'WALL')
zfill(3255, 3472, 3256, 3483, 'CARPET')
zput(3257, 3488, 'ALTAR')
zput(3258, 3488, 'ALTAR')
for Y in (3474, 3476, 3478, 3480, 3482):
    for X in (3253, 3254, 3257, 3258):
        zput(X, Y, 'PEW')
zput(3256, 3488, 'POTPLANT')
zput(3259, 3488, 'POTPLANT')
zput(3252, 3488, 'BOOKCASE')
# the palace: throne room
palace = building_at(B, 3222, 3488)
furnish(palace, ['THRONE', 'TABLE', 'TABLE', 'CHAIR', 'CHAIR', 'BOOKCASE', 'POTPLANT', 'POTPLANT', 'DRAWERS'])
# the Blue Moon Inn: bar, tables and barrels
inn = building_at(B, 3225, 3398)
furnish(inn, ['COUNTER', 'COUNTER', 'COUNTER', 'BARREL', 'BARREL', 'TABLE', 'CHAIR', 'CHAIR', 'TABLE', 'CHAIR',
              'CHAIR', 'FIREPLACE'])
# the anvils west of the square (placed by world_gen.forge())
smith = building_at(B, 3188, 3424)
furnish(smith, ['BARREL', 'CRATE'])
# the museum
museum = building_at(B, 3258, 3448)
furnish(museum, ['BOOKCASE', 'BOOKCASE', 'STATUE', 'STATUE', 'SHELF', 'SHELF', 'POTPLANT'])
for b in B:
    if not b['special']:
        furnish(b)
# where the Varrock Teleport spell lands
QPOS['TELE_VARROCK'] = nearest_walk(cx(3212), cy(3424), walk_ok, 4)

# ---- Varrock Palace above the ground: OSRS floors 1 and 2 and the tops of two towers -------------
# Each is a zone of its own (planes 6, 7, 8, one zone per plane) laid from tools/zones/
# varrock_palace<N>.ref: the walls and doors traced from the OSRS map, the floors set by hand.
# Outside the palace the player looks down onto the ground floor.
ZV = [Ctx(3199, 3501, 1, 212 + 34 * i, ZONE_Y, 212 + 34 * i, ZONE_Y, 241 + 34 * i, ZONE_Y + 35) for i in range(3)]
for _i, _z in enumerate(ZV):
    ZONES.append(('PALACE_%d' % (_i + 1), _z, 'VARROCK', 6 + _i))
    ZONE_THEMES.append((_z, 'VARROCK'))


def palace_floor(name, ground):
    ref = load_ref(name)
    X0, Y0, X1, Y1 = ref['rect']
    # (a tile a diagonal wall crosses is left out: towers keep their octagons)
    floor = lambda X, Y: X0 <= X <= X1 and Y0 <= Y <= Y1 and ref['G'][Y1 - Y][X - X0] in 'fwk' and \
        ref['X'][Y1 - Y][X - X0] != '/'
    for y in YR():
        for x in XR():
            TY[y][x] = GR[y][x] = 'VOID'
            X, Y = CTX.OX + (x - CTX.CX0), CTX.OY - (y - CTX.CY0)
            VIEW[(x, y)] = (OW.CX0 + X - OW.OX, OW.CY0 + OW.OY - Y)
    for Y in range(Y0, Y1 + 1):
        for X in range(X0, X1 + 1):
            if not floor(X, Y):
                continue
            zset(X, Y, ground, ground)
            del VIEW[zc(X, Y)]
            # (u, v): OSRS direction; the map's mark on that edge of the tile
            for (u, v, e) in ((0, 1, ref['N'][Y1 - Y][X - X0]), (0, -1, ref['N'][Y1 - Y + 1][X - X0]),
                              (-1, 0, ref['W'][Y1 - Y][X - X0]), (1, 0, ref['W'][Y1 - Y][X - X0 + 1])):
                if floor(X + u, Y + v):
                    kind = 'wall' if e in '-|' else 'door' if e == 'd' else None
                else:
                    kind = 'door' if e == 'd' else 'wall'       # the outer walls are closed
                if kind:
                    x, y = zc(X, Y)
                    EDGES[ekey(x, y, u, -v)] = (kind, 'WALL')


# staircases (2x2 or 1x2, OSRS tiles) and ladders, with the floors each joins; the ground floor's
# are in the palace above
PAL_STAIRS = {'NW': ((3202, 3496, 3203, 3497), (0, 1, 2)), 'NE': ((3218, 3495, 3219, 3496), (0, 1)),
              'C': ((3212, 3473, 3213, 3474), (0, 1)), 'SE': ((3225, 3471, 3225, 3472), (1, 2))}
PAL_LADDERS = {'NWL': ((3206, 3497), (2, 3)), 'SEL': ((3222, 3472), (2, 3))}
PAL_CTX = [VARROCK.ctx] + ZV
for _i, _z in enumerate(ZV):
    set_ctx(_z)
    palace_floor('varrock_palace%d' % (_i + 1), 'FLOOR' if _i == 0 else 'WOODFLOOR')
_pal_cells = {}         # (key, floor) -> cells
for _k, ((_x0, _y0, _x1, _y1), _fls) in PAL_STAIRS.items():
    for _f in _fls:
        set_ctx(PAL_CTX[_f])
        _pal_cells[(_k, _f)] = [zc(_X, _Y) for _X in range(_x0, _x1 + 1) for _Y in range(_y0, _y1 + 1)]
for _k, ((_X, _Y), _fls) in PAL_LADDERS.items():
    for _f in _fls:
        set_ctx(PAL_CTX[_f])
        _pal_cells[(_k, _f)] = [zc(_X, _Y)]
for (_k, _f), _cs in _pal_cells.items():
    for (_x, _y) in _cs:
        TY[_y][_x] = 'LADDER' if _k in PAL_LADDERS else 'STAIRS'
        if GR[_y][_x] not in INTERIOR_G:
            GR[_y][_x] = 'FLOOR'        # (the map's stair icons read as paths outdoors)
        if _f == 0 and not ROOF[_y][_x]:
            _g = next((ROOF[_y + v][_x + u] for (u, v) in D4 if ROOF[_y + v][_x + u]), 0)
            if _g:
                ROOF[_y][_x] = _g
                ROOFS[_g][1].add((_x, _y))
        # the map draws a staircase's rails and a ladder's hatch as walls: they would shut it in
        for (u, v) in D4:
            if (_x + u, _y + v) not in _cs:
                for _E in (EDGES, TRACE_EDGES):
                    _E.pop(ekey(_x, _y, u, v), None)
        if _f:
            for (_a, _b) in [(_x + u, _y + v) for u in (-1, 0, 1) for v in (-1, 0, 1)]:
                for (u, v) in D4:
                    if GR[_b][_a] != 'VOID' and GR[_b + v][_a + u] != 'VOID':
                        EDGES.pop(ekey(_a, _b, u, v), None)


def palace_stairs():
    """Where each staircase lands (the free floor tile beside it nearest the palace's middle), its
    named cells for the stair scripts, and the links for the reachability checks."""
    for (k, f), cs in _pal_cells.items():
        set_ctx(PAL_CTX[f])
        mx, my = zc(3213, 3483)
        side = [(x + u, y + v) for (x, y) in cs for (u, v) in D4 if (x + u, y + v) not in cs and
                TY[y + v][x + u] in WALKSET and TY[y + v][x + u] != 'DOOR' and not wall_between(x, y, u, v)]
        assert side, ('palace stairs with no way off', k, f)
        n = '%s%d' % (k, f)
        QPOS['VP_' + n] = cs[0]
        QPOS['VPA_' + n] = min(side, key=lambda c: (abs(c[0] - mx) + abs(c[1] - my), c))
        QUEST_OBJS.append(('LADDER' if k in PAL_LADDERS else 'STAIRS',) + cs[-1])
    for (k, f), cs in _pal_cells.items():
        for g in (f - 1, f + 1):
            if (k, g) in _pal_cells:
                for c in cs:
                    LINKS.setdefault(c, []).append(QPOS['VPA_%s%d' % (k, g)])


for (_k, _f), _cs in _pal_cells.items():        # keep the furniture off the ways onto the stairs
    NO_FURNISH.update((_x + u, _y + v) for (_x, _y) in _cs for (u, v) in D4)
defer(palace_stairs)
set_ctx(OW)
