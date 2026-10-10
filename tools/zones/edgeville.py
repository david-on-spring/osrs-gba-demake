# Edgeville at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
EDGEVILLE = city('EDGEVILLE', (3070, 3462, 3125, 3518), 152, 448, 6)
set_ctx(EDGEVILLE.ctx)
B = build_from_ref(load_ref('edgeville'), dict(stone='WALL', wood='WALL', road='COBBLE', fence='FENCE',
                                               roof_stone='thatch', roof_wood='thatch'))
bk = stamp(B, 3090, 3487, 3099, 3500, 'WALL', 'FLOOR', roof='thatch')
service(orect(3090, 3487, 3099, 3500), 'S', 'BOOTH', 'BANKER')
zshop('EDGEVILLE', building_at(B, 3080, 3510), 'SHOPKEEPER')
dave = stamp(B, 3076, 3487, 3083, 3501, 'WALL', 'WOODFLOOR', roof='thatch')       # Evil Dave's house
furnish(dave, ['RANGE', 'TABLE', 'CHAIR', 'BED', 'BOOKCASE', 'DRAWERS'])
# the ruined mansion south of the bank, with its yew trees
ruin(B, building_at(B, 3093, 3475))
ruin(B, building_at(B, 3083, 3470))
for (X, Y) in ((3087, 3470), (3087, 3474), (3087, 3478), (3094, 3477)):
    deco_near(X, Y, 'YEW', 2)
deco_near(3088, 3503, 'WELL')                                               # the village well
for b in B:
    if not b['special']:
        furnish(b)
