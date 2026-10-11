// The overworld: map state, ground items, movement, NPC AI, the game tick and per-frame
// update. Combat is in combat.c, skilling and object interaction in skilling.c, sprites in
// world_draw.c; they share world_internal.h.
#include "world_internal.h"

EWRAM_BSS Npc npcs[MAX_NPCS];
_Static_assert(sizeof(Npc) == 32, "tools/qtest.py NPC_SIZE");
Player pl;
s16 cam_x, cam_y;

Fire fires[MAX_FIRES];
Ground ground[MAX_GROUND];
Depleted depleted[MAX_DEPLETED];
Splat splats[MAX_SPLATS];
static u8 splat_next;
int tick_frames;
static int regen_ticks;
static u8 ispawn_timer[ITEM_SPAWN_COUNT + 1];   // ticks until a respawning ground item reappears
static u8 kind_vis[NPC_KIND_COUNT];             // quest NPC kinds currently shown

const s8 DX[4] = {0, 0, -1, 1};
const s8 DY[4] = {-1, 1, 0, 0};    // tiles: y grows north
const s8 SDY[4] = {1, -1, 0, 0};   // screen: y grows south


// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
// Map state. The map lives in ROM (map.h); runtime changes such as stumps and mined-out rocks
// are a small override table keyed by cell, with a filter so lookups for unchanged cells cost
// one bit test.
typedef struct { u32 cell; u16 meta, over; u8 type, mask; } Override;
static Override ovr[MAX_OVR];
int n_ovr;

static const Override *ovr_find(u32 cell) {
    if (!ovr_marked(cell)) return 0;
    for (int i = 0; i < n_ovr; i++)
        if (ovr[i].cell == cell) return &ovr[i];
    return 0;
}

int ovr_set(int x, int y, int p, int mask, int meta, int over, int type) {
    u32 cell = map_key(x, y, p);
    Override *o = (Override *)ovr_find(cell);
    if (!o) {
        if (n_ovr >= MAX_OVR) return 0;
        o = &ovr[n_ovr++];
        o->cell = cell;
        o->mask = 0;
        ovr_mark(cell);
    }
    o->mask |= mask;
    if (mask & OV_META) o->meta = meta;
    if (mask & OV_OVER) o->over = over;
    if (mask & OV_TYPE) o->type = type;
    return 1;
}

void ovr_clear(int x, int y, int p, int mask) {
    u32 cell = map_key(x, y, p);
    Override *o = (Override *)ovr_find(cell);
    if (!o) return;
    o->mask &= ~mask;
    if (o->mask) return;
    *o = ovr[--n_ovr];
    for (int i = 0; i < 32; i++) ovr_filter[i] = 0;   // filter bits may be shared: rebuild it
    for (int i = 0; i < n_ovr; i++) ovr_mark(ovr[i].cell);
}

int world_type(int x, int y, int p) {
    if (!in_map(x, y, p)) return T_WATER;
    const Override *o = ovr_find(map_key(x, y, p));
    return (o && (o->mask & OV_TYPE)) ? o->type : map_type_at(x, y, p);
}

int world_meta(int x, int y, int p) {
    const Override *o = ovr_find(map_key(x, y, p));
    return (o && (o->mask & OV_META)) ? o->meta : map_meta_at(x, y, p);
}

int world_over(int x, int y, int p) {
    const Override *o = ovr_find(map_key(x, y, p));
    return (o && (o->mask & OV_OVER)) ? o->over : map_over_at(x, y, p);
}

// a roof byte with bit 7 set is the eave of group (r & 0x7F) hanging over a cell outside it (walls
// stand a few pixels tall, so a roof drawn on top of them reaches into the row north of its
// building): drawn and hidden with that roof, but nobody standing there is under it
int world_roof(int x, int y, int p) {
    int r = map_roof_at(x, y, p);
    return r & 0x80 ? 0 : r;
}

// everything draw_tile() needs from one cell (the cell must be in_map)
void world_cell_gfx(int x, int y, int p, int *meta, int *over, int *roof) {
    const MapBlock *b = map_block(x, y, p);
    int i = map_cell(x, y);
    *meta = b->meta[i];
    *over = b->over[i];
    *roof = b->roof[i];
    const Override *o = n_ovr ? ovr_find(map_key(x, y, p)) : 0;
    if (o) {
        if (o->mask & OV_META) *meta = o->meta;
        if (o->mask & OV_OVER) *over = o->over;
    }
}

int res_alt_meta(int x, int y, int p) {
    u32 cell = map_key(x, y, p);
    int lo = 0, hi = RES_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (res_cell[mid] == cell) return res_alt[mid];
        if (res_cell[mid] < cell) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}


