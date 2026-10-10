// Video setup, palettes (fades, water shimmer, gradient), world tilemap streaming and OAM.
#include "game.h"
#include "map.h"

// VRAM layout
//   0x0000-0x4B1F  text canvas tiles (600 + blank)            BG0, char base 0
//   0x4B20-0x963F  panel canvas tiles (600 + blank)           BG1, char base 1
//   0x9800/0xA000/0xA800/0xB000  screen blocks 19..22         BG0..BG3 maps
//   0xB800-0xFFFF  world tile cache: 576 slots (char 448..1023)  BG2/BG3, char base 2
//
// World tiles are streamed: the map, its metatiles and all unique 8x8 tiles stay in ROM, and
// the tiles referenced by the visible 31x21 window are kept in a refcounted VRAM cache. When
// the camera crosses a tile boundary only the incoming column/row is resolved (42 entries)
// and the outgoing one released; missing tiles are copied straight into free slots (a slot is
// only reused one update after its last on-screen use, so the copy never shows mid-frame).
// tools/world_gen.py checks offline that no window needs more tiles than the cache holds.
#define SB_TEXT 19
#define SB_PANEL 20
#define SB_OVER 21
#define SB_GROUND 22

ObjAttr oam_buf[128];
int oam_n;
static void icon_cache_reset(void);

static EWRAM_BSS u16 map_ground[32 * 32] __attribute__((aligned(4)));
static EWRAM_BSS u16 map_overlay[32 * 32] __attribute__((aligned(4)));
static u8 maps_pending;
static s16 drawn_tx = -999, drawn_ty = -999;
static s8 drawn_plane = -1;
static u8 world_dirty = 1;
static u8 world_hidden;   // world layers off this frame: a teleport's redraw waits for the next one

#define CACHE_BASE 448        // char index of slot 0 (VRAM 0xB800 with char base 2)
#define CACHE_SLOTS 576
static EWRAM_BSS u16 gslot[WORLD_TILE_COUNT];   // cache slot + 1 holding each ROM tile, 0 = none
static EWRAM_BSS u16 sowner[CACHE_SLOTS];       // ROM tile held by each slot
static u16 sref[CACHE_SLOTS] __attribute__((aligned(4)));   // screen entries referencing each slot (IWRAM: hot)
static EWRAM_BSS u16 sfreed[CACHE_SLOTS];       // update serial when the slot became unused
static u16 upd_serial = 1, clock_hand = 1;
static u8 cache_retry;
u16 cache_misses, cache_loads;                  // debug counters (read by test scripts)
#define MAX_CELL_DIRTY 8
static s16 dirty_cells[MAX_CELL_DIRTY][2];
static u8 n_dirty_cells;

static EWRAM_BSS u16 wpal[14 * 16];   // world palette (with water shimmer applied)
static u8 wpal_set;                    // world_pal[] set in wpal (one per zone kind)
static u8 hidden_roof;                 // map_roof group left off the overlay layer
// the cell draw_tile() looked up last: the two tiles of a cell in a row share one lookup
static struct { s16 x, y; s8 p; u8 in; u16 meta, over; u8 roof; } memo = {0, 0, -1, 0, 0, 0, 0};
static EWRAM_BSS u16 opal[8 * 16];    // actor palettes (player palette is dynamic)
static u8 pal_pending;
static u8 fade_level = 16;
u16 panel_grad[161];
static u8 mosaic_n;
static u8 ui_eva = 12, ui_text_blend;
static u16 hl_color;
static s16 ui_scroll;

void gfx_set_ui_scroll(int x) { ui_scroll = x; }

// OSRS interface colours: brown stone panels, parchment chatbox, and the game's text colours
// (orange #FF981F labels, yellow #FFFF00 values, dark red #800000 dialogue names...)
static const u16 panel_pal[16] = {
    0x0000, 0x14C7, 0x0C85, 0x258E, 0x0442, 0x214B, 0x1909, 0x190A,
    0x2299, 0x0380, 0x01E0, 0x4AF9, 0x3233, 0x0864, 0x7FFF, 0x001C,
};
static const u16 text_pal[16] = {
    0x0000, 0x7FFF, 0x0E7F, 0x3633, 0x0000, 0x03FF, 0x0E7F, 0x03E0,
    0x001F, 0x03FF, 0x0E7F, 0x7C00, 0x0000, 0x0010, 0x4000, 0x0000,
};

