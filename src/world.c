// The overworld: map state, ground items, movement, NPC AI, the game tick and per-frame
// update. Combat is in combat.c, skilling and object interaction in skilling.c, sprites in
// world_draw.c; they share world_internal.h.
#include "world_internal.h"

EWRAM_BSS Npc npcs[MAX_NPCS];
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
const s8 DY[4] = {1, -1, 0, 0};


// ---------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------
// Map state. The map lives entirely in ROM (map_meta/map_over/map_type); runtime changes
// such as stumps and mined-out rocks are a small override table, with a bitmap so lookups
// for unchanged cells cost one bit test.
typedef struct { u32 cell; u16 meta, over; u8 type, mask; } Override;
static Override ovr[MAX_OVR];
int n_ovr;
static EWRAM_BSS u32 ovr_bits[(MAP_W * MAP_H + 31) / 32];

static const Override *ovr_find(int cell) {
    if (!(ovr_bits[cell >> 5] & (1u << (cell & 31)))) return 0;
    for (int i = 0; i < n_ovr; i++)
        if (ovr[i].cell == (u32)cell) return &ovr[i];
    return 0;
}

int ovr_set(int cell, int mask, int meta, int over, int type) {
    Override *o = (Override *)ovr_find(cell);
    if (!o) {
        if (n_ovr >= MAX_OVR) return 0;
        o = &ovr[n_ovr++];
        o->cell = cell;
        o->mask = 0;
        ovr_bits[cell >> 5] |= 1u << (cell & 31);
    }
    o->mask |= mask;
    if (mask & OV_META) o->meta = meta;
    if (mask & OV_OVER) o->over = over;
    if (mask & OV_TYPE) o->type = type;
    return 1;
}

void ovr_clear(int cell, int mask) {
    Override *o = (Override *)ovr_find(cell);
    if (!o) return;
    o->mask &= ~mask;
    if (o->mask) return;
    *o = ovr[--n_ovr];
    ovr_bits[cell >> 5] &= ~(1u << (cell & 31));
}

int world_type(int x, int y) {
    if (!in_map(x, y)) return T_WATER;
    int c = idx(x, y);
    const Override *o = ovr_find(c);
    return (o && (o->mask & OV_TYPE)) ? o->type : map_type[c];
}

int world_meta(int x, int y) {
    int c = idx(x, y);
    const Override *o = ovr_find(c);
    return (o && (o->mask & OV_META)) ? o->meta : map_meta[c];
}

int world_over(int x, int y) {
    int c = idx(x, y);
    const Override *o = ovr_find(c);
    return (o && (o->mask & OV_OVER)) ? o->over : map_over[c];
}

int res_alt_meta(int cell) {
    int lo = 0, hi = RES_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (res_cell[mid] == (u32)cell) return res_alt[mid];
        if (res_cell[mid] < (u32)cell) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}


int fire_at(int x, int y) {
    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active && fires[i].x == x && fires[i].y == y) return i;
    return -1;
}

// NPC occupancy bitmap: free_tile() runs for every NPC step, so it must not scan all NPCs
EWRAM_BSS u32 occ_bits[(MAP_W * MAP_H + 31) / 32];   // (not static: test scripts poke it)
int npc_hi;                       // npcs[] slots in use (spawn table size)

int npc_at(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return -1;
    int c = y * MAP_W + x;
    if (!(occ_bits[c >> 5] & (1u << (c & 31)))) return -1;
    for (int i = 0; i < npc_hi; i++)
        if (npcs[i].active && npcs[i].x == x && npcs[i].y == y) return i;
    return -1;
}

int spot_at(int x, int y) {
    for (int i = 0; i < FISHSPOT_COUNT; i++)
        if (fish_spots[i].x == x && fish_spots[i].y == y) return i;
    return -1;
}

int walkable(int x, int y) {
    if (!in_map(x, y)) return 0;
    return world_type(x, y) <= T_WALKABLE_MAX;   // fires don't block (walk over them as in OSRS)
}

int free_tile(int x, int y) {
    return walkable(x, y) && npc_at(x, y) < 0 && !(pl.x == x && pl.y == y);
}

// ---------------------------------------------------------------------------------------
// Zones and portals. The 1:1 zones (Lumbridge, the castle floors) sit below the overworld in
// the same cell grid; zones[] gives each one's rectangle, palette set and floor. Walking into a
// T_PORTAL cell (the ring around Lumbridge in both maps) warps with a short fade; staircases
// warp from their quest scripts (OP_WARP).
static u8 cur_zone = 0xFF, cur_roof;
static struct { u8 phase, t, dir, walk; s16 x, y; } warp;   // phase 1 fading out, 2 fading in
#define WARP_FRAMES 8

