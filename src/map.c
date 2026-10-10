// Map storage: the block cache, region lookup and per-cell runtime state (see map.h).
#include "map.h"

EWRAM_BSS MapBlock map_slot[MAP_SLOTS];
u32 map_slot_key[MAP_SLOTS];
u8 map_tor[256];
u16 map_loads, map_sync_loads, map_load_lines;   // map_load_lines: most scanlines one block took
u32 ovr_filter[32];
static EWRAM_BSS MapBlock void_block;    // what a cell outside the map reads as (never drawn)
static u8 prefetching;
static s16 focus_bx, focus_by;           // where map_prefetch() last looked: evict far from it
static u8 focus_p;

#define OCC_SIZE 512                     // power of two, at least twice MAX_NPCS
static u32 occ_tab[OCC_SIZE];            // map_key + 1 of each cell with an NPC, 0 = empty

void map_reset(void) {
    for (int i = 0; i < OCC_SIZE; i++) occ_tab[i] = 0;
    for (int i = 0; i < 32; i++) ovr_filter[i] = 0;
    for (int i = 0; i < MAP_SLOTS; i++) map_slot_key[i] = 0;
    for (int i = 0; i < 256; i++) map_tor[i] = 0;
    for (int i = 0; i < 256; i++) {
        void_block.meta[i] = void_block.over[i] = 0;
        void_block.type[i] = T_WATER;
        void_block.roof[i] = 0;
    }
}

