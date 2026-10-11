// The furnace and anvil screens ("What would you like to smelt / make?"). The furnace lists the
// bars, the anvil everything one metal makes (L/R switch metal); A picks how many (1, 5, 10 or
// All, remembered), and the player then works at the furnace or anvil until done.
#include "ui_internal.h"

static u8 furnace, tier, sel_anvil, sel_furnace, amount_sel = 3;
static const char *note;
static const u8 amounts[4] = {1, 5, 10, 255};   // 255: as many as possible

static int count(void) { return furnace ? SMELT_N : anvil_tiers[tier].n; }
static int sel(void) { return furnace ? sel_furnace : sel_anvil; }

static int level_of(int i) { return furnace ? smelt_defs[i].level : anvil_tiers[tier].list[i].level; }
static int item_of(int i) { return furnace ? smelt_defs[i].bar : anvil_tiers[tier].list[i].item; }
static int can_make(int i) { return furnace ? smelt_max(i) : smith_max(tier, i); }

// "Have / need" line in the detail panel
static int detail_need(int x, int y, int id, int need) {
    text_draw(x, y, items[id].name, TC_GREY);
    y += 10;
    int have = inv_count(id);
    char *p = sadd_num(sbuf, have);
    p = sadd(p, " / ");
    sadd_num(p, need);
    text_right(x + 60, y, sbuf, have >= need ? TC_WHITE : TC_RED);
    return y + 10;
}

static void smithy_draw(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    draw_header(furnace ? "What would you like to smelt?" : "What would you like to make?");
    if (!furnace) {
        // the metal, its bars in the inventory, and L/R to change it
        const AnvilTier *a = &anvil_tiers[tier];
        draw_button(98, 20, 'L');
        icon_at(110, 15, items[a->bar].icon);
        char *p = sadd(sbuf, items[a->bar].name);
        p = sadd(p, " x");
        sadd_num(p, inv_count(a->bar));
        text_draw(128, 20, sbuf, TC_ORANGE);
        draw_button(128 + text_width(sbuf) + 4, 20, 'R');
    }
    draw_panel(6, 31, 152, 91, 5);
    int n = count(), s = sel(), lvl = skill_level(SK_SMITHING);
    for (int k = 0; k < 28; k++) {
        int cx = GRID_X + (k % 7) * 20, cy = 36 + (k / 7) * 21;
        draw_cell(cx, cy, 19, 20, k == s);
        if (k >= n) continue;
        icon_at(cx + 2, cy + 2, items[item_of(k)].icon);
        // a red underline: the level is too low; a grey one: not enough materials
        if (lvl < level_of(k)) cv_fill(&cv_panel, cx + 2, cy + 17, 15, 2, PC_RED);
        else if (!can_make(k)) cv_fill(&cv_panel, cx + 2, cy + 17, 15, 2, PC_EDGE_DIM);
        if (!furnace && anvil_tiers[tier].list[k].qty > 1) {
            fmt_int(sbuf, anvil_tiers[tier].list[k].qty);
            text_draw(cx + 1, cy, sbuf, TC_YELLOW);
        }
    }
    cursor_to(GRID_X + (s % 7) * 20 - 7, 36 + (s / 7) * 21 + 6);
    // details: name, level, materials, XP
    draw_panel(162, 31, 72, 91, 5);
    int id = item_of(s), x = 168, y = 36;
    char lines[2][48];
    int nl = text_wrap(items[id].name, 60, lines, 2);
    for (int i = 0; i < nl; i++, y += 10) text_draw(x, y, lines[i], TC_GOLD);
    cv_fill(&cv_panel, x, y + 1, 60, 1, PC_EDGE_DIM);
    y += 5;
    text_draw(x, y, "Level", TC_GREY);
    fmt_int(sbuf, level_of(s));
    text_right(x + 60, y, sbuf, lvl >= level_of(s) ? TC_WHITE : TC_RED);
    y += 10;
    u32 xp;
    if (furnace) {
        const SmeltDef *d = &smelt_defs[s];
        y = detail_need(x, y, d->ore, 1);
        if (d->ore2) y = detail_need(x, y, d->ore2, d->n2);
        xp = d->xp;
    } else {
        const SmithDef *d = &anvil_tiers[tier].list[s];
        y = detail_need(x, y, anvil_tiers[tier].bar, d->bars);
        xp = (u32)anvil_tiers[tier].xp * d->bars;
    }
    text_draw(x, y, "XP", TC_GREY);
    char *p = sadd_num(sbuf, xp / 10);
    if (xp % 10) {
        p = sadd(p, ".");
        sadd_num(p, xp % 10);
    }
    text_right(x + 60, y, sbuf, TC_WHITE);
    // how many the inventory makes, or why not
    draw_panel(6, 125, 228, 21, 4);
    if (note) {
        text_draw(14, 131, note, TC_CYAN);
    } else {
        p = sadd(sbuf, "You can make ");
        p = sadd_num(p, can_make(s));
        if (!furnace && anvil_tiers[tier].list[s].qty > 1) {
            p = sadd(p, " (x");
            p = sadd_num(p, anvil_tiers[tier].list[s].qty);
            sadd(p, ")");
        }
        text_draw(14, 131, sbuf, TC_GOLD);
    }
    draw_footer(furnace ? "Choose a bar." : "L/R change metal", furnace ? "Smelt" : "Smith", "Close");
    if (pop.open) pop_draw();
}

