// The overworld: map state, player movement & interaction, NPC AI, combat and skilling.
#include "game.h"

EWRAM_BSS Npc npcs[MAX_NPCS];
Player pl;
s16 cam_x, cam_y;

#define MAX_FIRES 8
#define MAX_GROUND 40
#define MAX_DEPLETED 32
#define MAX_SPLATS 4

typedef struct { u8 active; s16 x, y; u16 timer; } Fire;
typedef struct { u16 id, qty; s16 x, y; u16 timer; } Ground;
typedef struct { u8 active, type; s16 x, y; u16 timer; } Depleted;
typedef struct { u8 timer; s16 wx, wy; } Splat;

static Fire fires[MAX_FIRES];
static Ground ground[MAX_GROUND];
static Depleted depleted[MAX_DEPLETED];
static Splat splats[MAX_SPLATS];
static u8 splat_next;
static int tick_frames;
static int regen_ticks;
static u8 ispawn_timer[ITEM_SPAWN_COUNT + 1];   // ticks until a respawning ground item reappears
static u8 kind_vis[NPC_KIND_COUNT];             // quest NPC kinds currently shown

static const s8 DX[4] = {0, 0, -1, 1};
static const s8 DY[4] = {1, -1, 0, 0};

// ---------------------------------------------------------------------------------------
// Resource tables
typedef struct { u8 type, level; u16 xp; u8 low, high; u8 item, deplete, respawn; } Resource;
static const Resource trees[] = {
    {T_TREE, 1, 250, 64, 200, I_LOGS, 255, 20},
    {T_OAK, 15, 375, 32, 100, I_OAK_LOGS, 40, 30},
    {T_WILLOW, 30, 675, 20, 60, I_WILLOW_LOGS, 32, 30},
    {T_YEW, 60, 1750, 6, 28, I_YEW_LOGS, 32, 60},
    {T_DEADTREE, 1, 250, 64, 200, I_LOGS, 255, 20},
};
#define NTREES (int)(sizeof(trees) / sizeof(trees[0]))
static const Resource rocks[] = {
    {T_COPPER, 1, 175, 100, 250, I_COPPER_ORE, 255, 8},
    {T_TIN, 1, 175, 100, 250, I_TIN_ORE, 255, 8},
    {T_IRON, 15, 350, 96, 255, I_IRON_ORE, 255, 16},
    {T_CLAY, 1, 50, 128, 255, I_CLAY, 255, 8},
};
#define NROCKS (int)(sizeof(rocks) / sizeof(rocks[0]))
typedef struct { u8 raw, cooked, burnt, level; u16 xp; u8 low; u16 high; } Recipe;
static const Recipe recipes[] = {
    {I_RAW_SHRIMP, I_SHRIMP, I_BURNT_SHRIMP, 1, 300, 128, 512},
    {I_RAW_ANCHOVY, I_ANCHOVY, I_BURNT_FISH, 1, 300, 128, 512},
    {I_RAW_BEEF, I_COOKED_MEAT, I_BURNT_MEAT, 1, 300, 128, 512},
    {I_RAW_CHICKEN, I_COOKED_CHICKEN, I_BURNT_MEAT, 1, 300, 128, 512},
    {I_RAW_TROUT, I_TROUT, I_BURNT_FISH, 15, 700, 88, 448},
    {I_RAW_SALMON, I_SALMON, I_BURNT_FISH, 25, 900, 68, 388},
};
#define NRECIPES (int)(sizeof(recipes) / sizeof(recipes[0]))

static int chance256(int low, int high, int lvl) {
    int p = low + (high - low) * (lvl - 1) / 98;
    return p > 255 ? 255 : p;
}
static int roll(int p256) { return rnd_range(256) < p256; }

// ---------------------------------------------------------------------------------------
static inline int idx(int x, int y) { return y * MAP_W + x; }
static int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }

// ---------------------------------------------------------------------------------------
// Map state. The map lives entirely in ROM (map_meta/map_over/map_type); runtime changes
// such as stumps and mined-out rocks are a small override table, with a bitmap so lookups
// for unchanged cells cost one bit test.
#define MAX_OVR 72
enum { OV_META = 1, OV_OVER = 2, OV_TYPE = 4 };
typedef struct { u16 cell, meta, over; u8 type, mask; } Override;
static Override ovr[MAX_OVR];
static int n_ovr;
static EWRAM_BSS u32 ovr_bits[(MAP_W * MAP_H + 31) / 32];

static const Override *ovr_find(int cell) {
    if (!(ovr_bits[cell >> 5] & (1u << (cell & 31)))) return 0;
    for (int i = 0; i < n_ovr; i++)
        if (ovr[i].cell == cell) return &ovr[i];
    return 0;
}

