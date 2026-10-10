# Falador at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
FALADOR = city('FALADOR', (2935, 3307, 3065, 3398), 0, 448, 5)
set_ctx(FALADOR.ctx)
B = build_from_ref(load_ref('falador'), dict(stone='WWALL', wood='WWALL', road='COBBLE', fence='FENCE', city='WWALL',
                                             roof_stone='red', roof_wood='red', oaks=True))
# the Party Room: one chequered hall entered from the south
party = stamp(B, 3035, 3371, 3058, 3385, 'WWALL', 'CHECKER', doors=[(3045, 3371), (3046, 3371)], roof='red')
for (X, Y) in ((3037, 3383), (3056, 3383), (3037, 3374), (3056, 3374)):
    zput(X, Y, 'POTPLANT')
for X in range(3043, 3049):
    zput(X, 3383, 'TABLE')
bank(building_at(B, 2946, 3368), 'W')                                       # west bank
bank(building_at(B, 3013, 3356), 'S')                                       # east bank
zshop('FALADOR', building_at(B, 2957, 3387), 'SHOPKEEPER')
zshop('CASSIE', building_at(B, 2975, 3384), 'CASSIE')
zshop('WAYNE', building_at(B, 2972, 3312), 'WAYNE')
# Falador Park: white bridge over the duck pond, the statue in its curved low walls, flower beds,
# sunflowers and benches
isw = lambda X, Y: TY[cy(Y)][cx(X)] == 'WATER'
best = None                                    # the shortest north-south crossing of the duck pond
for X in range(3010, 3030):
    for Y in range(3372, 3392):
        if isw(X, Y) and not isw(X, Y + 1):
            n = 0
            while isw(X, Y - n):
                n += 1
            if 3 <= n <= 7 and not isw(X, Y - n) and isw(X + 1, Y) and isw(X + 1, Y - n + 1) and (best is None or n < best[0]):
                best = (n, X, Y)
if best:
    n, X, Y = best
    for k in range(n):
        for u in (0, 1):
            if isw(X + u, Y - k):
                zset(X + u, Y - k, 'BRIDGE', 'BRIDGE')
deco(3004, 3385, 'STATUE')
for (X, Y) in ((3002, 3384), (3002, 3385), (3003, 3386), (3005, 3386), (3006, 3385), (3006, 3384)):
    deco(X, Y, 'LOWWALL')
# the map draws the park's flower beds as little soil patches: those become flower beds
seen_ = set()
for X in range(2998, 3030):
    for Y in range(3366, 3392):
        if (X, Y) in seen_ or TY[cy(Y)][cx(X)] != 'PATH':
            continue
        comp, st = [], [(X, Y)]
        seen_.add((X, Y))
        while st:
            a_ = st.pop()
            comp.append(a_)
            for (u, v) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n_ = (a_[0] + u, a_[1] + v)
                if n_ not in seen_ and 2998 <= n_[0] < 3030 and 3366 <= n_[1] < 3392 and TY[cy(n_[1])][cx(n_[0])] == 'PATH':
                    seen_.add(n_)
                    st.append(n_)
        if len(comp) <= 14:
            for (X2, Y2) in comp:
                zset(X2, Y2, 'FLOWERBED', 'GRASS')
for X in range(3000, 3004):
    for Y in (3372, 3373):
        deco(X, Y, 'SUNFLOWER', ('GRASS', 'FLOWERS', 'FERN'))
for (X, Y) in ((3012, 3387), (3013, 3387), (3021, 3371), (3022, 3371)):
    deco(X, Y, 'BENCH', ('GRASS', 'FLOWERS', 'FERN'))
for b in B:
    if not b['special']:
        furnish(b)