void smithy_open(int at_furnace) {
    furnace = at_furnace;
    note = 0;
    pop.open = 0;
    if (furnace) {
        // the best bar the player can smelt right now, else the last one picked
        for (int k = SMELT_N - 1; k >= 0; k--)
            if (smelt_max(k) && skill_level(SK_SMITHING) >= smelt_defs[k].level) {
                sel_furnace = k;
                break;
            }
    } else {
        int t = anvil_tier_owned();
        if (t != tier) sel_anvil = 0;
        tier = t < 0 ? 0 : t;
        if (sel_anvil >= anvil_tiers[tier].n) sel_anvil = 0;
    }
    game_state = GS_SMITH;
    player_stop();
    cursor_hide();
    gfx_set_world_fade(7);
    gfx_set_mosaic(1);
    gfx_set_ui_alpha(13, 0);
    sfx(SFX_OK);
    smithy_draw();
}

static void smithy_leave(void) {
    game_state = GS_WORLD;
    pop.open = 0;
    cursor_hide();
    icons_clear();
    gfx_set_world_fade(16);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(12, 0);
    ui_hud_reset();
}

// check what the screen can (A pressed): 1 if the popup may open
static int smithy_check(void) {
    int s = sel();
    if (skill_level(SK_SMITHING) < level_of(s)) {
        note = furnace ? "Your Smithing level is too low to smelt that." : "Your Smithing level is too low to make that.";
        return 0;
    }
    if (!can_make(s)) {
        note = furnace ? "You don't have the ore for that bar." : "You don't have enough bars to make that.";
        return 0;
    }
    return 1;
}

void smithy_update(void) {
    if (pop.open) {
        int r = pop_input();
        if (r >= 0 && r < 4) {
            amount_sel = r;
            int n = amounts[r], m = can_make(sel());
            if (n > m) n = m;
            smithy_leave();
            sfx(SFX_OK);
            if (!smith_start(furnace, furnace ? sel_furnace : tier, sel_anvil, n)) sfx(SFX_ERR);
            return;
        }
        if (r != -1) smithy_draw();
        return;
    }
    int changed = 0;
    if (!furnace && (keys_pressed & (KEY_L | KEY_R))) {
        tier = (tier + ((keys_pressed & KEY_R) ? 1 : ANVIL_TIERS - 1)) % ANVIL_TIERS;
        if (sel_anvil >= anvil_tiers[tier].n) sel_anvil = anvil_tiers[tier].n - 1;
        note = 0;
        sfx(SFX_MOVE);
        changed = 1;
    }
    int n = count(), s = sel();
    int c = s % 7, r = s / 7, rows = (n + 6) / 7;
    if (keys_repeat & KEY_LEFT) c = (c + 6) % 7;
    if (keys_repeat & KEY_RIGHT) c = (c + 1) % 7;
    if (keys_repeat & KEY_UP) r = (r + rows - 1) % rows;
    if (keys_repeat & KEY_DOWN) r = (r + 1) % rows;
    int ns = r * 7 + c;
    if (ns >= n) ns = n - 1;
    if (ns != s) {
        if (furnace) sel_furnace = ns;
        else sel_anvil = ns;
        note = 0;
        sfx(SFX_MOVE);
        changed = 1;
    }
    if (keys_pressed & KEY_A) {
        if (smithy_check()) {
            static const char *const smelt_opts[4] = {"Smelt 1", "Smelt 5", "Smelt 10", "Smelt All"};
            static const char *const smith_opts[4] = {"Smith 1", "Smith 5", "Smith 10", "Smith All"};
            s = sel();
            pop_reset(GRID_X + (s % 7) * 20 + 22, 36 + (s / 7) * 21);
            for (int i = 0; i < 4; i++) pop_add(furnace ? smelt_opts[i] : smith_opts[i], i);
            pop_add("Cancel", 200);
            pop.sel = amount_sel;      // the amount picked last time
            sfx(SFX_OK);
        } else {
            sfx(SFX_ERR);
        }
        changed = 1;
    }
    if (keys_pressed & (KEY_B | KEY_START)) {
        sfx(SFX_BACK);
        smithy_leave();
        return;
    }
    if (changed) smithy_draw();
}
