# Varrock at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
VARROCK = city('VARROCK', (3175, 3380, 3290, 3505), 0, 300, 3)
set_ctx(VARROCK.ctx)
B = build_from_ref(load_ref('varrock'), dict(stone='WALL', wood='WALL', road='COBBLE', fence='FENCE', city='WALL',
                                             roof_stone='slate', roof_wood='slate', oaks=True))
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
# the church: pews and the altar at the east end
church = stamp(B, 3253, 3471, 3260, 3488, 'WALL', 'FLOOR', doors=[(3256, 3471), (3257, 3471)], roof='slate')
zfill(3256, 3472, 3257, 3486, 'CARPET')
zput(3256, 3487, 'ALTAR')
zput(3257, 3487, 'ALTAR')
for Y in (3475, 3477, 3479, 3481, 3483):
    for X in (3254, 3255, 3258, 3259):
        zput(X, Y, 'PEW')
zput(3254, 3487, 'POTPLANT')
zput(3259, 3487, 'POTPLANT')
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
# where the Varrock Teleport spell lands
QPOS['TELE_VARROCK'] = nearest_walk(cx(3212), cy(3424), walk_ok, 4)