static int ovr_set(int cell, int mask, int meta, int over, int type) {
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

static void ovr_clear(int cell, int mask) {
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

static int res_alt_meta(int cell) {
    int lo = 0, hi = RES_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (res_cell[mid] == cell) return res_alt[mid];
        if (res_cell[mid] < cell) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void foe_release(int i);

static int fire_at(int x, int y) {
    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active && fires[i].x == x && fires[i].y == y) return i;
    return -1;
}

// NPC occupancy bitmap: free_tile() runs for every NPC step, so it must not scan all NPCs
EWRAM_BSS u32 occ_bits[(MAP_W * MAP_H + 31) / 32];   // (not static: test scripts poke it)
static int npc_hi;                       // npcs[] slots in use (spawn table size)
static inline void occ_set(int x, int y) { int c = y * MAP_W + x; occ_bits[c >> 5] |= 1u << (c & 31); }
static inline void occ_clear(int x, int y) { int c = y * MAP_W + x; occ_bits[c >> 5] &= ~(1u << (c & 31)); }

static int npc_at(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return -1;
    int c = y * MAP_W + x;
    if (!(occ_bits[c >> 5] & (1u << (c & 31)))) return -1;
    for (int i = 0; i < npc_hi; i++)
        if (npcs[i].active && npcs[i].x == x && npcs[i].y == y) return i;
    return -1;
}

static int spot_at(int x, int y) {
    for (int i = 0; i < FISHSPOT_COUNT; i++)
        if (fish_spots[i].x == x && fish_spots[i].y == y) return i;
    return -1;
}

static int walkable(int x, int y) {
    if (!in_map(x, y)) return 0;
    return world_type(x, y) <= T_WALKABLE_MAX;   // fires don't block (walk over them as in OSRS)
}

static int free_tile(int x, int y) {
    return walkable(x, y) && npc_at(x, y) < 0 && !(pl.x == x && pl.y == y);
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
    world_refresh_npcs();
    gfx_mark_world_dirty();
}

// ---------------------------------------------------------------------------------------
// Quest support: NPCs that exist only during part of a quest, scripted movement
void npc_set_active(int i, int on) {
    Npc *n = &npcs[i];
    if (on == n->active) return;
    if (on) {
        if (!free_tile(n->hx, n->hy)) return;
        n->active = 1;
        n->x = n->hx;
        n->y = n->hy;
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
    // saves from an older map layout may place the player inside a wall: go home
    if (!in_map(pl.x, pl.y) || world_type(pl.x, pl.y) > T_WALKABLE_MAX) {
        pl.x = START_X;
        pl.y = START_Y;
    }
    pl.px = pl.x * 16;
    pl.py = pl.y * 16;
    pl.moving = 0;
    pl.action = ACT_NONE;
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

static int pickup_at(int x, int y) {
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

static void splat_spawn(int wx, int wy, int dmg) {
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

static int adjacent(int ax, int ay, int bx, int by) {
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
// Combat
static int style_bonus(int sk) {
    if (sk == SK_ATTACK) return pl.style == STYLE_ACCURATE ? 3 : 0;
    if (sk == SK_STRENGTH) return pl.style == STYLE_AGGRESSIVE ? 3 : 0;
    if (sk == SK_DEFENCE) return pl.style == STYLE_DEFENSIVE ? 3 : 0;
    return 0;
}

static int hit_chance256(int atk_roll, int def_roll) {
    if (atk_roll > def_roll) return 256 - (def_roll + 2) * 128 / (atk_roll + 1);
    return atk_roll * 128 / (def_roll + 1);
}

static void npc_die(int i);

// Single-way combat: one NPC at a time may fight the player. The lock is held by the
// player's chosen target while the player pursues it, and by whoever last traded a hit
// for FOE_TICKS afterwards (so running away eventually frees the player).
#define FOE_TICKS 10
static s16 foe = -1;
static u8 foe_timer;

static int foe_free(int i) {
    return foe < 0 || foe == i || !foe_timer || !npcs[foe].active;
}
static void foe_engage(int i) {
    foe = i;
    foe_timer = FOE_TICKS;
}
static void foe_release(int i) {
    if (foe == i) foe = -1;
}

// ticks between swings for the wielded weapon (OSRS speeds)
static int weapon_speed(void) {
    switch (pl.equip[EQ_WEAPON]) {
    case I_BRONZE_AXE: case I_IRON_AXE: case I_STEEL_AXE: case I_MITHRIL_AXE: case I_ADAMANT_AXE:
    case I_RUNE_AXE: case I_BRONZE_PICK: case I_IRON_PICK: case I_STEEL_PICK: case I_MITHRIL_PICK:
    case I_ADAMANT_PICK: case I_RUNE_PICK: case I_IRON_MACE: case I_STEEL_LONGSWORD: case I_RUNE_LONGSWORD:
        return 5;
    default:
        return 4;   // unarmed, daggers, swords, scimitars
    }
}

static void player_attack(int i) {
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    int eff_a = skill_level(SK_ATTACK) + style_bonus(SK_ATTACK) + 8;
    int eff_s = skill_level(SK_STRENGTH) + style_bonus(SK_STRENGTH) + 8;
    int a = eff_a * (equip_bonus(0) + 64);
    int def = (d->def + 9) * (d->defb + 64);
    int maxhit = (eff_s * (equip_bonus(1) + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = roll(hit_chance256(a, def)) ? rnd_range(maxhit + 1) : 0;
    if (n->kind == NPC_DELRITH && pl.equip[EQ_WEAPON] != I_SILVERLIGHT) {
        if (dmg) msg_col("Only Silverlight can harm the demon!", TC_ORANGE);
        dmg = 0;
    }
    if (dmg > n->hp) dmg = n->hp;
    foe_engage(i);
    pl.lunge = 8;
    n->hpbar_timer = 200;
    n->in_combat = 1;
    if (!n->attack_cd) n->attack_cd = 2;
    splat_spawn(n->px, n->py, dmg);
    sfx(dmg ? SFX_HIT : SFX_MISS);
    if (dmg) {
        int sk = pl.style == STYLE_ACCURATE ? SK_ATTACK : pl.style == STYLE_AGGRESSIVE ? SK_STRENGTH : SK_DEFENCE;
        add_xp(sk, dmg * 40);
        add_xp(SK_HITPOINTS, dmg * 40 / 3);
        n->hp -= dmg;
        if (!n->hp) npc_die(i);
    }
}

static void npc_attack(int i) {
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    int a = (d->att + 9) * (d->atkb + 64);
    int eff_d = skill_level(SK_DEFENCE) + style_bonus(SK_DEFENCE) + 8;
    int def = eff_d * (equip_bonus(2) + 64);
    int maxhit = ((d->str + 9) * (d->strb + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = roll(hit_chance256(a, def)) ? rnd_range(maxhit + 1) : 0;
    if (dmg > pl.hp) dmg = pl.hp;
    foe_engage(i);
    n->lunge = 8;
    pl.hpbar_timer = 200;
    splat_spawn(pl.px, pl.py, dmg);
    sfx(dmg ? SFX_HIT : SFX_MISS);
    pl.hp -= dmg;
    if (pl.action != ACT_COMBAT && pl.autoret && !pl.moving) {
        pl.action = ACT_COMBAT;
        pl.target = i;
        if (!pl.attack_cd) pl.attack_cd = 1;
        ui_set_target(i);
    }
    if (!pl.hp) {
        msg_col("Oh dear, you are dead!", TC_RED);
        sfx(SFX_DEATH);
        player_respawn();
    }
}

void player_respawn(void) {
    for (int i = 0; i < npc_hi; i++) npcs[i].in_combat = 0;
    foe = -1;
    player_stop();
    pl.x = START_X;
    pl.y = START_Y;
    pl.px = pl.x * 16;
    pl.py = pl.y * 16;
    pl.moving = 0;
    pl.dir = DIR_DOWN;
    pl.hp = skill_level(SK_HITPOINTS);
    pl.hpbar_timer = 0;
}

static void npc_drops(Npc *n) {
    int x = n->x, y = n->y;
    switch (n->kind) {
    case NPC_COW:
        drop_item(x, y, I_BONES, 1);
        drop_item(x, y, I_RAW_BEEF, 1);
        drop_item(x, y, I_COWHIDE, 1);
        break;
    case NPC_CHICKEN:
        drop_item(x, y, I_BONES, 1);
        drop_item(x, y, I_RAW_CHICKEN, 1);
        drop_item(x, y, I_FEATHER, 5 + rnd_range(11));
        break;
    case NPC_GOBLIN: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 55) drop_item(x, y, I_COINS, 1 + rnd_range(15));
        else if (r < 63) drop_item(x, y, I_BRONZE_HELM, 1);
        else if (r < 70) drop_item(x, y, I_BRONZE_SHIELD, 1);
        else if (r < 75) drop_item(x, y, I_BRONZE_LEGS, 1);
        else if (r < 78) drop_item(x, y, I_LEATHER_BODY, 1);
        else if (r < 81) drop_item(x, y, I_BRONZE_DAGGER, 1);
        else if (r < 84) drop_item(x, y, I_BRONZE_FULL_HELM, 1);
        else if (r < 86) drop_item(x, y, I_BRONZE_CHAINBODY, 1);
        else if (r < 88) drop_item(x, y, I_BRONZE_SKIRT, 1);
        else if (r < 98) drop_item(x, y, I_GOBLIN_MAIL, 1);
        break;
    }
    case NPC_IMP: {
        static const u8 beads[4] = {I_RED_BEAD, I_YELLOW_BEAD, I_BLACK_BEAD, I_WHITE_BEAD};
        if (rnd_range(100) < 60) drop_item(x, y, beads[rnd_range(4)], 1);
        break;
    }
    case NPC_RAT:
        drop_item(x, y, I_BONES, 1);
        // the tail only drops while Hetty needs one (as in OSRS)
        if (quest_stage(Q_WITCH) == 1 && !has_item(I_RAT_TAIL)) drop_item(x, y, I_RAT_TAIL, 1);
        break;
    case NPC_SKELETON:
        drop_item(x, y, I_BONES, 1);
        break;
    case NPC_MAN:
    case NPC_WOMAN:
        drop_item(x, y, I_BONES, 1);
        if (rnd_range(100) < 70) drop_item(x, y, I_COINS, 3 + rnd_range(20));
        break;
    case NPC_WARRIOR:
    case NPC_DARK_WIZARD:
    case NPC_BARBARIAN: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 50) drop_item(x, y, I_COINS, 5 + rnd_range(25));
        else if (r < 58) drop_item(x, y, I_BRONZE_SWORD, 1);
        else if (r < 64) drop_item(x, y, I_IRON_HELM, 1);
        else if (r < 68) drop_item(x, y, I_IRON_SWORD, 1);
        break;
    }
    case NPC_GUARD: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 60) drop_item(x, y, I_COINS, 10 + rnd_range(50));
        else if (r < 70) drop_item(x, y, I_IRON_HELM, 1);
        else if (r < 78) drop_item(x, y, I_IRON_SWORD, 1);
        else if (r < 84) drop_item(x, y, I_IRON_LEGS, 1);
        else if (r < 88) drop_item(x, y, I_STEEL_SWORD, 1);
        else if (r < 91) drop_item(x, y, I_IRON_BODY, 1);
        else if (r < 93) drop_item(x, y, I_IRON_SCIMITAR, 1);
        else if (r < 95) drop_item(x, y, I_IRON_KITE, 1);
        else if (r < 96) drop_item(x, y, I_IRON_FULL_HELM, 1);
        else if (r < 97) drop_item(x, y, I_IRON_CHAINBODY, 1);
        else if (r < 98) drop_item(x, y, I_STEEL_LONGSWORD, 1);
        else if (r < 99) {
            // rare table: a taste of the higher tiers
            static const u8 rare[] = {I_MITHRIL_SCIMITAR, I_MITHRIL_FULL_HELM, I_MITHRIL_KITE,
                                      I_STEEL_BODY, I_STEEL_LEGS, I_ADAMANT_SCIMITAR, I_MITHRIL_BODY,
                                      I_RUNE_SCIMITAR};
            drop_item(x, y, rare[rnd_range(sizeof(rare))], 1);
        }
        break;
    }
    }
}

static void npc_die(int i) {
    Npc *n = &npcs[i];
    if (quest_npc_dying(i)) return;
    npc_drops(n);
    n->active = 0;
    occ_clear(n->x, n->y);
    n->in_combat = 0;
    foe_release(i);
    n->respawn = npc_defs[n->kind].respawn;
    if (pl.target == i && pl.action == ACT_COMBAT) player_stop();
}

// ---------------------------------------------------------------------------------------
// Skilling
// tools, best first: {item, power, level needed}
typedef struct { u8 id, power, level; } Tool;
static const Tool axes[] = {
    {I_RUNE_AXE, 5, 41}, {I_ADAMANT_AXE, 4, 31}, {I_MITHRIL_AXE, 3, 21},
    {I_STEEL_AXE, 2, 6}, {I_IRON_AXE, 1, 1}, {I_BRONZE_AXE, 0, 1},
};
static const Tool picks[] = {
    {I_RUNE_PICK, 5, 41}, {I_ADAMANT_PICK, 4, 31}, {I_MITHRIL_PICK, 3, 21},
    {I_STEEL_PICK, 2, 6}, {I_IRON_PICK, 1, 1}, {I_BRONZE_PICK, 0, 1},
};
// best usable tool; *owned is set if the player has any tool of this kind at all
static const Tool *best_tool(const Tool *t, int n, int sk, int *owned) {
    int lvl = skill_level(sk);
    *owned = 0;
    for (int i = 0; i < n; i++)
        if (has_item(t[i].id)) {
            *owned = 1;
            if (lvl >= t[i].level) return &t[i];
        }
    return 0;
}
static int axe_power(void) {
    int own;
    const Tool *t = best_tool(axes, 6, SK_WOODCUTTING, &own);
    return t ? t->power : -1;
}
static int pick_power(void) {
    int own;
    const Tool *t = best_tool(picks, 6, SK_MINING, &own);
    return t ? t->power : -1;
}

static const Resource *find_res(const Resource *tab, int n, int type) {
    for (int i = 0; i < n; i++)
        if (tab[i].type == type) return &tab[i];
    return 0;
}

static void deplete(int x, int y, int type, int respawn_ticks) {
    int slot = -1;
    for (int i = 0; i < MAX_DEPLETED; i++)
        if (!depleted[i].active) {
            slot = i;
            break;
        }
    int k = idx(x, y), alt = res_alt_meta(k);
    if (slot < 0 || alt < 0 || n_ovr + 2 > MAX_OVR) return;   // table full: resource stays
    Depleted *d = &depleted[slot];
    d->active = 1;
    d->type = type;
    d->x = x;
    d->y = y;
    d->timer = respawn_ticks;
    int is_tree = find_res(trees, NTREES, type) != 0;
    ovr_set(k, OV_META | OV_TYPE, alt, 0, is_tree ? T_STUMP : T_ROCK_EMPTY);
    gfx_mark_cell_dirty(x, y);
    if (is_tree && y > 0) {
        ovr_set(k - MAP_W, OV_OVER, 0, 0, 0);   // remove the canopy drawn on the cell above
        gfx_mark_cell_dirty(x, y - 1);
    }
}

static void restore(Depleted *d) {
    int k = idx(d->x, d->y);
    ovr_clear(k, OV_META | OV_TYPE);
    gfx_mark_cell_dirty(d->x, d->y);
    if (d->y > 0) {
        ovr_clear(k - MAP_W, OV_OVER);
        gfx_mark_cell_dirty(d->x, d->y - 1);
    }
    d->active = 0;
}

static const char *const log_msgs[] = {"You get some logs.", "You get some oak logs.", "You get some willow logs.",
                                       "You get some yew logs.", "You get some logs."};

static void begin_action(int act, int tx, int ty) {
    pl.action = act;
    pl.tx = tx;
    pl.ty = ty;
    pl.action_timer = 0;
}

static void skill_tick(void) {
    int lvl;
    switch (pl.action) {
    case ACT_CHOP: {
        const Resource *r = find_res(trees, NTREES, world_type(pl.tx, pl.ty));
        if (!r) {
            player_stop();
            return;
        }
        sfx(SFX_CHOP);
        pl.lunge = 6;
        if (!inv_free()) {
            msg("Your inventory is too full to hold any more logs.");
            player_stop();
            return;
        }
        lvl = skill_level(SK_WOODCUTTING);
        int p = chance256(r->low, r->high, lvl) * (4 + axe_power()) / 4;
        if (roll(p)) {
            inv_add(r->item, 1);
            msg(log_msgs[r - trees]);
            add_xp(SK_WOODCUTTING, r->xp);
            if (rnd_range(256) < r->deplete) {
                deplete(pl.tx, pl.ty, r->type, r->respawn);
                player_stop();
            }
        }
        break;
    }
    case ACT_MINE: {
        const Resource *r = find_res(rocks, NROCKS, world_type(pl.tx, pl.ty));
        if (!r) {
            player_stop();
            return;
        }
        sfx(SFX_MINE);
        pl.lunge = 6;
        if (!inv_free()) {
            msg("Your inventory is too full to hold any more ore.");
            player_stop();
            return;
        }
        lvl = skill_level(SK_MINING);
        int p = chance256(r->low, r->high, lvl) * (4 + pick_power()) / 4;
        if (roll(p)) {
            inv_add(r->item, 1);
            msg(r->item == I_COPPER_ORE ? "You manage to mine some copper." : r->item == I_TIN_ORE ? "You manage to mine some tin." :
                r->item == I_CLAY ? "You manage to mine some clay." : "You manage to mine some iron.");
            add_xp(SK_MINING, r->xp);
            deplete(pl.tx, pl.ty, r->type, r->respawn);
            player_stop();
        }
        break;
    }
    case ACT_FISH: {
        const FishSpotDef *s = &fish_spots[pl.target];
        sfx(SFX_SPLASH);
        if (!inv_free()) {
            msg("You can't carry any more fish.");
            player_stop();
            return;
        }
        lvl = skill_level(SK_FISHING);
        if (s->kind == 0) {
            if (lvl >= 15 && rnd_range(2)) {
                if (roll(chance256(24, 128, lvl))) {
                    inv_add(I_RAW_ANCHOVY, 1);
                    msg("You catch some anchovies.");
                    add_xp(SK_FISHING, 400);
                }
            } else if (roll(chance256(48, 256, lvl))) {
                inv_add(I_RAW_SHRIMP, 1);
                msg("You catch some shrimps.");
                add_xp(SK_FISHING, 100);
            }
        } else {
            if (!inv_count(I_FEATHER)) {
                msg("You don't have any feathers left to fish with.");
                player_stop();
                return;
            }
            if (lvl >= 30 && rnd_range(2)) {
                if (roll(chance256(16, 96, lvl))) {
                    inv_remove(I_FEATHER, 1);
                    inv_add(I_RAW_SALMON, 1);
                    msg("You catch a salmon.");
                    add_xp(SK_FISHING, 700);
                }
            } else if (roll(chance256(32, 192, lvl))) {
                inv_remove(I_FEATHER, 1);
                inv_add(I_RAW_TROUT, 1);
                msg("You catch a trout.");
                add_xp(SK_FISHING, 500);
            }
        }
        break;
    }
    case ACT_LIGHT: {
        int slot = pl.target;
        int id = pl.inv[slot].id;
        if (!(items[id].flags & IF_LIGHT)) {
            player_stop();
            return;
        }
        lvl = skill_level(SK_FIREMAKING);
        sfx(SFX_FIRE);
        if (roll(chance256(65, 513, lvl))) {
            int f = -1;
            for (int i = 0; i < MAX_FIRES; i++)
                if (!fires[i].active) {
                    f = i;
                    break;
                }
            if (f < 0) f = 0;
            pl.inv[slot].id = 0;
            pl.inv[slot].qty = 0;
            fires[f].active = 1;
            fires[f].x = pl.x;
            fires[f].y = pl.y;
            fires[f].timer = 60 + rnd_range(60);
            add_xp(SK_FIREMAKING, id == I_LOGS ? 400 : id == I_OAK_LOGS ? 600 : id == I_YEW_LOGS ? 2025 : 900);
            msg("The fire catches and the logs begin to burn.");
            player_stop();
            static const u8 order[4] = {DIR_LEFT, DIR_RIGHT, DIR_DOWN, DIR_UP};
            for (int k = 0; k < 4; k++) {
                int d = order[k];
                if (free_tile(pl.x + DX[d], pl.y + DY[d])) {
                    pl.x += DX[d];
                    pl.y += DY[d];
                    pl.moving = 1;
                    break;
                }
            }
            if (pl.moving) {
                // keep facing the fire while stepping away
                pl.dir = pl.x < fires[f].x ? DIR_RIGHT : pl.x > fires[f].x ? DIR_LEFT : pl.y < fires[f].y ? DIR_DOWN : DIR_UP;
            }
        }
        break;
    }
    case ACT_COOK: {
        int t = world_type(pl.tx, pl.ty);
        int on_range = t == T_RANGE;
        if (!on_range && t != T_FIREPIT && fire_at(pl.tx, pl.ty) < 0) {
            msg("The fire has gone out.");
            player_stop();
            return;
        }
        const Recipe *r = 0;
        for (int i = 0; i < NRECIPES; i++)
            if (recipes[i].raw == pl.cook_item) r = &recipes[i];
        int slot = -1;
        for (int i = 0; i < 28; i++)
            if (pl.inv[i].id == pl.cook_item) {
                slot = i;
                break;
            }
        if (!r || slot < 0) {
            player_stop();
            return;
        }
        lvl = skill_level(SK_COOKING);
        pl.lunge = 6;
        int high = r->high + (on_range ? 24 : 0);
        int p = r->low + (high - r->low) * (lvl - 1) / 98;
        if (rnd_range(256) < p) {
            pl.inv[slot].id = r->cooked;
            add_xp(SK_COOKING, r->xp);
            msg(r->raw == I_RAW_SHRIMP ? "You successfully cook some shrimps." : "You successfully cook the food.");
            sfx(SFX_FIRE);
        } else {
            pl.inv[slot].id = r->burnt;
            msg("You accidentally burn the food.");
            sfx(SFX_ERR);
        }
        if (!inv_count(pl.cook_item)) player_stop();
        break;
    }
    }
}

static int first_raw(void) {
    for (int i = 0; i < 28; i++)
        for (int k = 0; k < NRECIPES; k++)
            if (pl.inv[i].id == recipes[k].raw) return recipes[k].raw;
    return 0;
}

void player_start_firemaking(int slot) {
    if (!inv_count(I_TINDERBOX)) {
        msg("You need a tinderbox to light a fire.");
        sfx(SFX_ERR);
        return;
    }
    int id = pl.inv[slot].id;
    int req = id == I_LOGS ? 1 : id == I_OAK_LOGS ? 15 : id == I_YEW_LOGS ? 60 : 30;
    if (skill_level(SK_FIREMAKING) < req) {
        msg("You need a higher Firemaking level to burn these logs.");
        sfx(SFX_ERR);
        return;
    }
    int t = world_type(pl.x, pl.y);
    if (fire_at(pl.x, pl.y) >= 0 || t == T_FLOOR || t == T_WOODFLOOR || t == T_DOOR || t == T_BRIDGE ||
        t == T_CARPET || t == T_DITCH) {
        msg("You can't light a fire here.");
        sfx(SFX_ERR);
        return;
    }
    msg("You attempt to light the logs.");
    begin_action(ACT_LIGHT, pl.x, pl.y);
    pl.target = slot;
}

static void interact(void) {
    int fx = pl.x + DX[pl.dir], fy = pl.y + DY[pl.dir];
    int n = in_map(fx, fy) ? npc_at(fx, fy) : -1;
    if (n >= 0) {
        const NpcDef *d = &npc_defs[npcs[n].kind];
        if (!(d->flags & NF_ATTACK) && quest_talk_entry[npcs[n].kind] != 0xFFFF) {
            Npc *np = &npcs[n];
            if (d->sprite != SPR_KIND_SHEEP && d->sprite != SPR_KIND_COW)
                np->dir = pl.dir == DIR_UP ? DIR_DOWN : pl.dir == DIR_DOWN ? DIR_UP : pl.dir == DIR_LEFT ? DIR_RIGHT : DIR_LEFT;
            player_stop();
            quest_talk(n);
            return;
        }
        if (d->flags & NF_ATTACK) {
            if (pl.action == ACT_COMBAT && pl.target == n) return;   // already fighting it
            if (!foe_free(n)) {
                msg("I'm already under attack.");
                return;
            }
            // the swing timer keeps running: re-pressing A never makes an attack come sooner
            pl.action = ACT_COMBAT;
            pl.target = n;
            foe_engage(n);
            ui_set_target(n);
        } else if (d->flags & NF_TALK) {
            Npc *np = &npcs[n];
            np->dir = pl.dir == DIR_UP ? DIR_DOWN : pl.dir == DIR_DOWN ? DIR_UP : pl.dir == DIR_LEFT ? DIR_RIGHT : DIR_LEFT;
            dialog_open(np->kind, n);
        }
        return;
    }
    int s = spot_at(fx, fy);
    if (s >= 0) {
        if (fish_spots[s].kind == 0) {
            if (!inv_count(I_NET)) {
                msg("You need a small fishing net to fish here.");
                sfx(SFX_ERR);
                return;
            }
            msg("You cast out your net...");
        } else {
            if (!inv_count(I_ROD)) {
                msg("You need a fly fishing rod to fish here.");
                sfx(SFX_ERR);
                return;
            }
            if (skill_level(SK_FISHING) < 20) {
                msg("You need a Fishing level of 20 to fly fish.");
                sfx(SFX_ERR);
                return;
            }
            if (!inv_count(I_FEATHER)) {
                msg("You need feathers to fly fish.");
                sfx(SFX_ERR);
                return;
            }
            msg("You attempt to catch a fish.");
        }
        begin_action(ACT_FISH, fx, fy);
        pl.target = s;
        return;
    }
    if (!in_map(fx, fy)) return;
    int t = world_type(fx, fy);
    if (fire_at(fx, fy) < 0 && quest_object(t, fx, fy)) return;
    if (fire_at(fx, fy) >= 0 || t == T_RANGE || t == T_FIREPIT) {
        int raw = first_raw();
        if (!raw && inv_count(I_COOKED_MEAT)) {
            // as in OSRS, cooked meat can be burnt on purpose (Hetty wants burnt meat)
            inv_remove(I_COOKED_MEAT, 1);
            inv_add(I_BURNT_MEAT, 1);
            msg("You deliberately burn the meat.");
            sfx(SFX_FIRE);
            return;
        }
        if (!raw) {
            msg("You have nothing to cook.");
            return;
        }
        for (int i = 0; i < NRECIPES; i++)
            if (recipes[i].raw == raw && skill_level(SK_COOKING) < recipes[i].level) {
                msg("You need a higher Cooking level to cook that.");
                sfx(SFX_ERR);
                return;
            }
        pl.cook_item = raw;
        begin_action(ACT_COOK, fx, fy);
        return;
    }
    const Resource *r;
    if ((r = find_res(trees, NTREES, t))) {
        if (axe_power() < 0) {
            int own;
            best_tool(axes, 6, SK_WOODCUTTING, &own);
            msg(own ? "You need a higher Woodcutting level to use your axe." : "You need an axe to chop down this tree.");
            sfx(SFX_ERR);
            return;
        }
        if (skill_level(SK_WOODCUTTING) < r->level) {
            msg("You need a higher Woodcutting level to chop this tree.");
            sfx(SFX_ERR);
            return;
        }
        msg("You swing your axe at the tree.");
        begin_action(ACT_CHOP, fx, fy);
        return;
    }
    if ((r = find_res(rocks, NROCKS, t))) {
        if (pick_power() < 0) {
            int own;
            best_tool(picks, 6, SK_MINING, &own);
            msg(own ? "You need a higher Mining level to use your pickaxe." : "You need a pickaxe to mine this rock.");
            sfx(SFX_ERR);
            return;
        }
        if (skill_level(SK_MINING) < r->level) {
            msg("You need a Mining level of 15 to mine this rock.");
            sfx(SFX_ERR);
            return;
        }
        msg("You swing your pick at the rock.");
        begin_action(ACT_MINE, fx, fy);
        return;
    }
    if (t == T_ROCK_EMPTY) {
        msg("There is currently no ore available in this rock.");
        return;
    }
    if (t == T_STUMP) {
        msg("This tree has been cut down.");
        return;
    }
    if (t == T_BOOTH) {
        bank_open();
        return;
    }
    if (t == T_COUNTER) {
        shop_open();
        return;
    }
    if (pickup_at(pl.x, pl.y)) return;
    if (walkable(fx, fy) && pickup_at(fx, fy)) return;
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
    if (pl.moving) {
        move_entity(&pl.px, &pl.py, pl.x, pl.y, &pl.moving, (keys & KEY_B) ? 4 : 2, &pl.anim);
    }
    if (!pl.moving) {
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

    cam_x = pl.px + 8 - SCREEN_W / 2;
    cam_y = pl.py + 8 - SCREEN_H / 2;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    if (cam_x > MAP_W * 16 - SCREEN_W) cam_x = MAP_W * 16 - SCREEN_W;
    if (cam_y > MAP_H * 16 - SCREEN_H) cam_y = MAP_H * 16 - SCREEN_H;
}

// ---------------------------------------------------------------------------------------
// Sprites
typedef struct { s16 y; s16 who; } SortEnt;   // who: npc index (can exceed 127), -1 = player

static void lunge_offset(int dir, int lunge, int *ox, int *oy) {
    int amt = lunge > 4 ? 8 - lunge : lunge;
    if (amt > 3) amt = 3;
    *ox = DX[dir] * amt;
    *oy = DY[dir] * amt;
}

// human-shaped sprite sets: six consecutive 16x16 frames D0 D1 U0 U1 S0 S1
static void draw_human(int base, int sx, int sy, int dir, int anim, int pal, int moving, int flags) {
    int phase = moving ? (anim >> 3) & 3 : 0;
    int f, hflip = 0;
    if (dir == DIR_DOWN || dir == DIR_UP) {
        f = (dir == DIR_DOWN ? 0 : 2) + (phase & 1);
        hflip = phase == 3;
    } else {
        f = 4 + (phase & 1);
        hflip = dir == DIR_LEFT;
    }
    int bob = (phase & 1) ? -1 : 0;
    obj_sprite(sx, sy + bob, base + f * 4, pal, 2, hflip, 1, OBJF_MOSAIC | flags);
}

// best tool of a kind for the avatar to hold (the wielded one if it is of that kind)
static int shown_tool(const Tool *t, int n, int sk) {
    for (int i = 0; i < n; i++)
        if (pl.equip[EQ_WEAPON] == t[i].id) return t[i].id;
    int own;
    const Tool *b = best_tool(t, n, sk, &own);
    return b ? b->id : 0;
}

// the composited player sprite, animated by what the player is doing
static void draw_player(int sx, int sy) {
    int phase = pl.moving ? (pl.anim >> 3) & 3 : 0;
    int act = AVA_IDLE, t = 0, tool = 0;
    int busy = game_state == GS_WORLD && !pl.moving;
    int since_tick = pl.action_timer * TICK_FRAMES + tick_frames;   // skill tick strikes at 0
    if (busy) switch (pl.action) {
        case ACT_CHOP:
            act = AVA_CHOP;
            tool = shown_tool(axes, 6, SK_WOODCUTTING);
            t = since_tick;
            break;
        case ACT_MINE:
            act = AVA_MINE;
            tool = shown_tool(picks, 6, SK_MINING);
            t = since_tick;
            break;
        case ACT_FISH:
            act = fish_spots[pl.target].kind ? AVA_ROD : AVA_NET;
            tool = act == AVA_ROD ? I_ROD : I_NET;
            t = since_tick;
            break;
        case ACT_COMBAT: {
            Npc *n = &npcs[pl.target];
            if (n->active && adjacent(pl.x, pl.y, n->x, n->y)) {
                act = AVA_COMBAT;
                // the swing pose follows the hit; the wind-up always fills the last 24 frames
                int sp = weapon_speed(), cd = pl.attack_cd > sp ? sp : pl.attack_cd;
                int since = (sp - cd) * TICK_FRAMES + tick_frames, left = sp * TICK_FRAMES - since;
                t = since < 12 ? since : 4 * TICK_FRAMES - left;
                if (t < 12) t = 12;
            }
            break;
        }
        }
    if (phase & 1) sy--;
    avatar_draw_world(sx, sy, pl.dir, phase, act, t, tool);
}

static void draw_hpbar(int sx, int sy, int hp, int max) {
    int k = max ? (hp * 14 + max - 1) / max : 0;
    if (k > 14) k = 14;
    obj_sprite(sx, sy - 7, SPR_HPBAR + k * 2, 6, 1, 0, 0, OBJF_WIDE);
}

void world_draw_sprites(int unused) {
    (void)unused;
    if (game_state == GS_TITLE) return;   // the login screen is black
    int fx = game_state == GS_WORLD || game_state == GS_DIALOG || game_state == GS_TITLE;
    // hitsplats on top
    for (int i = 0; i < MAX_SPLATS && fx; i++)
        if (splats[i].timer) obj_sprite(splats[i].wx - cam_x, splats[i].wy - cam_y - (splats[i].timer > 34 ? 40 - splats[i].timer : 6) + 4, DYN_TILE_BASE + i * 4, 6, 1, 0, 1, 0);

    SortEnt ents[MAX_NPCS + 1];
    int ne = 0;
    ents[ne].y = pl.py;
    ents[ne++].who = -1;
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        if (!n->active) continue;
        int sx = n->px - cam_x, sy = n->py - cam_y;
        if (sx < -16 || sx > SCREEN_W || sy < -24 || sy > SCREEN_H) continue;
        ents[ne].y = n->py;
        ents[ne++].who = i;
    }
    for (int i = 1; i < ne; i++) {
        SortEnt e = ents[i];
        int j = i - 1;
        while (j >= 0 && ents[j].y < e.y) {
            ents[j + 1] = ents[j];
            j--;
        }
        ents[j + 1] = e;
    }
    // bars first so they are above everything
    if (pl.hpbar_timer && fx) draw_hpbar(pl.px - cam_x, pl.py - cam_y, pl.hp, skill_level(SK_HITPOINTS));
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        if (n->active && n->hpbar_timer && fx) draw_hpbar(n->px - cam_x, n->py - cam_y, n->hp, npc_defs[n->kind].hp);
    }
    for (int k = 0; k < ne; k++) {
        int ox, oy;
        if (ents[k].who < 0) {
            lunge_offset(pl.dir, pl.lunge, &ox, &oy);
            draw_player(pl.px - cam_x + ox, pl.py - cam_y + oy);
        } else {
            Npc *n = &npcs[ents[k].who];
            const NpcDef *d = &npc_defs[n->kind];
            lunge_offset(n->dir, n->lunge, &ox, &oy);
            int sx = n->px - cam_x + ox, sy = n->py - cam_y + oy;
            int alt = n->moving ? (n->anim >> 3) & 1 : ((frame >> 5) + ents[k].who) % 7 == 0;
            switch (d->sprite) {
            case SPR_KIND_HUMAN: draw_human(SPR_HUM_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_WIZARD: draw_human(SPR_WIZ_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_WITCH: draw_human(SPR_WITCH_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_PRIEST: draw_human(SPR_PRI_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_DRESS: draw_human(SPR_DRESS_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_SKELETON: draw_human(SPR_SKEL_D0, sx, sy, n->dir, n->anim, d->pal, n->moving, 0); break;
            case SPR_KIND_GHOST: {
                // translucent, hovering
                int hover = ((frame >> 4) + ents[k].who) & 3;
                hover = hover == 3 ? 1 : hover;
                obj_sprite(sx, sy - 2 - hover, ((frame >> 4) & 1) ? SPR_GHOST1 : SPR_GHOST0, d->pal, 2, n->dir == DIR_LEFT, 1, OBJF_SEMI);
                break;
            }
            case SPR_KIND_DEMON:
                obj_sprite(sx - 8, sy - 16, ((frame >> 4) & 1) ? SPR_DEMON1 : SPR_DEMON0, d->pal, 2, n->dir == DIR_LEFT, 0, OBJF_32 | OBJF_MOSAIC);
                break;
            default: {
                int tile;
                switch (d->sprite) {
                case SPR_KIND_COW: tile = alt ? SPR_COW1 : SPR_COW0; break;
                case SPR_KIND_CHICKEN: tile = alt ? SPR_CHK1 : SPR_CHK0; break;
                case SPR_KIND_SHEEP: tile = n->state ? (alt ? SPR_SHORN1 : SPR_SHORN0) : (alt ? SPR_SHEEP1 : SPR_SHEEP0); break;
                case SPR_KIND_IMP: tile = (alt || ((frame >> 3) & 4)) ? SPR_IMP1 : SPR_IMP0; break;
                default: tile = alt ? SPR_RAT1 : SPR_RAT0; break;
                }
                obj_sprite(sx, sy, tile, d->pal, 2, n->dir == DIR_LEFT, 1, OBJF_MOSAIC);
            }
            }
        }
    }
    // shadows under actors
    for (int k = 0; k < ne; k++) {
        int sx, sy;
        if (ents[k].who < 0) {
            sx = pl.px;
            sy = pl.py;
        } else {
            sx = npcs[ents[k].who].px;
            sy = npcs[ents[k].who].py;
        }
        obj_sprite(sx - cam_x, sy - cam_y + 11, SPR_SHADOW, 6, 2, 0, 0, OBJF_WIDE | OBJF_MOSAIC);
    }
    if (!fx) return;
    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active) obj_sprite(fires[i].x * 16 - cam_x, fires[i].y * 16 - cam_y, SPR_FIRE0 + ((frame >> 3) % 3) * 4, 6, 2, (frame >> 4) & 1, 1, OBJF_MOSAIC);
    for (int i = 0; i < FISHSPOT_COUNT; i++)
        obj_sprite(fish_spots[i].x * 16 - cam_x, fish_spots[i].y * 16 - cam_y, SPR_SPOT0 + ((frame / 12 + i) % 3) * 4, 6, 2, 0, 1, OBJF_MOSAIC);
    for (int i = 0; i < MAX_GROUND; i++)
        if (ground[i].id) obj_icon(ground[i].x * 16 - cam_x, ground[i].y * 16 - cam_y, items[ground[i].id].icon, 2);
}