static inline u16 rgb5(int r, int g, int b) { return r | (g << 5) | (b << 10); }
static void build_gradient(void);

static u8 title_pal_on;

// the login screen's grey stone palette and backdrop colours on the panel layer
void gfx_set_title_palette(int on) {
    title_pal_on = on;
    for (int i = 0; i < 16; i++) PAL_BG[i] = on ? title_pal[i] : panel_pal[i];
    for (int y = 0; y < 161; y++) {
        if (on) {
            // login box stone: flat grey with a faint grain
            int v = 11 + ((y * 7) % 5 == 0 ? -1 : 0);
            panel_grad[y] = rgb5(v, v, v);
        }
    }
    if (!on) build_gradient();
}

static void build_gradient(void) {
    // OSRS stone: warm brown, a touch lighter at the top, every fourth line a shade darker
    // for a faint masonry grain
    for (int y = 0; y < 161; y++) {
        int t = y > 159 ? 159 : y;
        int r = 9 - (t * 2) / 159;
        int g = 8 - (t * 2) / 159;
        int b = 6 - (t * 2) / 159;
        if ((y & 3) == 3) r--, g--;
        panel_grad[y] = rgb5(r, g, b);
    }
}

void gfx_init(void) {
    REG_DISPCNT = 0x0080;  // forced blank while loading
    REG_WAITCNT = 0x4317;

    dma3_fill32((void *)VRAM, 0, 0x18000 / 4);
    // tile cache: slot 0 is the permanent blank tile (ROM tile 0, all transparent)
    for (int i = 0; i < CACHE_SLOTS; i++) {
        sowner[i] = 0xFFFF;
        sref[i] = 0;
        sfreed[i] = 0;
    }
    for (int i = 0; i < WORLD_TILE_COUNT; i++) gslot[i] = 0;
    sowner[0] = 0;
    gslot[0] = 1;
    sref[0] = 0x8000;
    for (int i = 0; i < 32 * 32; i++) map_ground[i] = map_overlay[i] = CACHE_BASE;
    dma3_copy32((void *)VRAM_OBJ, obj_tiles, OBJ_TILE_COUNT * 8);
    icon_cache_reset();

    // canvases: unique tile per cell, plus a blank tile for the off-screen columns
    vu16 *m0 = VRAM + SB_TEXT * 0x400;
    vu16 *m1 = VRAM + SB_PANEL * 0x400;
    for (int i = 0; i < 32 * 32; i++) {
        int x = i & 31, y = i >> 5;
        int cell = (x < 30 && y < 20) ? y * 30 + x : 600;
        m0[i] = cell | (1 << 12);
        m1[i] = (601 + cell - 512);
    }

    for (int i = 0; i < 16; i++) {
        PAL_BG[i] = panel_pal[i];
        PAL_BG[16 + i] = text_pal[i];
    }
    for (int i = 0; i < 14 * 16; i++) wpal[i] = world_pal[wpal_set][i];
    for (int i = 0; i < 8 * 16; i++) opal[i] = obj_pal[i];
    for (int i = 8 * 16; i < 256; i++) PAL_OBJ[i] = obj_pal[i];
    build_gradient();
    hl_color = panel_pal[PC_HL1];
    pal_pending = 1;

    REG_BGCNT(0) = 0 | (0 << 2) | (SB_TEXT << 8);
    REG_BGCNT(1) = 1 | (1 << 2) | (SB_PANEL << 8);
    REG_BGCNT(2) = 1 | (2 << 2) | (1 << 6) | (SB_OVER << 8);
    REG_BGCNT(3) = 2 | (2 << 2) | (1 << 6) | (SB_GROUND << 8);
    for (int i = 0; i < 4; i++) {
        REG_BGHOFS(i) = 0;
        REG_BGVOFS(i) = 0;
    }

    for (int i = 0; i < 128; i++) {
        oam_buf[i].a0 = 0x0200;
        oam_buf[i].a1 = oam_buf[i].a2 = oam_buf[i].a3 = 0;
    }
    // affine matrix 0: 2x scale
    oam_buf[0].a3 = 0x80;
    oam_buf[1].a3 = 0;
    oam_buf[2].a3 = 0;
    oam_buf[3].a3 = 0x80;

    REG_DISPSTAT = 1 << 3;
    REG_IE = 1;
    REG_IME = 1;
    REG_DISPCNT = 0x1F40;
}