int fire_at(int x, int y, int p) {
    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active && fires[i].x == x && fires[i].y == y && fires[i].plane == p) return i;
    return -1;
}

// NPC occupancy (map.h): free_tile() runs for every NPC step, so it must not scan all NPCs
int npc_hi;                       // npcs[] slots in use (spawn table size)

int npc_at(int x, int y, int p) {
    if (!in_map(x, y, p) || !occ_get(x, y, p)) return -1;
    for (int i = 0; i < npc_hi; i++)
        if (npcs[i].active && npcs[i].x == x && npcs[i].y == y && npcs[i].plane == p) return i;
    return -1;
}

int spot_at(int x, int y, int p) {
    for (int i = 0; i < FISHSPOT_COUNT; i++)
        if (fish_spots[i].x == x && fish_spots[i].y == y && fish_spots[i].plane == p) return i;
    return -1;
}

// terrain only: fires don't block (walk over them as in OSRS). From the regions' walkability
// bitmaps, so NPCs anywhere in the world move without decompressing their surroundings.
int walkable(int x, int y, int p) {
    if (!in_map(x, y, p)) return 0;
    const Override *o = n_ovr ? ovr_find(map_key(x, y, p)) : 0;
    if (o && (o->mask & OV_TYPE)) return o->type <= T_WALKABLE_MAX;
    return map_walk_at(x, y, p);
}

int free_tile(int x, int y, int p) {
    return walkable(x, y, p) && npc_at(x, y, p) < 0 && !(pl.x == x && pl.y == y && pl.plane == p);
}

// a wall on the edge between (x, y) and its neighbour in direction dir (walls stand on tile
// edges, as in OSRS); shot: only walls that stop projectiles (fences do not)
int wall_dir(int x, int y, int p, int dir, int shot) {
    int k = shot ? 2 : 0;
    switch (dir) {
    case DIR_UP: return map_edge_at(x, y, p, k);
    case DIR_DOWN: return map_edge_at(x, y - 1, p, k);
    case DIR_LEFT: return map_edge_at(x, y, p, k + 1);
    default: return map_edge_at(x + 1, y, p, k + 1);
    }
}

// one step from (x, y) in direction dir onto a free tile, through no wall
int step_free(int x, int y, int p, int dir) {
    return !wall_dir(x, y, p, dir, 0) && free_tile(x + DX[dir], y + DY[dir], p);
}

// ---------------------------------------------------------------------------------------
// Zones. The castle's upper floors are planes 1 and 2 above the ground; zones[] gives each
// floor's rectangle (the camera stays inside it), palette set and plane. Staircases warp between
// them with a short fade from their quest scripts (OP_WARP). On the ground the world is seamless:
// the palette set follows the camera (pal_regions[]), swapping only while the old set's town is
// off screen.
static u8 cur_zone = 0xFF, cur_roof;
static struct { u8 phase, t, dir, plane; s16 x, y; } warp;   // phase 1 fading out, 2 fading in
#define WARP_FRAMES 8

// where the map should be loaded: the camera's centre, or where a staircase or teleport is going
void world_focus(int *x, int *y, int *p) {
    if (warp.phase == 1) {
        *x = warp.x, *y = warp.y, *p = warp.plane;
        return;
    }
    *x = WORLD_X0 + ((cam_x + SCREEN_W / 2) >> 4);
    *y = WORLD_Y1 - ((cam_y + SCREEN_H / 2) >> 4);
    *p = pl.plane;
}

int world_zone(int x, int y, int p) {
    for (int i = 0; i < ZONE_COUNT; i++)
        if (zones[i].plane == p && x >= zones[i].x0 && x <= zones[i].x1 && y >= zones[i].y0 && y <= zones[i].y1)
            return i;
    return 0;
}

// Chebyshev distance from the player to an NPC; NPCs on another floor are out of reach
static int dist(int ax, int ay, int bx, int by);
static int npc_dist(const Npc *n) { return n->plane == pl.plane ? dist(n->x, n->y, pl.x, pl.y) : 0x7FFF; }

// zone (floor) and the roof the player is under; call after the player moves
static void zone_refresh(void) {
    int z = world_zone(pl.x, pl.y, pl.plane);
    if (z != cur_zone) {
        cur_zone = z;
        if (zones[z].plane) gfx_set_world_palette_set(zones[z].pal_set);
        gfx_mark_world_dirty();
    }
    int r = world_roof(pl.x, pl.y, pl.plane);
    if (r != cur_roof) {
        cur_roof = r;
        gfx_set_hidden_roof(r);
    }
}

void player_warp(int x, int y, int p, int dir) {
    player_stop();
    warp.phase = 1;
    warp.t = 0;
    warp.x = x;
    warp.y = y;
    warp.plane = p;
    warp.dir = dir;
}
int world_warping(void) { return warp.phase != 0; }

