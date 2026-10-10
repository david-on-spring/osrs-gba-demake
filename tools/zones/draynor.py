# Draynor Village at 1:1. Run by tools/world_gen.py in its namespace (see tools/zones/README.md).
DRAYNOR = city('DRAYNOR', (3072, 3226, 3132, 3285))
set_ctx(DRAYNOR.ctx)
B = build_from_ref(load_ref('draynor'), dict(stone='WALL', wood='WALL', road='COBBLE', fence='FENCE',
                                             roof_stone='slate', roof_wood='slate'))
# Draynor bank: booths along the south wall, entered from the north
bank(building_at(B, 3092, 3243), 'S')
# market square stalls (2x2, striped awnings)
for (X, Y) in ((3076, 3253), (3081, 3253), (3076, 3248), (3081, 3248)):
    for dx in (0, 1):
        for dy in (0, 1):
            zput(X + dx, Y - dy, 'BIGSTALL')
for b in B:
    if not b['special']:
        furnish(b)
