// Map storage access. The only code that knows how the map's cell data is laid out: everything
// else asks for a cell's type, metatiles or roof through these helpers (world.c adds the runtime
// overrides on top: world_type/world_meta/world_over).
#pragma once
#include "game.h"

static inline int map_in(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static inline u32 map_key(int x, int y) { return y * MAP_W + x; }   // unique per cell (hashes, sorted tables)

// ROM cell data without overrides (the cell must be map_in)
static inline int map_type_at(int x, int y) { return map_type[map_key(x, y)]; }
static inline int map_meta_at(int x, int y) { return map_meta[map_key(x, y)]; }
static inline int map_over_at(int x, int y) { return map_over[map_key(x, y)]; }
static inline int map_roof_at(int x, int y) { return map_roof[map_key(x, y)]; }

// NPC occupancy: one bit per cell, so free_tile() need not scan the NPCs
extern u32 occ_bits[];
static inline int occ_get(int x, int y) { u32 c = map_key(x, y); return (occ_bits[c >> 5] >> (c & 31)) & 1; }
static inline void occ_set(int x, int y) { u32 c = map_key(x, y); occ_bits[c >> 5] |= 1u << (c & 31); }
static inline void occ_clear(int x, int y) { u32 c = map_key(x, y); occ_bits[c >> 5] &= ~(1u << (c & 31)); }

// cells with a runtime override (world.c's table): one bit per cell, so most lookups cost a bit test
extern u32 ovr_bits[];
static inline int ovr_marked(u32 key) { return (ovr_bits[key >> 5] >> (key & 31)) & 1; }
static inline void ovr_mark(u32 key) { ovr_bits[key >> 5] |= 1u << (key & 31); }
static inline void ovr_unmark(u32 key) { ovr_bits[key >> 5] &= ~(1u << (key & 31)); }

void map_reset(void);                   // no NPCs, no overrides