static void warp_update(void) {
    if (warp.phase == 1) {
        gfx_set_world_fade(16 - (++warp.t) * 16 / WARP_FRAMES);
        if (warp.t < WARP_FRAMES) return;
        player_tp(warp.x, warp.y, warp.plane, warp.dir);
        zone_refresh();
        warp.phase = 2;
        warp.t = 0;
    } else {
        gfx_set_world_fade((++warp.t) * 16 / WARP_FRAMES);
        if (warp.t >= WARP_FRAMES) warp.phase = 0;
    }
}

int world_region(void) {
    for (int i = 0; i < REGION_COUNT; i++) {
        const Region *r = &regions[i];
        if (pl.plane == r->plane && pl.x >= r->x0 && pl.x <= r->x1 && pl.y >= r->y0 && pl.y <= r->y1) return i;
    }
    return REGION_COUNT - 1;
}

// Test scripts move an NPC by filling this in (tools/qtest.py npcpos): npc index + 1, then the
// tile. Applied at the start of the next frame, with its occupancy moved too.
struct { u16 npc; s16 x, y; u8 plane; } dbg_npc_move;

void world_debug(void) {
    if (!dbg_npc_move.npc) return;
    Npc *n = &npcs[dbg_npc_move.npc - 1];
    dbg_npc_move.npc = 0;
    if (n->active) occ_clear(n->x, n->y, n->plane);
    n->x = dbg_npc_move.x;
    n->y = dbg_npc_move.y;
    n->plane = dbg_npc_move.plane;
    n->px = TILE_PX(n->x);
    n->py = TILE_PY(n->y);
    n->moving = 0;
    if (n->active) occ_set(n->x, n->y, n->plane);
}

// ---------------------------------------------------------------------------------------
void world_init(void) {
    n_ovr = 0;
    map_reset();
    for (int i = 0; i < MAX_NPCS; i++) npcs[i].active = 0;
    npc_hi = SPAWN_COUNT < MAX_NPCS ? SPAWN_COUNT : MAX_NPCS;
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        n->active = 1;
        n->kind = spawns[i].kind;
        n->x = n->hx = spawns[i].x;
        n->y = n->hy = spawns[i].y;
        n->plane = spawns[i].plane;
        n->px = TILE_PX(n->x);
        n->py = TILE_PY(n->y);
        n->dir = DIR_DOWN;
        n->hp = npc_defs[n->kind].hp;
        n->moving = 0;
        n->in_combat = 0;
        n->respawn = 0;
        n->attack_cd = 0;
        n->hpbar_timer = 0;
        n->curse = n->bound = 0;
        occ_set(n->x, n->y, n->plane);
    }
    for (int i = 0; i < MAX_FIRES; i++) fires[i].active = 0;
    for (int i = 0; i < MAX_GROUND; i++) ground[i].id = 0;
    for (int i = 0; i < MAX_DEPLETED; i++) depleted[i].active = 0;
    for (int i = 0; i < MAX_SPLATS; i++) splats[i].timer = 0;
    for (int i = 0; i < ITEM_SPAWN_COUNT; i++) ispawn_timer[i] = 1;
    for (int i = 0; i < NPC_KIND_COUNT; i++) kind_vis[i] = 1;   // every spawn starts active
    shops_reset();
    world_refresh_npcs();
    gfx_mark_world_dirty();
}

// ---------------------------------------------------------------------------------------
// Quest support: NPCs that exist only during part of a quest, scripted movement
void npc_set_active(int i, int on) {
    Npc *n = &npcs[i];
    if (on == n->active) return;
    if (on) {
        // a wanderer may be standing on the spot: use the nearest free cell instead (rings
        // searched top row first, as they appear on screen)
        int sx = -1, sy = -1;
        for (int r = 0; r <= 2 && sx < 0; r++)
            for (int dy = r; dy >= -r && sx < 0; dy--)
                for (int dx = -r; dx <= r && sx < 0; dx++)
                    if ((dx == r || dx == -r || dy == r || dy == -r) && free_tile(n->hx + dx, n->hy + dy, n->plane))
                        sx = n->hx + dx, sy = n->hy + dy;
        if (sx < 0) return;
        n->active = 1;
        n->x = sx;
        n->y = sy;
        n->px = TILE_PX(n->x);
        n->py = TILE_PY(n->y);
        n->hp = npc_defs[n->kind].hp;
        n->moving = n->in_combat = n->hpbar_timer = n->state = 0;
        n->curse = n->bound = 0;
        n->respawn = 0;
        occ_set(n->x, n->y, n->plane);
    } else {
        n->active = 0;
        n->in_combat = 0;
        foe_release(i);
        occ_clear(n->x, n->y, n->plane);
        if (pl.action == ACT_COMBAT && pl.target == i) player_stop();
    }
}