void gfx_set_world_palette_set(int set) {
    if (set == wpal_set) return;
    wpal_set = set;
    for (int i = 0; i < 14 * 16; i++) wpal[i] = world_pal[set][i];
    pal_pending = 1;
}

void gfx_set_hidden_roof(int group) {
    if (group == hidden_roof) return;
    // the roof being uncovered and the one being hidden may both be on screen: redraw it all
    hidden_roof = group;
    world_dirty = 1;
}

void gfx_set_world_fade(int level16) {
    if (level16 < 0) level16 = 0;
    if (level16 > 16) level16 = 16;
    if (fade_level != level16) {
        fade_level = level16;
        pal_pending = 1;
    }
}

void gfx_set_mosaic(int n) { (void)n; mosaic_n = 0; }   // OSRS never blurs the world
void gfx_set_ui_alpha(int eva, int text_too) {
    ui_eva = eva;
    ui_text_blend = text_too;
}
void gfx_mark_world_dirty(void) {
    world_dirty = 1;
    memo.p = -1;
}

void gfx_mark_cell_dirty(int x, int y, int p) {
    memo.p = -1;
    if (p != pl.plane) return;   // only the player's floor is drawn
    if (n_dirty_cells >= MAX_CELL_DIRTY) {
        world_dirty = 1;
        return;
    }
    dirty_cells[n_dirty_cells][0] = x - WORLD_X0;   // cell column / row in pixel space
    dirty_cells[n_dirty_cells][1] = WORLD_Y1 - y;
    n_dirty_cells++;
}

void gfx_set_hl_pulse(int phase) {
    // phase 0..63 -> gentle breathing of the hovered row (lighter stone)
    int t = phase < 32 ? phase : 63 - phase;
    hl_color = rgb5(11 + t / 11, 9 + t / 11, 7 + t / 16);
}

static u16 scale_col(u16 c, int f) {
    int r = (c & 31) * f >> 4, g = ((c >> 5) & 31) * f >> 4, b = ((c >> 10) & 31) * f >> 4;
    return r | (g << 5) | (b << 10);
}

static void upload_palettes(void) {
    int f = fade_level;
    for (int i = 0; i < 14 * 16; i++) PAL_BG[32 + i] = f == 16 ? wpal[i] : scale_col(wpal[i], f);
    for (int i = 0; i < 8 * 16; i++) {
        int bank = i >> 4;
        // FX stay bright; so does the player bank in the menu (portrait / paper-doll)
        int keep = bank == 6 || (bank == 0 && game_state == GS_MENU);
        PAL_OBJ[i] = (f == 16 || keep) ? opal[i] : scale_col(opal[i], f);
    }
}

void gfx_set_player_palette(void) {
    avatar_palette(opal);   // tier colours for the composited player (avatar.c)
    pal_pending = 1;
}

// ---------------------------------------------------------------------------------------
// The tile cache and the window redraw run for every on-screen tile, so they are ARM code in
// IWRAM. A full redraw (teleport, staircase, roof change) must fit in a frame with the rest of it.

// Loads ROM tile gt into a free slot and returns the slot (0 when the cache is full: retried
// with a full redraw next update)
IWRAM_CODE __attribute__((noinline)) static int cache_load(u32 gt) {
    for (int n = 1; n < CACHE_SLOTS; n++) {
        int k = clock_hand;
        if (++clock_hand >= CACHE_SLOTS) clock_hand = 1;
        if (sref[k] || sfreed[k] == upd_serial) continue;
        if (sowner[k] != 0xFFFF) gslot[sowner[k]] = 0;
        sowner[k] = gt;
        gslot[gt] = k + 1;
        sref[k] = 1;
        // the slot is not on screen (nor in the screenblock DMA'd last vblank): copy directly
        typedef struct { u32 w[8]; } Tile;   // copied with ldm/stm
        *(Tile *)(0x06008000 + (CACHE_BASE + k) * 32) = *(const Tile *)&world_tiles[gt * 8];
        cache_loads++;
        return k;
    }
    cache_misses++;
    cache_retry = 1;
    return 0;
}

// Returns the cache slot holding ROM tile gt, loading it into a free slot if needed. Slot 0 is
// the permanent blank tile (ROM tile 0) and is not counted.
static inline __attribute__((always_inline)) int cache_get(u32 gt) {
    if (!gt) return 0;
    int s = gslot[gt];
    if (s) {
        sref[s - 1]++;
        return s - 1;
    }
    return cache_load(gt);
}

