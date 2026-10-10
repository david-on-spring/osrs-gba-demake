// Map storage access. The only code that knows how the map's cell data is laid out: everything
// else asks for a cell's type, metatiles or roof through these helpers (world.c adds the runtime
// overrides on top: world_type/world_meta/world_over).
#pragma once
#include "game.h"

// Cells are addressed by absolute OSRS tile (x east, y north, plane). The data is still one
// MAP_W x MAP_H grid with each plane's area at an offset (map_planes[]).
static inline int map_gx(int x, int p) { return x + map_planes[p][0]; }
static inline int map_gy(int y, int p) { return map_planes[p][1] - y; }
static inline int map_in(int x, int y, int p) {
    if ((unsigned)p >= PLANE_COUNT) return 0;
    int gx = map_gx(x, p), gy = map_gy(y, p);
    return gx >= 0 && gy >= 0 && gx < MAP_W && gy < MAP_H;
}
static inline u32 map_key(int x, int y, int p) { return map_gy(y, p) * MAP_W + map_gx(x, p); }   // unique per cell

// ROM cell data without overrides (the cell must be map_in)
static inline int map_type_at(int x, int y, int p) { return map_type[map_key(x, y, p)]; }
static inline int map_meta_at(int x, int y, int p) { return map_meta[map_key(x, y, p)]; }
static inline int map_over_at(int x, int y, int p) { return map_over[map_key(x, y, p)]; }
static inline int map_roof_at(int x, int y, int p) { return map_roof[map_key(x, y, p)]; }

// NPC occupancy: one bit per cell, so free_tile() need not scan the NPCs
extern u32 occ_bits[];
static inline int occ_get(int x, int y, int p) { u32 c = map_key(x, y, p); return (occ_bits[c >> 5] >> (c & 31)) & 1; }
static inline void occ_set(int x, int y, int p) { u32 c = map_key(x, y, p); occ_bits[c >> 5] |= 1u << (c & 31); }
static inline void occ_clear(int x, int y, int p) { u32 c = map_key(x, y, p); occ_bits[c >> 5] &= ~(1u << (c & 31)); }

// cells with a runtime override (world.c's table): one bit per cell, so most lookups cost a bit test
extern u32 ovr_bits[];
static inline int ovr_marked(u32 key) { return (ovr_bits[key >> 5] >> (key & 31)) & 1; }
static inline void ovr_mark(u32 key) { ovr_bits[key >> 5] |= 1u << (key & 31); }
static inline void ovr_unmark(u32 key) { ovr_bits[key >> 5] &= ~(1u << (key & 31)); }

void map_reset(void);                   // no NPCs, no overrides
