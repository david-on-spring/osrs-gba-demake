"""C emission for the world: map, tiles, palettes, spawns and zones.

w is the snapshot from tools/world_cache.py (world_gen's results as plain data), so changes here
do not invalidate the cached world.
"""


def emit_header(w, h):
    h.append('#define MAP_W %d' % w.MW)
    h.append('#define MAP_H %d' % w.MH)
    h.append('#define OW_H %d' % w.OW_H)
    for n, i in w.T.items():
        h.append('#define T_%s %d' % (n, i))
    h.append('#define T_WALKABLE_MAX %d' % w.T['DOOR'])
    h.append('#define WORLD_TILE_COUNT %d' % len(w.idx_tiles))
    h.append('#define META_COUNT %d' % len(w.meta_out))
    h.append('#define RES_COUNT %d' % len(w.RES_ALT))
    h.append('#define WORLD_PAL_SETS %d' % w.NSETS)
    h.append('#define WATER_SLOT_MAX %d' % max(len(s) for s in w.water_slots))
    h.append('#define SPAWN_COUNT %d' % len(w.SPAWNS))
    h.append('#define FISHSPOT_COUNT %d' % len(w.FISH_SPOTS))
    h.append('#define REGION_COUNT %d' % len(w.REGIONS))
    h.append('#define PAL_REGION_COUNT %d' % len(w.PAL_REGIONS))
    h.append('#define ZONE_COUNT %d' % len(w.ZONES))
    h.append('enum { %s };' % ', '.join('ZONE_' + z[0] for z in w.ZONES))
    h.append('enum { %s };' % ', '.join('SHOP_' + n for n, _, _ in w.SHOPS))
    h.append('#define SHOP_COUNT %d' % len(w.SHOPS))
    h.append('#define START_X %d' % w.START[0])
    h.append('#define START_Y %d' % w.START[1])
    for n in sorted(k for k in w.QPOS if k.startswith('TELE_')):    # teleport spell arrival cells
        h.append('#define %s_X %d' % (n, w.QPOS[n][0]))
        h.append('#define %s_Y %d' % (n, w.QPOS[n][1]))
    h.append('''
typedef struct { u8 kind; u16 x, y; } Spawn;
typedef struct { u8 kind; u16 x, y; } FishSpotDef;
typedef struct { const char *name; u16 x0, y0, x1, y1; } Region;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set, plane; } Zone;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set; } PalRegion;   // camera cell (top-left) rect -> palette set
extern const u32 world_tiles[WORLD_TILE_COUNT * 8];
extern const u32 meta_tiles[META_COUNT][4];   // low 16: tile id, high 16: flip/palette attrs
extern const u16 map_meta[MAP_W * MAP_H];
extern const u16 map_over[MAP_W * MAP_H];
extern const u8 map_type[MAP_W * MAP_H];
extern const u8 map_roof[MAP_W * MAP_H];      // roof group (0 none): hidden while the player is under it
extern const u32 res_cell[RES_COUNT];         // sorted cell indices of trees/rocks
extern const u16 res_alt[RES_COUNT];          // their depleted metatile (stump / empty rock)
extern const u16 world_pal[WORLD_PAL_SETS][14 * 16];
extern const u16 water_slots[WORLD_PAL_SETS][WATER_SLOT_MAX + 1];   // palette indices, 0-terminated
extern const Spawn spawns[SPAWN_COUNT];
extern const FishSpotDef fish_spots[FISHSPOT_COUNT];
extern const Region regions[REGION_COUNT];
extern const u16 shop_pos[SHOP_COUNT][2];     // cell at the centre of each shop's building
extern const Zone zones[ZONE_COUNT];
extern const PalRegion pal_regions[PAL_REGION_COUNT];
''')


def emit_source(w, c, arr, tile_words, npc_kinds):
    for k, _, _ in w.SPAWNS:
        assert k in npc_kinds, k
    wt = []
    for t in w.idx_tiles:
        wt += tile_words(t)
    c.append(arr('u32', 'world_tiles[WORLD_TILE_COUNT * 8]', wt, 8, '0x%08X'))
    c.append('const u32 meta_tiles[META_COUNT][4] = {')
    for m in w.meta_out:
        c.append('  {0x%08X,0x%08X,0x%08X,0x%08X},' % tuple(m))
    c.append('};')
    c.append(arr('u16', 'map_meta[MAP_W * MAP_H]', [w.map_meta[y][x] for y in range(w.MH) for x in range(w.MW)], 24, '%d'))
    c.append(arr('u16', 'map_over[MAP_W * MAP_H]', [w.map_over[y][x] for y in range(w.MH) for x in range(w.MW)], 32, '%d'))
    c.append(arr('u8', 'map_type[MAP_W * MAP_H]', [w.T[w.TY[y][x]] for y in range(w.MH) for x in range(w.MW)], 40, '%d'))
    c.append(arr('u8', 'map_roof[MAP_W * MAP_H]', [w.ROOF[y][x] for y in range(w.MH) for x in range(w.MW)], 40, '%d'))
    c.append(arr('u32', 'res_cell[RES_COUNT]', [i for i, _ in w.RES_ALT], 24, '%d'))
    c.append(arr('u16', 'res_alt[RES_COUNT]', [m for _, m in w.RES_ALT], 24, '%d'))
    c.append('const u16 world_pal[WORLD_PAL_SETS][14 * 16] = {')
    for wp in w.world_pal:
        c.append('  {' + ','.join('0x%04X' % v for v in wp) + '},')
    c.append('};')
    c.append('const u16 water_slots[WORLD_PAL_SETS][WATER_SLOT_MAX + 1] = {')
    for ws in w.water_slots:
        c.append('  {' + ','.join('%d' % v for v in ws + [0] * (max(len(s) for s in w.water_slots) + 1 - len(ws))) + '},')
    c.append('};')
    c.append('const Spawn spawns[SPAWN_COUNT] = {')
    for k, x, y in w.SPAWNS:
        c.append('  {NPC_%s, %d, %d},' % (k, x, y))
    c.append('};')
    c.append('const FishSpotDef fish_spots[FISHSPOT_COUNT] = {')
    for k, x, y in w.FISH_SPOTS:
        c.append('  {%d, %d, %d},' % (0 if k == 'NET' else 1, x, y))
    c.append('};')
    c.append('const u16 shop_pos[SHOP_COUNT][2] = {')
    for n, x, y in w.SHOPS:
        c.append('  {%d, %d},   // %s' % (x, y, n))
    c.append('};')
    c.append('const Region regions[REGION_COUNT] = {')
    for n, x0, y0, x1, y1 in w.REGIONS:
        c.append('  {"%s", %d, %d, %d, %d},' % (n.replace('"', '\\"'), x0, y0, x1, y1))
    c.append('};')
    c.append('const PalRegion pal_regions[PAL_REGION_COUNT] = {')
    for (x0, y0, x1, y1), ps in w.PAL_REGIONS:
        c.append('  {%d, %d, %d, %d, %d},' % (x0, y0, x1, y1, ps))
    c.append('};')
    c.append('const Zone zones[ZONE_COUNT] = {')
    for n, z, ps, pl_ in w.ZONES:
        c.append('  {%d, %d, %d, %d, %d, %d},   // %s' % (z.X0, z.Y0, z.X1, z.Y1, ps, pl_, n))
    c.append('};')
