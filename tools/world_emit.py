"""C emission for the world: map, tiles, palettes, spawns and zones.

w is the snapshot from tools/world_cache.py (world_gen's results as plain data), so changes here
do not invalidate the cached world.
"""
import sys


def emit_header(w, h):
    h.append('#define MAP_REGION_COUNT %d' % len(w.MAP_REGIONS))
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
    h.append('#define WORLD_X0 %d   // pixel origin (OSRS tile at px 0)' % w.WORLD_X0)
    h.append('#define WORLD_Y1 %d   // OSRS tile row at py 0 (Y grows north, py south)' % w.WORLD_Y1)
    h.append('#define PLANE_COUNT %d' % w.NPLANES)
    for n, cell in [('START', w.START)] + [(k, w.QPOS[k]) for k in sorted(k for k in w.QPOS if k.startswith('TELE_'))]:
        X, Y, P = w.osrs_of(*cell)        # start / respawn and the teleport spells' arrival tiles
        h.append('#define %s_X %d' % (n, X))
        h.append('#define %s_Y %d' % (n, Y))
        h.append('#define %s_P %d' % (n, P))
    h.append('''
// positions are absolute OSRS tiles: x east, y north, plane 0..3; rects are inclusive, y0 south
typedef struct { u8 kind, plane; u16 x, y; } Spawn;
typedef struct { u8 kind, plane; u16 x, y; } FishSpotDef;
typedef struct { const char *name; u16 x0, y0, x1, y1; u8 plane; } Region;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set, plane; } Zone;
typedef struct { u16 x0, y0, x1, y1; u8 pal_set; } PalRegion;   // camera tile (top-left) rect -> palette set
typedef struct { u16 x, y; u8 plane; } ShopPos;
extern const u32 world_tiles[WORLD_TILE_COUNT * 8];
extern const u32 meta_tiles[META_COUNT][4];   // low 16: tile id, high 16: flip/palette attrs
// The map: OSRS map squares (64x64 tiles) of each plane, sorted by (plane, rx, ry). A block is
// 16x16 tiles, LZ77-compressed in the GBA BIOS format: meta u16[256], over u16[256], type
// u8[256], roof u8[256] (roof group, 0 none: hidden while the player is under it), cell
// (x & 15, y & 15) at index (y & 15) * 16 + (x & 15). See docs/regions.md.
enum { RF_EDGE = 1, RF_ROOFS = 2, RF_OVERLAY = 4 };   // region flags: partly outside the bounds, has roofs, overlay
typedef struct {
    u8 rx, ry, plane, pal_set;    // map square (x >> 6, y >> 6), floor, palette set of most of its cells
    u8 flags;                     // RF_*
    const u32 *blk[16];           // block (x >> 4 & 3, y >> 4 & 3) at [(y >> 4 & 3) * 4 + (x >> 4 & 3)]; 0 = outside the map
    const u32 *walk;              // 64x64 bits, word (y & 63) * 2 + (x >> 5 & 1), bit x & 31: walkable cells
    const u32 *edge;              // 0, or four such bitmaps: a wall on the tile's north edge, on its west
                                  // edge (both block walking), then the walls that also block projectiles
} MapRegion;
extern const MapRegion map_regions[MAP_REGION_COUNT];
extern const u16 map_bounds[PLANE_COUNT][4];   // x0, y0, x1, y1 of each floor (x0 > x1: no such floor)
extern const u32 res_cell[RES_COUNT];         // sorted keys ((plane << 28) | (y << 14) | x) of trees/rocks
extern const u16 res_alt[RES_COUNT];          // their depleted metatile (stump / empty rock)
extern const u16 world_pal[WORLD_PAL_SETS][14 * 16];
extern const u16 water_slots[WORLD_PAL_SETS][WATER_SLOT_MAX + 1];   // palette indices, 0-terminated
extern const Spawn spawns[SPAWN_COUNT];
extern const FishSpotDef fish_spots[FISHSPOT_COUNT];
extern const Region regions[REGION_COUNT];
extern const ShopPos shop_pos[SHOP_COUNT];      // tile at the centre of each shop's building
extern const Zone zones[ZONE_COUNT];
extern const PalRegion pal_regions[PAL_REGION_COUNT];
''')