void world_refresh_npcs(void) {
    for (int k = 0; k < NPC_KIND_COUNT; k++) {
        int v = quest_npc_visible(k);
        if (v < 0 || v == kind_vis[k]) continue;
        kind_vis[k] = v;
        for (int i = 0; i < npc_hi; i++)
            if (npcs[i].kind == k) npc_set_active(i, v);
    }
}

// centre on the player, kept inside the player's zone; on the ground, load the palette set of
// the town the camera is near (any set draws the countryside: its banks are shared)
void world_camera(void) {
    const Zone *z = &zones[world_zone(pl.x, pl.y, pl.plane)];
    int x0 = TILE_PX(z->x0), y0 = TILE_PY(z->y1), x1 = TILE_PX(z->x1 + 1) - SCREEN_W, y1 = TILE_PY(z->y0 - 1) - SCREEN_H;
    cam_x = pl.px + 8 - SCREEN_W / 2;
    cam_y = pl.py + 8 - SCREEN_H / 2;
    if (cam_x > x1) cam_x = x1;
    if (cam_y > y1) cam_y = y1;
    if (cam_x < x0) cam_x = x0;
    if (cam_y < y0) cam_y = y0;
    if (z->plane) return;
    int cx = WORLD_X0 + (cam_x >> 4), cy = WORLD_Y1 - (cam_y >> 4);   // the tile at the top-left
    for (int i = 0; i < PAL_REGION_COUNT; i++) {
        const PalRegion *r = &pal_regions[i];
        if (cx >= r->x0 && cx <= r->x1 && cy >= r->y0 && cy <= r->y1) {
            gfx_set_world_palette_set(r->pal_set);
            break;
        }
    }
}

void player_tp(int x, int y, int p, int dir) {
    player_stop();
    pl.x = x;
    pl.y = y;
    pl.plane = p;
    pl.px = TILE_PX(x);
    pl.py = TILE_PY(y);
    pl.dir = dir;
    pl.moving = 0;
}

int player_can_walk_through(void) {
    return free_tile(pl.x + DX[pl.dir] * 2, pl.y + DY[pl.dir] * 2, pl.plane);
}

int player_walk_through(void) {
    int nx = pl.x + DX[pl.dir] * 2, ny = pl.y + DY[pl.dir] * 2;
    if (!free_tile(nx, ny, pl.plane)) return 0;
    pl.x = nx;
    pl.y = ny;
    pl.moving = 1;   // glide across the door / past the guard
    return 1;
}

int npc_portrait_tile(int kind) {
    static const u16 base[] = {
        [SPR_KIND_HUMAN] = SPR_HUM_D0, [SPR_KIND_COW] = SPR_COW0, [SPR_KIND_CHICKEN] = SPR_CHK0,
        [SPR_KIND_WIZARD] = SPR_WIZ_D0, [SPR_KIND_WITCH] = SPR_WITCH_D0, [SPR_KIND_PRIEST] = SPR_PRI_D0,
        [SPR_KIND_DRESS] = SPR_DRESS_D0, [SPR_KIND_SKELETON] = SPR_SKEL_D0, [SPR_KIND_GHOST] = SPR_GHOST0,
        [SPR_KIND_SHEEP] = SPR_SHEEP0, [SPR_KIND_IMP] = SPR_IMP0, [SPR_KIND_RAT] = SPR_RAT0,
        [SPR_KIND_DEMON] = SPR_DEMON0,
    };
    return base[npc_defs[kind].sprite];
}

void world_after_load(void) {
    home_cancel();
    target_set(-1);
    proj_clear();
    // saves from an older map layout may place the player inside a wall: go home
    if (!in_map(pl.x, pl.y, pl.plane) || world_type(pl.x, pl.y, pl.plane) > T_WALKABLE_MAX) {
        pl.x = START_X;
        pl.y = START_Y;
        pl.plane = START_P;
    }
    pl.px = TILE_PX(pl.x);
    pl.py = TILE_PY(pl.y);
    pl.moving = 0;
    pl.action = ACT_NONE;
    warp.phase = 0;
    cur_zone = 0xFF;
    cur_roof = 0xFF;
    zone_refresh();
    gfx_set_player_palette();
    world_refresh_npcs();
}

