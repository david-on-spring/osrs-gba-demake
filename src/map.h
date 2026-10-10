// Map storage access. The only code that knows how the map's cell data is laid out: everything
// else asks for a cell's type, metatiles or roof through these helpers (world.c adds the runtime
// overrides on top: world_type/world_meta/world_over).
//
// Cells are addressed by absolute OSRS tile (x east, y north, plane). The map is stored as OSRS
// map squares (map_regions[], 64x64 tiles) of sixteen 16x16-tile blocks, each LZ77-compressed
// in ROM. Blocks are decompressed into a small cache in EWRAM: map_prefetch() loads the ones
// around the camera within each frame's spare scanlines, and a block that is needed but not
// loaded is decompressed on the spot. Walkability, which NPCs need wherever they are, comes from
// each region's uncompressed bitmap instead. See docs/regions.md.
#pragma once
#include "game.h"

// in the playable bounds of a floor (the cell exists)
static inline int map_in(int x, int y, int p) {
    if ((unsigned)p >= PLANE_COUNT) return 0;
    const u16 *b = map_bounds[p];
    return x >= b[0] && y >= b[1] && x <= b[2] && y <= b[3];
}
// unique per cell (hashes, the sorted resource table); x, y < 16384
static inline u32 map_key(int x, int y, int p) { return ((u32)p << 28) | ((u32)y << 14) | (u32)x; }

// ---- decompressed blocks ----
typedef struct { u16 meta[256], over[256]; u8 type[256], roof[256]; } MapBlock;   // as stored in ROM
#define MAP_SLOTS 40                    // blocks held in EWRAM (1.5 KB each)
extern MapBlock map_slot[MAP_SLOTS];
extern u32 map_slot_key[MAP_SLOTS];     // map_bkey() of the block in each slot, 0 = free
extern u8 map_tor[256];                 // slot + 1 holding a block (index map_tidx()), 0 = none
extern u16 map_loads, map_sync_loads;   // blocks decompressed: all, and on demand (debug counters)
extern u16 map_load_lines;              // most scanlines one block took to decompress (debug)
static inline u32 map_bkey(int bx, int by, int p) { return ((u32)p << 24 | (u32)by << 12 | (u32)bx) + 1; }
// index in map_tor[]: the block position modulo 16, shifted per floor so that the floors of one
// building do not share entries (blocks that do share one are at least 5 blocks apart)
static inline int map_tidx(int bx, int by, int p) { return ((by + p * 11) & 15) << 4 | ((bx + p * 5) & 15); }
const MapBlock *map_block_load(int bx, int by, int p);
// the block holding a cell (map_in), decompressed now if it is not loaded
static inline const MapBlock *map_block(int x, int y, int p) {
    int bx = x >> 4, by = y >> 4, t = map_tor[map_tidx(bx, by, p)];
    if (t && map_slot_key[t - 1] == map_bkey(bx, by, p)) return &map_slot[t - 1];
    return map_block_load(bx, by, p);
}
static inline int map_cell(int x, int y) { return (y & 15) << 4 | (x & 15); }   // index in a block

// ROM cell data without overrides (the cell must be map_in)
static inline int map_type_at(int x, int y, int p) { return map_block(x, y, p)->type[map_cell(x, y)]; }
static inline int map_meta_at(int x, int y, int p) { return map_block(x, y, p)->meta[map_cell(x, y)]; }
static inline int map_over_at(int x, int y, int p) { return map_block(x, y, p)->over[map_cell(x, y)]; }
static inline int map_roof_at(int x, int y, int p) { return map_block(x, y, p)->roof[map_cell(x, y)]; }
int map_walk_at(int x, int y, int p);   // walkable terrain (map_in), from the region's bitmap: never decompresses

// load the blocks around a cell (the camera, or where a teleport is going) while the frame has
// scanlines to spare; vb0 is the VBlank count when the frame started
void map_prefetch(int x, int y, int p, u32 vb0);

// ---- NPC occupancy: a hash set of the cells with an NPC, so free_tile() need not scan them ----
int occ_get(int x, int y, int p);
void occ_set(int x, int y, int p);
void occ_clear(int x, int y, int p);

// ---- cells with a runtime override (world.c's table): a 1024-bit filter, so lookups for the
// other cells cost one bit test. Clearing a mark means rebuilding the filter from the table.
extern u32 ovr_filter[32];
static inline u32 ovr_hash(u32 key) { return (key * 2654435761u) >> 22; }
static inline int ovr_marked(u32 key) { u32 h = ovr_hash(key); return (ovr_filter[h >> 5] >> (h & 31)) & 1; }
static inline void ovr_mark(u32 key) { u32 h = ovr_hash(key); ovr_filter[h >> 5] |= 1u << (h & 31); }

void map_reset(void);                   // no NPCs, no overrides, no blocks loaded
