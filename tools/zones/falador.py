# Falador at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
FALADOR = city('FALADOR', (2935, 3307, 3065, 3398), 0, 448, 5)
set_ctx(FALADOR.ctx)
B = build_from_ref(load_ref('falador'), dict(stone='WWALL', wood='HWALL', road='COBBLE', fence='FENCE', city='WWALL',
                                             roof_stone='blue', roof_wood='brown', oaks=True))
bank(building_at(B, 2946, 3368), 'W')                                       # west bank
bank(building_at(B, 3013, 3356), 'S')                                       # east bank
zshop('FALADOR', building_at(B, 2957, 3387), 'SHOPKEEPER')
zshop('CASSIE', building_at(B, 2975, 3384), 'CASSIE')
zshop('WAYNE', building_at(B, 2972, 3312), 'WAYNE')
for b in B:
    if not b['special']:
        furnish(b)
