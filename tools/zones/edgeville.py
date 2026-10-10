# Edgeville at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
EDGEVILLE = city('EDGEVILLE', (3070, 3462, 3125, 3518), 152, 448, 6)
set_ctx(EDGEVILLE.ctx)
B = build_from_ref(load_ref('edgeville'), dict(stone='WALL', wood='WALL', road='COBBLE', fence='FENCE',
                                               roof_stone='thatch', roof_wood='thatch'))
bk = stamp(B, 3091, 3488, 3098, 3499, 'WALL', 'FLOOR', doors=[(3094, 3488), (3095, 3488)], roof='thatch')
service(orect(3091, 3488, 3098, 3499), 'N', 'BOOTH', 'BANKER')
zshop('EDGEVILLE', building_at(B, 3080, 3510), 'SHOPKEEPER')
dave = stamp(B, 3077, 3489, 3086, 3497, 'WALL', 'WOODFLOOR', doors=[(3086, 3493)], roof='thatch')
furnish(dave, ['RANGE', 'TABLE', 'CHAIR', 'BED', 'BOOKCASE', 'DRAWERS'])
deco_near(3088, 3503, 'WELL')                                               # the village well
for b in B:
    if not b['special']:
        furnish(b)