int world_zone(int x, int y) {
    for (int i = 0; i < ZONE_COUNT; i++)
        if (x >= zones[i].x0 && x <= zones[i].x1 && y >= zones[i].y0 && y <= zones[i].y1) return i;
    return 0;
}

static const Portal *portal_at(int x, int y) {
    int c = idx(x, y), lo = 0, hi = PORTAL_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (portals[mid].cell == (u32)c) return &portals[mid];
        if (portals[mid].cell < (u32)c) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

// zone palette and the roof the player is under; call after the player moves
static void zone_refresh(void) {
    int z = world_zone(pl.x, pl.y);
    if (z != cur_zone) {
        cur_zone = z;
        gfx_set_world_palette_set(zones[z].pal_set);
        gfx_mark_world_dirty();
    }
    int r = map_roof[idx(pl.x, pl.y)];
    if (r != cur_roof) {
        cur_roof = r;
        gfx_set_hidden_roof(r);
    }
}

static void warp_start(int x, int y, int dir, int walk) {
    player_stop();
    warp.phase = 1;
    warp.t = 0;
    warp.x = x;
    warp.y = y;
    warp.dir = dir;
    warp.walk = walk;
}

void player_warp(int x, int y, int dir) { warp_start(x, y, dir, 0); }
int world_warping(void) { return warp.phase != 0; }

static void warp_update(void) {
    if (warp.phase == 1) {
        gfx_set_world_fade(16 - (++warp.t) * 16 / WARP_FRAMES);
        if (warp.t < WARP_FRAMES) return;
        player_tp(warp.x, warp.y, warp.dir);
        zone_refresh();
        if (warp.walk && free_tile(pl.x + DX[pl.dir], pl.y + DY[pl.dir])) {
            pl.x += DX[pl.dir];   // arrive mid-stride
            pl.y += DY[pl.dir];
            pl.moving = 1;
        }
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
        if (pl.x >= r->x0 && pl.x <= r->x1 && pl.y >= r->y0 && pl.y <= r->y1) return i;
    }
    return REGION_COUNT - 1;
}

// ---------------------------------------------------------------------------------------
void world_init(void) {
    n_ovr = 0;
    for (int i = 0; i < (MAP_W * MAP_H + 31) / 32; i++) ovr_bits[i] = 0;
    for (int i = 0; i < MAX_NPCS; i++) npcs[i].active = 0;
    for (int i = 0; i < (MAP_W * MAP_H + 31) / 32; i++) occ_bits[i] = 0;
    npc_hi = SPAWN_COUNT < MAX_NPCS ? SPAWN_COUNT : MAX_NPCS;
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        n->active = 1;
        n->kind = spawns[i].kind;
        n->x = n->hx = spawns[i].x;
        n->y = n->hy = spawns[i].y;
        n->px = n->x * 16;
        n->py = n->y * 16;
        n->dir = DIR_DOWN;
        n->hp = npc_defs[n->kind].hp;
        n->moving = 0;
        n->in_combat = 0;
        n->respawn = 0;
        n->attack_cd = 0;
        n->hpbar_timer = 0;
        occ_set(n->x, n->y);
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
        // a wanderer may be standing on the spot: use the nearest free cell instead
        int sx = -1, sy = -1;
        for (int r = 0; r <= 2 && sx < 0; r++)
            for (int dy = -r; dy <= r && sx < 0; dy++)
                for (int dx = -r; dx <= r && sx < 0; dx++)
                    if ((dx == r || dx == -r || dy == r || dy == -r) && free_tile(n->hx + dx, n->hy + dy))
                        sx = n->hx + dx, sy = n->hy + dy;
        if (sx < 0) return;
        n->active = 1;
        n->x = sx;
        n->y = sy;
        n->px = n->x * 16;
        n->py = n->y * 16;
        n->hp = npc_defs[n->kind].hp;
        n->moving = n->in_combat = n->hpbar_timer = n->state = 0;
        n->respawn = 0;
        occ_set(n->x, n->y);
    } else {
        n->active = 0;
        n->in_combat = 0;
        foe_release(i);
        occ_clear(n->x, n->y);
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

// centre on the player, kept inside the player's zone
void world_camera(void) {
    const Zone *z = &zones[world_zone(pl.x, pl.y)];
    int x0 = z->x0 * 16, y0 = z->y0 * 16, x1 = (z->x1 + 1) * 16 - SCREEN_W, y1 = (z->y1 + 1) * 16 - SCREEN_H;
    cam_x = pl.px + 8 - SCREEN_W / 2;
    cam_y = pl.py + 8 - SCREEN_H / 2;
    if (cam_x > x1) cam_x = x1;
    if (cam_y > y1) cam_y = y1;
    if (cam_x < x0) cam_x = x0;
    if (cam_y < y0) cam_y = y0;
}

void player_tp(int x, int y, int dir) {
    player_stop();
    pl.x = x;
    pl.y = y;
    pl.px = x * 16;
    pl.py = y * 16;
    pl.dir = dir;
    pl.moving = 0;
}

int player_can_walk_through(void) {
    return free_tile(pl.x + DX[pl.dir] * 2, pl.y + DY[pl.dir] * 2);
}

int player_walk_through(void) {
    int nx = pl.x + DX[pl.dir] * 2, ny = pl.y + DY[pl.dir] * 2;
    if (!free_tile(nx, ny)) return 0;
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
    // saves from before the 1:1 towns: an old 1:2 town is now only a view from outside
    for (int i = 0; i < LEGACY_COUNT; i++) {
        const Legacy *l = &legacy[i];
        if (pl.x >= l->x0 && pl.x <= l->x1 && pl.y >= l->y0 && pl.y <= l->y1) {
            pl.x = pl.x * 2 + l->dx;
            pl.y = pl.y * 2 + l->dy;
            break;
        }
    }
    // saves from an older map layout may place the player inside a wall: go home
    if (!in_map(pl.x, pl.y) || world_type(pl.x, pl.y) > T_WALKABLE_MAX) {
        pl.x = START_X;
        pl.y = START_Y;
    }
    pl.px = pl.x * 16;
    pl.py = pl.y * 16;
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
    for (int i = 0; i < SK_COUNT; i++) p->xp[i] = 0;
    p->xp[SK_HITPOINTS] = xp_table[10] * 10;
    p->hp = 10;
    p->x = START_X;
    p->y = START_Y;
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
void drop_item(int x, int y, int id, int qty) {
    if (items[id].flags & IF_STACK) {
        for (int i = 0; i < MAX_GROUND; i++)
            if (ground[i].id == id && ground[i].x == x && ground[i].y == y) {
                ground[i].qty += qty;
                ground[i].timer = 240;
                return;
            }
    } else if (qty > 1) {
        for (int k = 0; k < qty; k++) drop_item(x, y, id, 1);
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
    ground[slot].timer = 240;  // two minutes of ticks
}

int item_on_ground_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_GROUND; i++)
        if (ground[i].id) n++;
    return n;
}

int pickup_at(int x, int y) {
    for (int i = MAX_GROUND - 1; i >= 0; i--) {
        if (!ground[i].id || ground[i].x != x || ground[i].y != y) continue;
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
    if (in_map(nx, ny)) {
        int t = world_type(nx, ny);
        const Portal *p = t == T_PORTAL ? portal_at(nx, ny) : 0;
        if (p) {
            warp_start(p->x, p->y, p->dir, 1);
            return;
        }
        if (t == T_STAIRS) {   // walking into a staircase climbs it
            interact();
            return;
        }
    }
    if (!free_tile(nx, ny)) return;
    pl.x = nx;
    pl.y = ny;
    pl.moving = 1;
}

static void npc_step(Npc *n, int dir) {
    n->dir = dir;
    int nx = n->x + DX[dir], ny = n->y + DY[dir];
    if (!free_tile(nx, ny)) return;
    occ_clear(n->x, n->y);
    n->x = nx;
    n->y = ny;
    occ_set(nx, ny);
    n->moving = 1;
}

static int dir_toward(int fx, int fy, int tx, int ty, int alt) {
    int dx = tx - fx, dy = ty - fy;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int horiz = adx > ady;
    if (alt) horiz = !horiz;
    if (horiz && dx) return dx < 0 ? DIR_LEFT : DIR_RIGHT;
    if (dy) return dy < 0 ? DIR_UP : DIR_DOWN;
    if (dx) return dx < 0 ? DIR_LEFT : DIR_RIGHT;
    return -1;
}

int adjacent(int ax, int ay, int bx, int by) {
    int dx = ax - bx, dy = ay - by;
    return (dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1));
}

static int dist(int ax, int ay, int bx, int by) {
    int dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

// path one step toward a target, trying the secondary axis if blocked
static void step_toward(int tx, int ty) {
    for (int alt = 0; alt < 2; alt++) {
        int d = dir_toward(pl.x, pl.y, tx, ty, alt);
        if (d < 0) return;
        if (free_tile(pl.x + DX[d], pl.y + DY[d])) {
            start_step(d);
            return;
        }
    }
}

static void npc_step_toward(Npc *n, int tx, int ty) {
    for (int alt = 0; alt < 2; alt++) {
        int d = dir_toward(n->x, n->y, tx, ty, alt);
        if (d < 0) return;
        if (free_tile(n->x + DX[d], n->y + DY[d])) {
            npc_step(n, d);
            return;
        }
    }
}

void player_stop(void) {
    if (pl.action == ACT_COMBAT) ui_set_target(-1);
    pl.action = ACT_NONE;
    pl.lunge = 0;
}

// ---------------------------------------------------------------------------------------
static void game_tick(void) {
    // combat timers tick whatever the player is doing
    if (pl.attack_cd) pl.attack_cd--;
    if (foe_timer) foe_timer--;
    // player combat
    if (pl.action == ACT_COMBAT) {
        Npc *n = &npcs[pl.target];
        if (!n->active) {
            player_stop();
        } else if (!foe_free(pl.target)) {
            msg("I'm already under attack.");
            player_stop();
        } else if (!adjacent(pl.x, pl.y, n->x, n->y)) {
            if (!pl.moving && !n->moving) {
                if (dist(pl.x, pl.y, n->x, n->y) > 10) player_stop();
                else step_toward(n->x, n->y);
            }
        } else if (!pl.moving && !n->moving) {
            pl.dir = dir_toward(pl.x, pl.y, n->x, n->y, 0);
            foe_engage(pl.target);
            if (!pl.attack_cd) {
                player_attack(pl.target);
                pl.attack_cd = weapon_speed();
            }
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
                if (free_tile(n->hx, n->hy)) {
                    n->active = 1;
                    n->x = n->hx;
                    n->y = n->hy;
                    occ_set(n->x, n->y);
                    n->px = n->x * 16;
                    n->py = n->y * 16;
                    n->hp = d->hp;
                    n->moving = 0;
                    n->hpbar_timer = 0;
                } else {
                    n->respawn = 1;
                }
            }
            continue;
        }
        if (n->state && n->kind == NPC_SHEEP) n->state--;   // sheared wool grows back
        if (n->moving) continue;
        if (!n->in_combat && (d->flags & NF_AGGRO) && pl.action != ACT_COMBAT && foe_free(i) &&
            my_cb <= d->level * 2 && dist(n->x, n->y, pl.x, pl.y) <= 3) {
            n->in_combat = 1;
            n->attack_cd = 2;
        }
        if (n->in_combat) {
            // leashed, or the player is busy with another NPC: give up the fight
            if (dist(n->x, n->y, n->hx, n->hy) > 12 || dist(n->x, n->y, pl.x, pl.y) > 9 || !foe_free(i)) {
                n->in_combat = 0;
                foe_release(i);
                continue;
            }
            if (!adjacent(n->x, n->y, pl.x, pl.y)) {
                if (!pl.moving || pl.action == ACT_COMBAT) npc_step_toward(n, pl.x, pl.y);
                continue;
            }
            int dd = dir_toward(n->x, n->y, pl.x, pl.y, 0);
            if (dd >= 0) n->dir = dd;
            if (n->attack_cd) n->attack_cd--;
            if (!n->attack_cd && !pl.moving) {
                npc_attack(i);
                n->attack_cd = 4;
            }
            continue;
        }
        if (d->wander && rnd_range(10) == 0) {
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
            if (ground[g].id == sp->item && ground[g].x == sp->x && ground[g].y == sp->y) {
                ground[g].timer = 240;
                found = 1;
                break;
            }
        if (found) ispawn_timer[i] = 40;
        else if (!--ispawn_timer[i]) drop_item(sp->x, sp->y, sp->item, 1);
    }

    shops_tick();
    world_refresh_npcs();   // cheap; catches stage changes made outside scripts
    if (++regen_ticks >= 60) {
        regen_ticks = 0;
        if (pl.hp < skill_level(SK_HITPOINTS)) pl.hp++;
    }
}

static void move_entity(s16 *px, s16 *py, int tx, int ty, u8 *moving, int speed, u8 *anim) {
    int gx = tx * 16, gy = ty * 16;
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
    if (warp.phase) {
        warp_update();
    } else if (pl.moving) {
        move_entity(&pl.px, &pl.py, pl.x, pl.y, &pl.moving, (keys & KEY_B) ? 4 : 2, &pl.anim);
    }
    if (!pl.moving && !warp.phase) {
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
        if (keys_pressed & KEY_A) interact();
        if ((keys_pressed & KEY_B) && pl.action != ACT_NONE) {
            player_stop();
        }
    }
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

    if (++tick_frames >= TICK_FRAMES) {
        tick_frames = 0;
        game_tick();
    }

    zone_refresh();
    world_camera();
}