static inline __attribute__((always_inline)) void cache_put(u32 entry) {
    int s = (entry & 0x3FF) - CACHE_BASE;
    if (s > 0 && --sref[s] == 0) sfreed[s] = upd_serial;
}

// screen entry for metatile entry e (low 16: ROM tile, high 16: flip/palette attributes)
static inline __attribute__((always_inline)) u32 cache_entry(u32 e) {
    return (CACHE_BASE + cache_get(e & 0xFFFF)) | (e >> 16);
}

// tx, ty: 8x8 tile in pixel space (two per cell); the player's floor is drawn
static void draw_tile(int tx, int ty) {
    int i = (ty & 31) * 32 + (tx & 31);
    u32 ge = 0, oe = 0;
    int x = WORLD_X0 + (tx >> 1), y = WORLD_Y1 - (ty >> 1), p = pl.plane;
    if (x != memo.x || y != memo.y || p != memo.p) {
        memo.x = x, memo.y = y, memo.p = p;
        memo.in = tx >= 0 && ty >= 0 && map_in(x, y, p);
        if (memo.in) {
            int meta, over, roof;
            world_cell_gfx(x, y, p, &meta, &over, &roof);
            memo.meta = meta, memo.over = over, memo.roof = roof;
        }
    }
    if (memo.in) {
        int k = ((ty & 1) << 1) | (tx & 1);
        ge = meta_tiles[memo.meta][k];
        if (!hidden_roof || memo.roof != hidden_roof) oe = meta_tiles[memo.over][k];
    }
    u16 g = cache_entry(ge);
    u16 o = cache_entry(oe);
    cache_put(map_ground[i]);
    cache_put(map_overlay[i]);
    map_ground[i] = g;
    map_overlay[i] = o;
}

static inline __attribute__((always_inline)) void clear_tile(int tx, int ty) {
    int i = (ty & 31) * 32 + (tx & 31);
    cache_put(map_ground[i]);
    cache_put(map_overlay[i]);
    map_ground[i] = map_overlay[i] = CACHE_BASE;
}

#define WIN_W 31
#define WIN_H 21

static const u32 no_tiles[4];   // blank, as draw_tile() leaves a cell off the map or a hidden roof

// one 8x8 tile of the window: the screen entry for metatile entry e, counted in its slot
#define REDRAW_ENTRY(dst, e) do { \
    u32 e_ = (e), t_ = e_ & 0xFFFF, s_; \
    if (!t_) s_ = 0; \
    else if ((s_ = gslot[t_]) != 0) sref[--s_]++; \
    else s_ = cache_load(t_); \
    (dst) = (CACHE_BASE + s_) | (e_ >> 16); \
} while (0)