// ---------------------------------------------------------------------------------------
// Regions (sorted by plane, rx, ry): binary search, remembering the last one found
static const MapRegion *region_find(int rx, int ry, int p) {
    static const MapRegion *last;
    if (last && last->rx == rx && last->ry == ry && last->plane == p) return last;
    u32 k = (u32)p << 16 | (u32)rx << 8 | (u32)ry;
    int lo = 0, hi = MAP_REGION_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const MapRegion *r = &map_regions[mid];
        u32 rk = (u32)r->plane << 16 | (u32)r->rx << 8 | r->ry;
        if (rk == k) return last = r;
        if (rk < k) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

static const u32 *block_src(int bx, int by, int p) {
    const MapRegion *r = region_find(bx >> 2, by >> 2, p);
    return r ? r->blk[(by & 3) * 4 + (bx & 3)] : 0;
}

int map_walk_at(int x, int y, int p) {
    const MapRegion *r = region_find(x >> 6, y >> 6, p);
    if (!r) return 0;
    int lx = x & 63;
    return (r->walk[(y & 63) * 2 + (lx >> 5)] >> (lx & 31)) & 1;
}

// ---------------------------------------------------------------------------------------
// Block cache. map_tor[] is indexed by the block's position modulo 16 in each direction
// (map_tidx), so the blocks around the camera never share an entry; a slot whose entry was
// taken over by another block is dropped.

// LZ77 in the BIOS format tools/lz77.py writes (what SWI 0x11 LZ77UnCompWram reads). ARM code in
// IWRAM decoding into an IWRAM buffer: a block takes at most 22 scanlines instead of the BIOS's 29.
IWRAM_CODE __attribute__((noinline)) static void lz77_wram(const void *src_, void *dst_) {
    u32 buf[sizeof(MapBlock) / 4];   // decoded in IWRAM, then copied out: EWRAM bytes are slow
    const u8 *src = src_;
    u32 size = src[1] | src[2] << 8 | src[3] << 16;
    u8 *dst = (u8 *)buf, *end = dst + size;
    src += 4;
    while (dst < end) {
        u32 flags = *src++ | 0x100;   // the marker bit leaves the byte after its eighth item
        for (; flags < 0x10000 && dst < end; flags <<= 1) {
            if (flags & 0x80) {
                u32 b0 = src[0], b1 = src[1];
                src += 2;
                const u8 *from = dst - (((b0 & 15) << 8 | b1) + 1);
                u32 len = (b0 >> 4) + 3;
                do *dst++ = *from++; while (--len);
            } else {
                *dst++ = *src++;
            }
        }
    }
    dma3_copy32(dst_, buf, (size + 3) / 4);
}

static int block_dist(u32 key) {
    u32 k = key - 1;
    if ((int)(k >> 24) != focus_p) return 0x7FFF;
    int dx = (int)(k & 0xFFF) - focus_bx, dy = (int)((k >> 12) & 0xFFF) - focus_by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

// a free slot, else the one holding the block farthest from the focus
static int pick_slot(void) {
    int best = 0, bd = -1;
    for (int s = 0; s < MAP_SLOTS; s++) {
        if (!map_slot_key[s]) return s;
        int d = block_dist(map_slot_key[s]);
        if (d > bd) bd = d, best = s;
    }
    return best;
}

const MapBlock *map_block_load(int bx, int by, int p) {
    const u32 *src = block_src(bx, by, p);
    if (!src) return &void_block;
    if (!prefetching) {   // needed now (a teleport's redraw): keep what is near it, not near the old focus
        focus_bx = bx;
        focus_by = by;
        focus_p = p;
    }
    int s = pick_slot(), ti = map_tidx(bx, by, p);
    if (map_slot_key[s]) {   // the evicted block leaves its entry
        u32 k = map_slot_key[s] - 1;
        int oi = map_tidx(k & 0xFFF, (k >> 12) & 0xFFF, k >> 24);
        if (map_tor[oi] == s + 1) map_tor[oi] = 0;
    }
    if (map_tor[ti]) map_slot_key[map_tor[ti] - 1] = 0;   // the block that had this entry
    int v0 = REG_VCOUNT;
    lz77_wram(src, &map_slot[s]);
    int lines = REG_VCOUNT - v0;
    if (lines < 0) lines += 228;
    if (lines > map_load_lines) map_load_lines = lines;
    map_slot_key[s] = map_bkey(bx, by, p);
    map_tor[ti] = s + 1;
    map_loads++;
    if (!prefetching) map_sync_loads++;
    return &map_slot[s];
}

// Blocks within PF_RANGE blocks of the focus, nearest first, one at a time while the frame has
// PF_COST scanlines left before the next VBlank (a block takes ~PF_COST at most to decompress).
#define PF_RANGE 2
#define PF_COST 48
static int frame_lines(u32 vb0) {
    extern volatile u32 snd_play_frame;   // VBlanks so far (crt0.s)
    if (snd_play_frame != vb0) return 228;
    int v = REG_VCOUNT;
    return v >= 160 ? v - 160 : v + 68;
}

void map_prefetch(int x, int y, int p, u32 vb0) {
    focus_bx = x >> 4;
    focus_by = y >> 4;
    focus_p = p;
    for (int r = 0; r <= PF_RANGE; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (dx != -r && dx != r && dy != -r && dy != r) continue;   // ring r only
                int bx = focus_bx + dx, by = focus_by + dy, t = map_tor[map_tidx(bx, by, p)];
                if (bx < 0 || by < 0) continue;
                if (t && map_slot_key[t - 1] == map_bkey(bx, by, p)) continue;
                if (!block_src(bx, by, p)) continue;
                if (frame_lines(vb0) > 228 - PF_COST) return;
                prefetching = 1;
                map_block_load(bx, by, p);
                prefetching = 0;
            }
}

// ---------------------------------------------------------------------------------------
// NPC occupancy: open addressing with linear probing; removal shifts the run back
static inline u32 occ_hash(u32 k) { return (k * 2654435761u) >> 23; }   // 9 bits: OCC_SIZE

int occ_get(int x, int y, int p) {
    u32 k = map_key(x, y, p) + 1;
    for (u32 i = occ_hash(k);; i = (i + 1) & (OCC_SIZE - 1)) {
        if (occ_tab[i] == k) return 1;
        if (!occ_tab[i]) return 0;
    }
}

void occ_set(int x, int y, int p) {
    u32 k = map_key(x, y, p) + 1, i = occ_hash(k);
    while (occ_tab[i]) {
        if (occ_tab[i] == k) return;
        i = (i + 1) & (OCC_SIZE - 1);
    }
    occ_tab[i] = k;
}

void occ_clear(int x, int y, int p) {
    u32 k = map_key(x, y, p) + 1, i = occ_hash(k);
    while (occ_tab[i] != k) {
        if (!occ_tab[i]) return;
        i = (i + 1) & (OCC_SIZE - 1);
    }
    // close the gap: move back any later entry of the run whose home is not between the gap and it
    for (u32 j = i;;) {
        j = (j + 1) & (OCC_SIZE - 1);
        if (!occ_tab[j]) break;
        u32 h = occ_hash(occ_tab[j]);
        if (((j - h) & (OCC_SIZE - 1)) >= ((j - i) & (OCC_SIZE - 1))) {
            occ_tab[i] = occ_tab[j];
            i = j;
        }
    }
    occ_tab[i] = 0;
}
