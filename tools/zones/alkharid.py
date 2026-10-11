# Al Kharid at 1:1 (with the Lumbridge toll gate on its western fence). Run by tools/world_gen.py in its
# namespace (see tools/zones/README.md).
ALKHARID = city('ALKHARID', (3262, 3142, 3330, 3235))
set_ctx(ALKHARID.ctx)
B = build_from_ref(load_ref('alkharid'), dict(stone='WWALL', wood='WWALL', road='PATH', sand='DESERT', fence='FENCE',
                                              roof_stone='slate', roof_wood='slate', tree='PALM', desert=True,
                                              crops='DEADTREE'))
# the toll gate: 10 coins each way (obj TOLLGATE / talk BORDER_GUARD in tools/quests.txt)
for Y in (3227, 3228):
    zset(3267, Y, 'TOLLGATE')
    QUEST_OBJS.append(('TOLLGATE',) + zc(3267, Y))
QPOS['TOLLGATE_W'] = zc(3266, 3228)
QPOS['TOLLGATE_E'] = zc(3268, 3228)
bank(building_at(B, 3270, 3166), 'E')
zshop('ALKHARID', building_at(B, 3315, 3179), 'SHOPKEEPER')
zshop('ZEKE', building_at(B, 3287, 3189), 'ZEKE')
zshop('LOUIE', building_at(B, 3315, 3165), 'LOUIE')
palace = building_at(B, 3292, 3162)
furnish(palace, ['THRONE', 'TABLE', 'TABLE', 'CHAIR', 'CHAIR', 'POTPLANT', 'POTPLANT', 'POTPLANT', 'POTPLANT',
                 'BOOKCASE', 'DRAWERS'])
karim = building_at(B, 3273, 3180)
furnish(karim, ['RANGE', 'TABLE', 'BARREL', 'SACKS'])
smelt = building_at(B, 3277, 3187)
furnish(smelt, ['BARREL', 'CRATE'])            # its furnace: world_gen.forge()
deco_near(3293, 3189, 'WELL', on=('DESERT', 'SAND', 'PATH'))               # the well before the palace
for b in B:
    if not b['special']:
        furnish(b, ['BED', 'TABLE', 'CHAIR', 'POTPLANT', 'SACKS', 'BARREL', 'DRAWERS', 'CHAIR'])