// The whole window at tile tx0, ty0. Releasing the old window entry by entry and drawing the
// new one with draw_tile() took two to three frames, so instead every slot the old window used
// is freed at once (as a release would: not reused before the next update), and the new window
// is drawn a row at a time, with one map lookup per row of cells.
IWRAM_CODE __attribute__((noinline)) static void redraw_window(int tx0, int ty0) {
    u16 serial = upd_serial;
    u32 *ref2 = (u32 *)sref;   // two slots at a time: most are not on screen
    for (int s = 0; s < CACHE_SLOTS; s += 2) {
        u32 r = ref2[s >> 1];
        if (!r) continue;
        if (s && (r & 0xFFFF)) sfreed[s] = serial;   // slot 0 (the blank tile) is never released
        if (r >> 16) sfreed[s + 1] = serial;
        ref2[s >> 1] = s ? 0 : (r & 0xFFFF);
    }
    // entries outside the window stay blank
    for (int ty = ty0 + WIN_H; ty < ty0 + 32; ty++) {
        u32 *g = (u32 *)&map_ground[(ty & 31) * 32], *o = (u32 *)&map_overlay[(ty & 31) * 32];
        for (int n = 0; n < 16; n++) g[n] = o[n] = CACHE_BASE | CACHE_BASE << 16;
    }
    int p = pl.plane, hide = hidden_roof;
    int cx0 = tx0 >> 1, ncell = ((tx0 + WIN_W - 1) >> 1) - cx0 + 1;
    const u32 *gm[WIN_W / 2 + 2], *om[WIN_W / 2 + 2];   // the metatiles of one row of cells
    for (int ty = ty0; ty < ty0 + WIN_H; ty++) {
        if (ty == ty0 || !(ty & 1)) {
            int cy = ty >> 1, y = WORLD_Y1 - cy;
            for (int c = 0; c < ncell; c++) gm[c] = om[c] = no_tiles;
            const u16 *b = map_bounds[p];
            int xa = WORLD_X0 + (cx0 < 0 ? 0 : cx0), xb = WORLD_X0 + cx0 + ncell - 1;
            if (xa < b[0]) xa = b[0];
            if (xb > b[2]) xb = b[2];
            if (cy >= 0 && y >= b[1] && y <= b[3] && xa <= xb) {
                u16 meta[WIN_W / 2 + 2], over[WIN_W / 2 + 2];
                u8 roof[WIN_W / 2 + 2];
                world_row_gfx(xa, y, p, xb - xa + 1, meta, over, roof);
                for (int n = 0, c = xa - WORLD_X0 - cx0; n <= xb - xa; n++, c++) {
                    gm[c] = meta_tiles[meta[n]];
                    if (!hide || roof[n] != hide) om[c] = meta_tiles[over[n]];
                }
            }
        }
        u16 *g = &map_ground[(ty & 31) * 32], *o = &map_overlay[(ty & 31) * 32];
        int k = (ty & 1) << 1, tx = tx0;
        for (; tx < tx0 + WIN_W; tx++) {
            int c = (tx >> 1) - cx0, kk = k | (tx & 1);
            REDRAW_ENTRY(g[tx & 31], gm[c][kk]);
            REDRAW_ENTRY(o[tx & 31], om[c][kk]);
        }
        g[tx & 31] = o[tx & 31] = CACHE_BASE;   // the 32nd column
    }
}

void gfx_world_update(void) {
    int tx0 = cam_x >> 3, ty0 = cam_y >> 3;
    if (++upd_serial == 0) upd_serial = 1;
    int dx = tx0 - drawn_tx, dy = ty0 - drawn_ty;
    int jump = dx > 4 || dx < -4 || dy > 4 || dy < -4 || pl.plane != drawn_plane;
    if (world_dirty || cache_retry || jump) {
        // A teleport or a staircase: the frame that moves the player also redraws the HUD and
        // decompresses map blocks, and together with the redraw it would drop a frame. Show
        // the world black for this frame (as the warp fade already has it) and redraw next.
        if (jump && !world_hidden) {
            world_hidden = 1;
            return;
        }
        world_hidden = 0;
        drawn_plane = pl.plane;
        world_dirty = cache_retry = 0;
        n_dirty_cells = 0;
        drawn_tx = tx0;
        drawn_ty = ty0;
        redraw_window(tx0, ty0);
        maps_pending = 1;
        return;
    }
    if (!dx && !dy && !n_dirty_cells) return;
    while (drawn_tx < tx0) {
        for (int ty = drawn_ty; ty < drawn_ty + WIN_H; ty++) clear_tile(drawn_tx, ty);
        drawn_tx++;
        for (int ty = drawn_ty; ty < drawn_ty + WIN_H; ty++) draw_tile(drawn_tx + WIN_W - 1, ty);
    }
    while (drawn_tx > tx0) {
        for (int ty = drawn_ty; ty < drawn_ty + WIN_H; ty++) clear_tile(drawn_tx + WIN_W - 1, ty);
        drawn_tx--;
        for (int ty = drawn_ty; ty < drawn_ty + WIN_H; ty++) draw_tile(drawn_tx, ty);
    }
    while (drawn_ty < ty0) {
        for (int tx = drawn_tx; tx < drawn_tx + WIN_W; tx++) clear_tile(tx, drawn_ty);
        drawn_ty++;
        for (int tx = drawn_tx; tx < drawn_tx + WIN_W; tx++) draw_tile(tx, drawn_ty + WIN_H - 1);
    }
    while (drawn_ty > ty0) {
        for (int tx = drawn_tx; tx < drawn_tx + WIN_W; tx++) clear_tile(tx, drawn_ty + WIN_H - 1);
        drawn_ty--;
        for (int tx = drawn_tx; tx < drawn_tx + WIN_W; tx++) draw_tile(tx, drawn_ty);
    }
    // cells changed at runtime (stumps, mined rocks): redraw their 2x2 tiles if visible
    for (int n = 0; n < n_dirty_cells; n++) {
        int cx = dirty_cells[n][0], cy = dirty_cells[n][1];
        for (int k = 0; k < 4; k++) {
            int tx = cx * 2 + (k & 1), ty = cy * 2 + (k >> 1);
            if (tx >= drawn_tx && tx < drawn_tx + WIN_W && ty >= drawn_ty && ty < drawn_ty + WIN_H) draw_tile(tx, ty);
        }
    }
    n_dirty_cells = 0;
    maps_pending = 1;
}