def emit_regions(w, c, arr):
    streams, walks, edges = {}, {}, {}      # identical blocks / bitmaps are stored once
    for (p, rx, ry, ps, fl, blocks, walk, edge) in w.MAP_REGIONS:
        if edge is not None and tuple(edge) not in edges:
            edges[tuple(edge)] = len(edges)
            c.append(arr('u32', 'me_%d[512]' % edges[tuple(edge)], edge, 8, '0x%08X').replace('const u32', 'static const u32'))
        for b in blocks:
            if b is not None and b not in streams:
                streams[b] = len(streams)
                words = [int.from_bytes(b[i:i + 4], 'little') for i in range(0, len(b), 4)]
                c.append(arr('u32', 'mb_%d[%d]' % (streams[b], len(words)), words, 12, '0x%08X').replace('const u32', 'static const u32'))
        k = tuple(walk)
        if k not in walks:
            walks[k] = len(walks)
            c.append(arr('u32', 'mw_%d[128]' % walks[k], walk, 8, '0x%08X').replace('const u32', 'static const u32'))
    c.append('const MapRegion map_regions[MAP_REGION_COUNT] = {')
    for (p, rx, ry, ps, fl, blocks, walk, edge) in w.MAP_REGIONS:
        c.append('  {%d, %d, %d, %d, %d, {%s}, mw_%d, %s},' % (rx, ry, p, ps, fl, ', '.join(
            'mb_%d' % streams[b] if b is not None else '0' for b in blocks), walks[tuple(walk)],
            'me_%d' % edges[tuple(edge)] if edge is not None else '0'))
    c.append('};')
    nb = sum(1 for r in w.MAP_REGIONS for b in r[5] if b is not None)
    print('regions: %d (planes %s), %d blocks (%d stored), %d bytes compressed of %d, %d walk bitmaps, %d edge sets' % (
        len(w.MAP_REGIONS), ','.join(str(p) for p in sorted(w.BOUNDS)), nb, len(streams), sum(len(b) for b in streams),
        nb * 1536, len(walks), len(edges)), file=sys.stderr)


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
    emit_regions(w, c, arr)
    res = sorted(((lambda X, Y, P: (P << 28) | (Y << 14) | X)(*w.osrs_of(i % w.MW, i // w.MW)), m) for i, m in w.RES_ALT)
    c.append(arr('u32', 'res_cell[RES_COUNT]', [k for k, _ in res], 12, '0x%08X'))
    c.append(arr('u16', 'res_alt[RES_COUNT]', [m for _, m in res], 24, '%d'))
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
        X, Y, P = w.osrs_of(x, y)
        c.append('  {NPC_%s, %d, %d, %d},' % (k, P, X, Y))
    c.append('};')
    c.append('const FishSpotDef fish_spots[FISHSPOT_COUNT] = {')
    for k, x, y in w.FISH_SPOTS:
        X, Y, P = w.osrs_of(x, y)
        c.append('  {%d, %d, %d, %d},' % (0 if k == 'NET' else 1, P, X, Y))
    c.append('};')
    c.append('const ShopPos shop_pos[SHOP_COUNT] = {')
    for n, x, y in w.SHOPS:
        c.append('  {%d, %d, %d},   // %s' % (w.osrs_of(x, y) + (n,)))
    c.append('};')
    c.append('const u16 map_bounds[PLANE_COUNT][4] = {')
    for p in range(w.NPLANES):
        c.append('  {%d, %d, %d, %d},' % w.BOUNDS.get(p, (1, 0, 0, 0)))
    c.append('};')
    c.append('const Region regions[REGION_COUNT] = {')
    for n, x0, y0, x1, y1 in w.REGIONS:
        c.append('  {"%s", %d, %d, %d, %d, %d},' % ((n.replace('"', '\\"'),) + w.osrs_rect(x0, y0, x1, y1)))
    c.append('};')
    c.append('const PalRegion pal_regions[PAL_REGION_COUNT] = {')
    for (x0, y0, x1, y1), ps in w.PAL_REGIONS:
        X0, Y0, X1, Y1, P = w.osrs_rect(x0, y0, min(x1, w.OW_W - 1), min(y1, w.OW_H - 1))
        assert P == 0
        c.append('  {%d, %d, %d, %d, %d},' % (X0, Y0, X1, Y1, ps))
    c.append('};')
    c.append('const Zone zones[ZONE_COUNT] = {')
    for n, z, ps, pl_ in w.ZONES:
        X0, Y0, X1, Y1, P = w.osrs_rect(z.X0, z.Y0, z.X1, z.Y1)
        assert P == pl_
        c.append('  {%d, %d, %d, %d, %d, %d},   // %s' % (X0, Y0, X1, Y1, ps, pl_, n))
    c.append('};')
