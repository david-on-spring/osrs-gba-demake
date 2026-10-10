# Region world format

The map is stored the way OSRS stores it: as **map squares** ("regions") of 64×64 tiles per
plane, addressed by absolute OSRS coordinates. Nothing in the game knows the size of the world;
a region that is not in ROM costs nothing, so the format scales from today's 416×416 mainland to
the whole OSRS map (see "Scaling" below).

## Coordinates

Every position in the game is an absolute OSRS tile: `x` grows east, `y` grows north, `plane` is
the floor (0 ground .. 3). That covers the player (and saves), NPCs and their homes, ground items,
fires, depleted trees and rocks, fishing spots, shops, area names, zones, quest places and
teleport destinations. The castle's first and second floors are planes 1 and 2 at their real
coordinates.

Pixels (`px`, `py`, `cam_x`, `cam_y`) grow east and south from the tile `WORLD_X0`, `WORLD_Y1`
(`TILE_PX`, `TILE_PY` in `game.h`) and are the same on every floor, so a floor is drawn over the
same pixels as the ground below it. Only things on the player's floor are drawn, and NPCs on
another floor are out of reach.

`tools/world_gen.py` still lays the world out on its cell grid; `osrs_of()` turns a cell into a
tile when the data is emitted.

## ROM layout (`build/assets.c`)

```c
typedef struct {
    u8 rx, ry, plane, pal_set;    // map square (x >> 6, y >> 6), floor, palette set of most cells
    u8 flags;                     // RF_EDGE (partly outside the bounds), RF_ROOFS, RF_OVERLAY
    const u32 *blk[16];           // 16x16-tile blocks, [(y >> 4 & 3) * 4 + (x >> 4 & 3)]; 0 = none
    const u32 *walk;              // 64x64 bits: walkable cells
} MapRegion;
extern const MapRegion map_regions[];   // sorted by (plane, rx, ry)
extern const u16 map_bounds[PLANE_COUNT][4];
```

- **Block**: 16×16 tiles, LZ77-compressed in the GBA BIOS format (`tools/lz77.py`). Unpacked it
  is 1,536 bytes: `meta u16[256]`, `over u16[256]`, `type u8[256]`, `roof u8[256]`, cell
  `(x & 15, y & 15)` at index `(y & 15) * 16 + (x & 15)`. Blocks outside the playable bounds are
  not stored, and identical blocks are stored once.
- **Walk bitmap**: one bit per tile (type `<= T_WALKABLE_MAX`), uncompressed (512 bytes per
  region; identical bitmaps are shared). NPCs wander, chase and respawn all over the world every
  tick, so their walkability checks read this and never decompress anything.
- **Header**: palette set and flags describe the region for tools and for a future importer; the
  camera still picks palette sets from `pal_regions[]`.
- `res_cell[]` (trees and rocks and their depleted metatiles) is keyed by
  `(plane << 28) | (y << 14) | x`.

An OSRS-cache importer can fill the same structure: one entry per map square and plane, sixteen
blocks each.

## Runtime (`src/map.h`, `src/map.c`)

- **Block cache**: 40 slots of 1.5 KB in EWRAM. `map_block(x, y, p)` finds a block through a
  16×16 table indexed by the block position modulo 16 (the blocks around the camera never share
  an entry), so a lookup is a table read and a compare.
- **Prefetch**: after the frame's work, `map_prefetch()` decompresses missing blocks within two
  blocks of the camera, nearest first, one at a time while the frame still has room for a block
  before the next VBlank. During a staircase or teleport fade it loads around the destination
  instead. A block that is needed but missing (a test script's direct teleport) is decompressed
  on the spot; `map_sync_loads` counts those.
- **Eviction**: a free slot, else the block farthest from the camera.
- Decompression is the BIOS `SWI 0x11` (LZ77UnCompWram) straight into the slot.
- **Occupancy** (which cells hold an NPC) is a 512-entry hash set keyed by absolute cell instead
  of a bitmap of the whole map.
- **Overrides** (stumps, mined rocks) stay a 72-entry table keyed by absolute cell, so they
  survive the camera leaving; a 1024-bit filter replaces the whole-map bitmap.

## Measurements

Before is `main` at 734d215; after is this format. Perf is scanlines of work per frame
(`perf_max`), measured with the same scripts in game-frame mode on both builds, both with the
VBlank-counting perf counter (the old counter wrapped at 228 and hid frames that ran over).

| | Before | After |
|---|---|---|
| ROM | 2,663,424 B | 2,058,240 B (−605 KB) |
| `.text` | 75,664 B | 79,040 B |
| `.rodata` | 2,584,012 B | 1,975,348 B |
| Map cell data in ROM | 1,148,160 B (four 416×460 arrays) | 504,388 B blocks + 30,208 B walk bitmaps + 60 headers |
| EWRAM (`.ewram`) | 134,576 B | 150,224 B (block cache 61,440 B in, two whole-map bitmaps 47,840 B out) |
| IWRAM (`.data` + `.bss`) | 16,540 B | 19,324 B (occupancy hash 2 KB, cache index) |
| Regions / blocks | — | 60 map squares on planes 0–2, 694 blocks (689 distinct), 59 distinct walk bitmaps |
| Compression | — | 1,065,984 → 504,388 B (2.1×); `meta` is most of it (every cell has its own metatile) |
| One block decompressed (SWI 0x11) | — | at most 29 scanlines |
| Running across map-square boundaries (`shots/t/regions.src`) | 189 / 182 | 208 / 208, no block decompressed on demand |
| Running in and between towns (`shots/map/perf.src`) | 181 / 174 / 187 | 199 / 208 / 203 |
| `shots/map/perf_towns.src` walking segments | 161 / 162 / 161 / 150 | 173 / 182 / 193 / 194 |
| Teleport frame (first redraw, `perf.src`) | 588 | 675 |
| Staircase frame (castle, first floor) | 623 (Step 2 build) | 618, no block decompressed on demand |

Walking frames rise by about 20 lines because the prefetch uses the frame's spare scanlines
(it stops when a block would no longer fit before the next VBlank); they stay under 228.
Frames that redraw the whole 31×21 window (a teleport, a staircase, a roof change) already took
two to three VBlanks before this change. A test script's direct teleport also decompresses the
one to three blocks under the new view on the spot; an in-game teleport or staircase does not,
because the fade loads the destination first.

Block size, measured on this map (whole map, LZ77): 8×8 tiles 1.91×, 16×16 2.08×, 32×32 2.20×,
64×64 2.27×. 16×16 keeps a teleport's on-demand work to a few small blocks and the cache small.

## Scaling

- RAM no longer depends on the size of the map: the block cache, occupancy hash and override
  table are fixed. What still grows with the map is ROM: the region table (76 bytes per map
  square), the blocks and `res_cell[]`.
- ROM: about 2.6 bytes per tile for blocks plus 0.125 for walkability on this map, so a
  thousand map squares (4 million tiles) would take about 11 MB.
- Coordinates fit OSRS (x, y < 16384, 4 planes; region ids are u8 x u8). Pixels are s16, so one
  pixel space spans 2,048 tiles; a much larger world needs the pixel origin to move with the
  player (rebase `px`/`py`/`cam_*`), as the OSRS client rebases its scene.
- The next wall is metatiles: `meta` is u16 and 45,750 of 65,535 are used. Per-region metatile
  dictionaries (u8 or u16 indices into a region's own list) would lift it and shrink blocks.
- NPCs are still one global table of 256 that all run every tick; a whole-world map wants them
  per region too.
