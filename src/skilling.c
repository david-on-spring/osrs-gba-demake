// Skilling (woodcutting, mining, fishing, firemaking, cooking) and the A-button interact.
#include "world_internal.h"

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

// tools, best first: {item, power, level needed}
const Tool axes[] = {
    {I_RUNE_AXE, 5, 41}, {I_ADAMANT_AXE, 4, 31}, {I_MITHRIL_AXE, 3, 21},
    {I_STEEL_AXE, 2, 6}, {I_IRON_AXE, 1, 1}, {I_BRONZE_AXE, 0, 1},
};
const Tool picks[] = {
    {I_RUNE_PICK, 5, 41}, {I_ADAMANT_PICK, 4, 31}, {I_MITHRIL_PICK, 3, 21},
    {I_STEEL_PICK, 2, 6}, {I_IRON_PICK, 1, 1}, {I_BRONZE_PICK, 0, 1},
};
// best usable tool; *owned is set if the player has any tool of this kind at all
const Tool *best_tool(const Tool *t, int n, int sk, int *owned) {
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

void deplete(int x, int y, int type, int respawn_ticks) {
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

void restore(Depleted *d) {
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

void skill_tick(void) {
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

void interact(void) {
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