// ---------------------------------------------------------------------------------------
void obj_sprite(int x, int y, int tile, int pal, int prio, int hflip, int size16, int flags) {
    if (oam_n >= 128) return;
    int w = (flags & (OBJF_AFFINE2X | OBJF_32)) ? 32 : (flags & OBJF_8x8) ? 8 : 16;
    int h = (flags & OBJF_WIDE) ? 8 : (flags & OBJF_TALL) ? w * 2 : w;
    if (x <= -w || x >= SCREEN_W || y <= -h || y >= SCREEN_H) return;
    ObjAttr *o = &oam_buf[oam_n++];
    u16 a0 = (y & 0xFF);
    if (flags & OBJF_MOSAIC) a0 |= 1 << 12;
    if (flags & OBJF_SEMI) a0 |= 1 << 10;
    if (flags & OBJF_WIDE) a0 |= 1 << 14;
    if (flags & OBJF_TALL) a0 |= 2 << 14;   // vertical: 16x32 (32x64 with AFFINE2X)
    if (pal == 0xFF) a0 |= 1 << 13;   // 8bpp icon
    u16 a1 = (x & 0x1FF);
    if (flags & OBJF_AFFINE2X) {
        a0 |= 3 << 8;                  // affine, double size
        a1 |= (flags & OBJF_TALL ? 2 : 1) << 14;   // 16x16 or 16x32 source
    } else {
        if (hflip & 1) a1 |= 1 << 12;
        if (hflip & 2) a1 |= 1 << 13;   // vertical flip
        if (flags & OBJF_32) a1 |= 2 << 14;
        else if (flags & OBJF_TALL) a1 |= 2 << 14;
        else if (!(flags & OBJF_8x8)) a1 |= (size16 ? 1 : 0) << 14;
    }
    o->a0 = a0;
    o->a1 = a1;
    o->a2 = (tile & 0x3FF) | ((prio & 3) << 10) | (((pal == 0xFF ? 0 : pal) & 15) << 12);
}

// ---------------------------------------------------------------------------------------
// Item icons (16x16, 8bpp) are streamed from ROM into an OBJ VRAM cache after the dynamic
// tiles, so the number of icons is not limited by VRAM. A slot is reused only once neither
// the OAM being built nor the one on screen (built last frame) references it.
#define ICON_BASE ((DYN_TILE_BASE + 48 + 1) & ~1)       // 8bpp tiles must start on an even index
#define ICON_SLOTS ((1024 - ICON_BASE) / 8)
static u8 icon_where[ICON_COUNT];                        // slot + 1, 0 = not loaded
static u8 slot_icon[ICON_SLOTS];
static u32 slot_used[ICON_SLOTS];
static u8 icon_hand;
u16 icon_loads;                                          // debug counter

static void icon_cache_reset(void) {
    for (int i = 0; i < ICON_COUNT; i++) icon_where[i] = 0;
    for (int i = 0; i < ICON_SLOTS; i++) slot_icon[i] = 0xFF, slot_used[i] = 0;
}

static int icon_vram_tile(int icon) {
    int s = icon_where[icon];
    if (s) {
        slot_used[s - 1] = frame;
        return ICON_BASE + (s - 1) * 8;
    }
    for (int n = 0; n < ICON_SLOTS; n++) {
        int k = icon_hand;
        if (++icon_hand >= ICON_SLOTS) icon_hand = 0;
        if (slot_icon[k] != 0xFF && slot_used[k] + 1 >= frame) continue;
        if (slot_icon[k] != 0xFF) icon_where[slot_icon[k]] = 0;
        slot_icon[k] = icon;
        icon_where[icon] = k + 1;
        slot_used[k] = frame;
        dma3_copy32((void *)(VRAM_OBJ + (ICON_BASE + k * 8) * 16), &icon_gfx[icon * 64], 64);
        icon_loads++;
        return ICON_BASE + k * 8;
    }
    return -1;
}