void world_new_game(void) {
    Player *p = &pl;
    for (int i = 0; i < 28; i++) p->inv[i].id = p->inv[i].qty = 0;
    for (int i = 0; i < 96; i++) p->bank[i].id = p->bank[i].qty = 0;
    for (int i = 0; i < EQ_COUNT; i++) p->equip[i] = 0;
    p->ammo_qty = 0;
    p->spell = 0;
    p->home_cd = 0;
    for (int i = 0; i < SK_COUNT; i++) p->xp[i] = 0;
    p->xp[SK_HITPOINTS] = xp_table[10] * 10;
    p->hp = 10;
    p->x = START_X;
    p->y = START_Y;
    p->plane = START_P;
    p->dir = DIR_DOWN;
    p->style = STYLE_ACCURATE;
    p->autoret = 1;
    p->playtime = 0;
    p->music = 1;
    static const u8 starter[] = {I_BRONZE_AXE, I_TINDERBOX, I_NET, I_BRONZE_PICK, I_BRONZE_SWORD,
                                 I_WOODEN_SHIELD, I_SHRIMP, I_SHRIMP};
    for (unsigned i = 0; i < sizeof(starter); i++) inv_add(starter[i], 1);
    inv_add(I_COINS, 25);
    quest_new_game();
    world_after_load();
}

// ---------------------------------------------------------------------------------------
// Ground items, fires, hitsplats
void drop_item(int x, int y, int p, int id, int qty) {
    if (items[id].flags & IF_STACK) {
        for (int i = 0; i < MAX_GROUND; i++)
            if (ground[i].id == id && ground[i].x == x && ground[i].y == y && ground[i].plane == p) {
                ground[i].qty += qty;
                ground[i].timer = 240;
                return;
            }
    } else if (qty > 1) {
        for (int k = 0; k < qty; k++) drop_item(x, y, p, id, 1);
        return;
    }
    int slot = -1, oldest = 0x7FFF;
    for (int i = 0; i < MAX_GROUND; i++) {
        if (!ground[i].id) {
            slot = i;
            break;
        }
        if (ground[i].timer < oldest) {
            oldest = ground[i].timer;
            slot = i;
        }
    }
    ground[slot].id = id;
    ground[slot].qty = qty;
    ground[slot].x = x;
    ground[slot].y = y;
    ground[slot].plane = p;
    ground[slot].timer = 240;  // two minutes of ticks
}

int item_on_ground_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_GROUND; i++)
        if (ground[i].id) n++;
    return n;
}

int pickup_at(int x, int y, int p) {
    for (int i = MAX_GROUND - 1; i >= 0; i--) {
        if (!ground[i].id || ground[i].x != x || ground[i].y != y || ground[i].plane != p) continue;
        Ground *g = &ground[i];
        if (!(items[g->id].flags & IF_STACK && inv_count(g->id)) && !inv_free()) {
            msg("You don't have enough inventory space.");
            sfx(SFX_ERR);
            return 1;
        }
        inv_add(g->id, g->qty);
        char buf[48], n[12];
        if (g->qty > 1) {
            fmt_num(n, g->qty);
            int k = 0;
            const char *a = "You pick up ";
            while (*a) buf[k++] = *a++;
            for (char *q = n; *q;) buf[k++] = *q++;
            buf[k++] = ' ';
            for (const char *q = items[g->id].name; *q && k < 46;) buf[k++] = *q++;
            buf[k++] = '.';
            buf[k] = 0;
        } else {
            int k = 0;
            const char *a = "You pick up: ";
            while (*a) buf[k++] = *a++;
            for (const char *q = items[g->id].name; *q && k < 46;) buf[k++] = *q++;
            buf[k++] = '.';
            buf[k] = 0;
        }
        msg(buf);
        sfx(g->id == I_COINS ? SFX_COINS : SFX_PICKUP);
        g->id = 0;
        return 1;
    }
    return 0;
}

void splat_spawn(int wx, int wy, int dmg) {
    int s = splat_next;
    splat_next = (splat_next + 1) % MAX_SPLATS;
    splats[s].timer = 40;
    splats[s].wx = wx;
    splats[s].wy = wy;
    gfx_dyn_hitsplat(s, dmg);
}

// ---------------------------------------------------------------------------------------
// Movement
static void start_step(int dir) {
    pl.dir = dir;
    int nx = pl.x + DX[dir], ny = pl.y + DY[dir];
    if (in_map(nx, ny, pl.plane)) {
        int t = world_type(nx, ny, pl.plane);
        if (t == T_STAIRS) {   // walking into a staircase climbs it
            interact();
            return;
        }
    }
    if (!step_free(pl.x, pl.y, pl.plane, dir)) return;
    pl.x = nx;
    pl.y = ny;
    pl.moving = 1;
}

static void npc_step(Npc *n, int dir) {
    n->dir = dir;
    int nx = n->x + DX[dir], ny = n->y + DY[dir];
    if (!step_free(n->x, n->y, n->plane, dir)) return;
    occ_clear(n->x, n->y, n->plane);
    n->x = nx;
    n->y = ny;
    occ_set(nx, ny, n->plane);
    n->moving = 1;
}

