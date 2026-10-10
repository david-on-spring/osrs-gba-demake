# Varrock at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
VARROCK = city('VARROCK', (3175, 3380, 3290, 3505), 0, 300, 3)
set_ctx(VARROCK.ctx)
B = build_from_ref(load_ref('varrock'), dict(stone='WALL', wood='HWALL', road='COBBLE', fence='FENCE', city='WALL',
                                             roof_stone='slate', roof_wood='brown', oaks=True))
# banks
bank(building_at(B, 3185, 3440), 'W')                      # west bank
bank(building_at(B, 3253, 3420), 'N')                      # east bank
# shops
zshop('VARROCK', building_at(B, 3216, 3415), 'SHOPKEEPER')
zshop('SWORDSHOP', building_at(B, 3205, 3399), 'SWORD_KEEPER')
zshop('HORVIK', building_at(B, 3229, 3437), 'HORVIK')
# Varrock Square fountain
for X in range(3210, 3214):
    for Y in range(3427, 3431):
        zput(X, Y, 'FOUNTAIN')
# the church: pews and the altar at the east end
church = building_at(B, 3257, 3480)
furnish(church, ['ALTAR', 'PEW', 'PEW', 'PEW', 'PEW', 'PEW', 'PEW', 'POTPLANT', 'POTPLANT'])
# the palace: throne room
palace = building_at(B, 3222, 3488)
furnish(palace, ['THRONE', 'TABLE', 'TABLE', 'CHAIR', 'CHAIR', 'BOOKCASE', 'POTPLANT', 'POTPLANT', 'DRAWERS'])
# the Blue Moon Inn: bar, tables and barrels
inn = building_at(B, 3225, 3398)
furnish(inn, ['COUNTER', 'COUNTER', 'COUNTER', 'BARREL', 'BARREL', 'TABLE', 'CHAIR', 'CHAIR', 'TABLE', 'CHAIR',
              'CHAIR', 'FIREPLACE'])
# the anvils west of the square
smith = building_at(B, 3188, 3424)
furnish(smith, ['ANVIL', 'ANVIL', 'BARREL', 'CRATE'])
# the museum
museum = building_at(B, 3258, 3448)
furnish(museum, ['BOOKCASE', 'BOOKCASE', 'STATUE', 'STATUE', 'SHELF', 'SHELF', 'POTPLANT'])
for b in B:
    if not b['special']:
        furnish(b)