void obj_icon(int x, int y, int icon, int prio) {
    if (x <= -16 || x >= SCREEN_W || y <= -16 || y >= SCREEN_H || oam_n >= 128) return;
    int t = icon_vram_tile(icon);
    if (t >= 0) obj_sprite(x, y, t, 0xFF, prio, 0, 1, 0);
}

void obj_icon_flip(int x, int y, int icon, int prio, int hflip, int vflip) {
    if (x <= -16 || x >= SCREEN_W || y <= -16 || y >= SCREEN_H || oam_n >= 128) return;
    int t = icon_vram_tile(icon);
    if (t >= 0) obj_sprite(x, y, t, 0xFF, prio, (hflip ? 1 : 0) | (vflip ? 2 : 0), 1, 0);
}

void obj_hide_rest(void) {
    for (int i = oam_n; i < 128; i++) oam_buf[i].a0 = 0x0200;
}

// 3x5 digits for hitsplats
static const u8 digits3x5[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

void gfx_dyn_hitsplat(int slot, int dmg) {
    u32 buf[32];
    const u32 *src = &obj_tiles[(dmg ? SPR_SPLAT_RED : SPR_SPLAT_BLUE) * 8];
    for (int i = 0; i < 32; i++) buf[i] = src[i];
    char d[4];
    int n = 0;
    if (dmg >= 10) d[n++] = dmg / 10 % 10;
    d[n++] = dmg % 10;
    int width = n * 4 - 1;
    int x0 = 8 - (width + 1) / 2, y0 = 5;
    for (int k = 0; k < n; k++)
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (digits3x5[(int)d[k]][r] & (4 >> c)) {
                    int x = x0 + k * 4 + c, y = y0 + r;
                    int tile = (y >> 3) * 2 + (x >> 3);
                    u32 *row = &buf[tile * 8 + (y & 7)];
                    int sh = (x & 7) * 4;
                    *row = (*row & ~(0xFu << sh)) | (2u << sh);
                }
    dma3_copy32((void *)(VRAM_OBJ + (DYN_TILE_BASE + slot * 4) * 16), buf, 32);
}

// ---------------------------------------------------------------------------------------
void gfx_vblank(void) {
    dma3_copy32((void *)OAM_MEM, oam_buf, 256);
    avatar_vblank();

    REG_DISPCNT = world_hidden ? 0x1340 : 0x1F40;   // BG2/BG3: the world
    REG_BGHOFS(0) = ui_scroll;
    REG_BGHOFS(1) = ui_scroll;
    REG_BGHOFS(2) = cam_x;
    REG_BGVOFS(2) = cam_y;
    REG_BGHOFS(3) = cam_x;
    REG_BGVOFS(3) = cam_y;
    if (maps_pending) {
        dma3_copy32((void *)(VRAM + SB_GROUND * 0x400), map_ground, 512);
        dma3_copy32((void *)(VRAM + SB_OVER * 0x400), map_overlay, 512);
        maps_pending = 0;
    }

    // water shimmer: swap the two highlight shades every 24 frames
    if (frame % 24 == 0) {
        const u16 *ws = water_slots[wpal_set];
        for (int i = 0; ws[i] && ws[i + 1]; i += 2) {
            int a = ws[i] - 32, b = ws[i + 1] - 32;
            u16 t = wpal[a];
            wpal[a] = wpal[b];
            wpal[b] = t;
        }
        pal_pending = 1;
    }
    if (pal_pending) {
        upload_palettes();
        pal_pending = 0;
    }
    if (!title_pal_on) PAL_BG[PC_HL1] = hl_color;

    REG_MOSAIC = mosaic_n | (mosaic_n << 4) | (mosaic_n << 8) | (mosaic_n << 12);
    REG_BLDCNT = (ui_text_blend ? 0x0003 : 0x0002) | 0x0040 | 0x3E00;
    int eva = ui_eva >= 12 ? 16 : ui_eva * 16 / 12;
    REG_BLDALPHA = eva | ((16 - eva) << 8);

    cv_flush(&cv_panel, 10);
    cv_flush(&cv_text, 10);

    // the panel gradient HBlank DMA is re-armed by the VBlank IRQ handler (crt0.s)
}