static int dir_toward(int fx, int fy, int tx, int ty, int alt) {
    int dx = tx - fx, dy = ty - fy;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int horiz = adx > ady;
    if (alt) horiz = !horiz;
    if (horiz && dx) return dx < 0 ? DIR_LEFT : DIR_RIGHT;
    if (dy) return dy > 0 ? DIR_UP : DIR_DOWN;
    if (dx) return dx < 0 ? DIR_LEFT : DIR_RIGHT;
    return -1;
}

// next to each other with no wall between (melee and talking reach)
int adjacent(int ax, int ay, int bx, int by, int p) {
    int dx = bx - ax, dy = by - ay;
    if (dx == 0 && (dy == 1 || dy == -1)) return !wall_dir(ax, ay, p, dy > 0 ? DIR_UP : DIR_DOWN, 0);
    if (dy == 0 && (dx == 1 || dx == -1)) return !wall_dir(ax, ay, p, dx > 0 ? DIR_RIGHT : DIR_LEFT, 0);
    return 0;
}

static int dist(int ax, int ay, int bx, int by) {
    int dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

// projectiles fly over water, fences and furniture; walls, trees and cliffs stop them
static int blocks_shot(int x, int y, int p) {
    int t = world_type(x, y, p);
    return (t >= T_WALL && t <= T_SWALL) || (t >= T_TREE && t <= T_DEADTREE) || t == T_CLIFF || t == T_HEDGE ||
           t == T_VOID || t == T_LOCKDOOR;
}

// Bresenham walk between the two cells (ends excluded)
int clear_shot(int ax, int ay, int bx, int by, int p) {
    if (world_zone(ax, ay, p) != world_zone(bx, by, p)) return 0;
    int dx = bx - ax, dy = by - ay;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy, x = ax, y = ay;
    int hx = sx > 0 ? DIR_RIGHT : DIR_LEFT, vy = sy > 0 ? DIR_UP : DIR_DOWN;
    for (;;) {
        int e2 = 2 * err, mx = e2 > -dy, my = e2 < dx;
        // walls on the edges crossed; a diagonal step gets through if either corner path is open
        if (mx && my) {
            if ((wall_dir(x, y, p, hx, 1) || wall_dir(x + sx, y, p, vy, 1)) &&
                (wall_dir(x, y, p, vy, 1) || wall_dir(x, y + sy, p, hx, 1)))
                return 0;
        } else if (mx ? wall_dir(x, y, p, hx, 1) : wall_dir(x, y, p, vy, 1)) {
            return 0;
        }
        if (mx) err -= dy, x += sx;
        if (my) err += dx, y += sy;
        if (x == bx && y == by) return 1;
        if (blocks_shot(x, y, p)) return 0;
    }
}

// path one step toward a target, trying the secondary axis if blocked
static void step_toward(int tx, int ty) {
    for (int alt = 0; alt < 2; alt++) {
        int d = dir_toward(pl.x, pl.y, tx, ty, alt);
        if (d < 0) return;
        if (step_free(pl.x, pl.y, pl.plane, d)) {
            start_step(d);
            return;
        }
    }
}

static void npc_step_toward(Npc *n, int tx, int ty) {
    for (int alt = 0; alt < 2; alt++) {
        int d = dir_toward(n->x, n->y, tx, ty, alt);
        if (d < 0) return;
        if (step_free(n->x, n->y, n->plane, d)) {
            npc_step(n, d);
            return;
        }
    }
}

void player_stop(void) {
    if (pl.action == ACT_COMBAT) ui_set_target(-1);
    pl.action = ACT_NONE;
    cast_once = 0;
    pl.lunge = 0;
}

// from the target menu: go and fight or talk to an NPC (game_tick walks there)
void player_walk_to(int npc, int act) {
    if (act == ACT_COMBAT) {
        npc_fight(npc);
        return;
    }
    if (pl.action == ACT_COMBAT) ui_set_target(-1);
    pl.action = act;
    pl.target = npc;
}

// ---------------------------------------------------------------------------------------
static void game_tick(void) {
    // combat timers tick whatever the player is doing
    if (pl.attack_cd) pl.attack_cd--;
    if (pl.home_cd) pl.home_cd--;
    if (foe_timer) foe_timer--;
    // player combat
    if (pl.action == ACT_COMBAT) {
        // melee waits for the target to stop; arrows and spells hit moving targets
        Npc *n = &npcs[pl.target];
        int still = !pl.moving && (!n->moving || attack_range() > 1);
        if (!n->active) {
            player_stop();
        } else if (!foe_free(pl.target)) {
            msg("I'm already under attack.");
            player_stop();
        } else if (!npc_in_reach(pl.target)) {
            if (still) {
                if (npc_dist(n) > 12) player_stop();
                else step_toward(n->x, n->y);
            }
        } else if (still) {
            int d = dir_toward(pl.x, pl.y, n->x, n->y, 0);
            if (d >= 0) pl.dir = d;
            foe_engage(pl.target);
            if (!pl.attack_cd) {
                // a spell cast from the spellbook goes off once, then the player stands still
                int speed = weapon_speed(), manual = cast_once;
                if (player_attack(pl.target)) {
                    pl.attack_cd = speed;
                    if (manual) player_stop();
                } else {
                    player_stop();
                }
            }
        }
    } else if (pl.action == ACT_TALK) {
        Npc *n = &npcs[pl.target];
        if (!n->active || npc_dist(n) > 12) {
            player_stop();
        } else if (!adjacent(pl.x, pl.y, n->x, n->y, pl.plane)) {
            if (!pl.moving) step_toward(n->x, n->y);
        } else if (!pl.moving && !n->moving) {
            pl.dir = dir_toward(pl.x, pl.y, n->x, n->y, 0);
            pl.action = ACT_NONE;
            npc_talk(pl.target);
        }
    } else if (pl.action == ACT_SMELT || pl.action == ACT_SMITH) {
        if (!pl.moving) {
            int d = dir_toward(pl.x, pl.y, pl.tx, pl.ty, 0);
            if (d >= 0) pl.dir = d;
            smith_tick();
        }
    } else if (pl.action != ACT_NONE && !pl.moving) {
        if (pl.action != ACT_LIGHT) {
            int d = dir_toward(pl.x, pl.y, pl.tx, pl.ty, 0);
            if (d >= 0) pl.dir = d;
        }
        if (++pl.action_timer >= 2) {
            pl.action_timer = 0;
            skill_tick();
        }
    }

    // npcs
    int my_cb = combat_level();
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        const NpcDef *d = &npc_defs[n->kind];
        if (!n->active) {
            if (n->respawn && !--n->respawn) {
                if (free_tile(n->hx, n->hy, n->plane)) {
                    n->active = 1;
                    n->x = n->hx;
                    n->y = n->hy;
                    occ_set(n->x, n->y, n->plane);
                    n->px = TILE_PX(n->x);
                    n->py = TILE_PY(n->y);
                    n->hp = d->hp;
                    n->moving = 0;
                    n->hpbar_timer = 0;
                    n->curse = n->bound = 0;
                } else {
                    n->respawn = 1;
                }
            }
            continue;
        }
        if (n->state && n->kind == NPC_SHEEP) n->state--;   // sheared wool grows back
        if (n->bound) n->bound--;                            // Bind / Snare / Entangle wear off
        if (n->moving) continue;
        if (!n->in_combat && (d->flags & NF_AGGRO) && pl.action != ACT_COMBAT && foe_free(i) &&
            my_cb <= d->level * 2 && npc_dist(n) <= 3) {
            n->in_combat = 1;
            n->attack_cd = 2;
        }
        if (n->in_combat) {
            // leashed, or the player is busy with another NPC: give up the fight
            if (dist(n->x, n->y, n->hx, n->hy) > 12 || npc_dist(n) > 9 || !foe_free(i)) {
                n->in_combat = 0;
                foe_release(i);
                continue;
            }
            // casters (dark wizards) fight from up to 7 cells away; everyone else walks up
            int reach = adjacent(n->x, n->y, pl.x, pl.y, pl.plane) ||
                        (d->magic_max && npc_dist(n) <= 7 && clear_shot(n->x, n->y, pl.x, pl.y, pl.plane));
            if (!reach) {
                if (!n->bound && (!pl.moving || pl.action == ACT_COMBAT)) npc_step_toward(n, pl.x, pl.y);
                continue;
            }
            int dd = dir_toward(n->x, n->y, pl.x, pl.y, 0);
            if (dd >= 0) n->dir = dd;
            if (n->attack_cd) n->attack_cd--;
            if (!n->attack_cd && !pl.moving) {
                npc_attack(i);
                n->attack_cd = d->speed ? d->speed : 4;
            }
            continue;
        }
        if (d->wander && !n->bound && rnd_range(10) == 0) {
            int dir = rnd_range(4);
            int nx = n->x + DX[dir], ny = n->y + DY[dir];
            if (dist(nx, ny, n->hx, n->hy) <= d->wander) npc_step(n, dir);
            else npc_step_toward(n, n->hx, n->hy);
        }
    }

    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active && !--fires[i].timer) fires[i].active = 0;
    for (int i = 0; i < MAX_GROUND; i++)
        if (ground[i].id && !--ground[i].timer) ground[i].id = 0;
    for (int i = 0; i < MAX_DEPLETED; i++)
        if (depleted[i].active && !--depleted[i].timer) restore(&depleted[i]);
    // respawning ground items (eggs, buckets, pots, quest items): keep present ones from
    // despawning, bring missing ones back after a short delay
    for (int i = 0; i < ITEM_SPAWN_COUNT; i++) {
        const ItemSpawn *sp = &item_spawns[i];
        int found = 0;
        for (int g = 0; g < MAX_GROUND; g++)
            if (ground[g].id == sp->item && ground[g].x == sp->x && ground[g].y == sp->y && ground[g].plane == sp->plane) {
                ground[g].timer = 240;
                found = 1;
                break;
            }
        if (found) ispawn_timer[i] = 40;
        else if (!--ispawn_timer[i]) drop_item(sp->x, sp->y, sp->plane, sp->item, 1);
    }

    shops_tick();
    world_refresh_npcs();   // cheap; catches stage changes made outside scripts
    if (++regen_ticks >= 60) {
        regen_ticks = 0;
        if (pl.hp < skill_level(SK_HITPOINTS)) pl.hp++;
    }
}

static void move_entity(s16 *px, s16 *py, int tx, int ty, u8 *moving, int speed, u8 *anim) {
    int gx = TILE_PX(tx), gy = TILE_PY(ty);
    for (int s = 0; s < speed; s++) {
        if (*px < gx) (*px)++;
        else if (*px > gx) (*px)--;
        if (*py < gy) (*py)++;
        else if (*py > gy) (*py)--;
    }
    (*anim) += speed;
    if (*px == gx && *py == gy) *moving = 0;
}

void world_update(void) {
    world_debug();
    if (warp.phase) {
        warp_update();
    } else if (pl.moving) {
        move_entity(&pl.px, &pl.py, pl.x, pl.y, &pl.moving, (keys & KEY_B) ? 4 : 2, &pl.anim);
    }
    home_update();   // before input: a key press this frame interrupts the cast
    if (home_cast && !warp.phase) {
        // casting: the player stands still and ignores input
    } else if (!pl.moving && !warp.phase) {
        int dir = -1;
        if (keys & KEY_UP) dir = DIR_UP;
        else if (keys & KEY_DOWN) dir = DIR_DOWN;
        else if (keys & KEY_LEFT) dir = DIR_LEFT;
        else if (keys & KEY_RIGHT) dir = DIR_RIGHT;
        if (dir >= 0) {
            if (pl.action != ACT_NONE) player_stop();
            if (dir != pl.dir && (keys_pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) {
                pl.dir = dir;
                pl.turn_delay = 5;
            } else if (pl.turn_delay) {
                pl.turn_delay--;
            } else {
                start_step(dir);
                if (!pl.moving) pl.dir = dir;
            }
        } else {
            pl.turn_delay = 0;
            pl.anim = 0;
        }
        if (keys_pressed & KEY_A) {
            // A on the target (or with nobody in front) opens its menu; a different NPC in
            // front is dealt with directly
            int fx = pl.x + DX[pl.dir], fy = pl.y + DY[pl.dir];
            int f = in_map(fx, fy, pl.plane) ? npc_at(fx, fy, pl.plane) : -1;
            if (lock_target >= 0 && (f < 0 || f == lock_target)) target_menu_open();
            else interact();
        }
        if ((keys_pressed & KEY_B) && pl.action != ACT_NONE) {
            player_stop();
        }
    }
    if (keys_pressed & KEY_L) target_cycle();
    if ((keys_pressed & KEY_B) && lock_target >= 0) {
        target_set(-1);
        if (pl.action == ACT_COMBAT || pl.action == ACT_TALK) player_stop();
    }
    // the target is forgotten once it is gone or far away
    if (lock_target >= 0) {
        Npc *n = &npcs[lock_target];
        if (!n->active || npc_dist(n) > 15 || world_zone(n->x, n->y, n->plane) != world_zone(pl.x, pl.y, pl.plane))
            target_set(-1);
    }
    if (game_state == GS_TARGET) return;   // the target menu pauses the world
    if (pl.lunge) pl.lunge--;
    if (pl.hpbar_timer) pl.hpbar_timer--;

    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        if (!n->active) continue;
        if (n->moving) move_entity(&n->px, &n->py, n->x, n->y, &n->moving, 1, &n->anim);
        else n->anim = 0;
        if (n->lunge) n->lunge--;
        if (n->hpbar_timer) n->hpbar_timer--;
    }
    for (int i = 0; i < MAX_SPLATS; i++)
        if (splats[i].timer) splats[i].timer--;
    proj_update();

    if (++tick_frames >= TICK_FRAMES) {
        tick_frames = 0;
        game_tick();
    }

    zone_refresh();
    world_camera();
}
