// All user interface: HUD, the START menu (items, equipment, skills, combat, system),
// dialogue, bank, shop, level-up popups and the title screen. Styled after FF7 Remake:
// frosted translucent panels over a dimmed, mosaic-blurred world, chamfered corners,
// thin cyan accents and sliding/breathing selection bars.
#include "game.h"

static char sbuf[96];

static char *sadd(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}
static char *sadd_num(char *p, u32 v) {
    char n[16];
    fmt_num(n, v);
    return sadd(p, n);
}
static void upper(char *dst, const char *src) {
    while (*src) {
        char c = *src++;
        *dst++ = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    *dst = 0;
}

// ---------------------------------------------------------------------------------------
// UI sprite list (icons, cursor, portrait) rebuilt by page redraws, emitted every frame
typedef struct { s16 x, y; u8 icon; } UiIcon;
static UiIcon ui_icons[40];
static int ui_icon_n;
static s16 cursor_x = -100, cursor_y, cursor_tx = -100, cursor_ty;
static u8 show_portrait;
static s16 portrait_x, portrait_y;
static u8 show_arrow;
static s16 slide;   // UI horizontal slide offset (pixels)

static void icons_clear(void) { ui_icon_n = 0; }
static void icon_at(int x, int y, int icon) {
    if (ui_icon_n < 40) {
        ui_icons[ui_icon_n].x = x;
        ui_icons[ui_icon_n].y = y;
        ui_icons[ui_icon_n++].icon = icon;
    }
}
static void cursor_to(int x, int y) {
    cursor_tx = x;
    cursor_ty = y;
    if (cursor_x < -50) {
        cursor_x = x;
        cursor_y = y;
    }
}
static void cursor_hide(void) { cursor_x = cursor_tx = -100; }

// ---------------------------------------------------------------------------------------
// HUD
typedef struct { char text[96]; u8 col; u16 timer; } Msg;
static Msg msgs[2];
typedef struct { u8 sk, timer; u16 amount; } XpDrop;
static XpDrop drops[3];
static int area_region = -1, area_timer;
static s8 target_npc = -1;
static u8 cached_hp = 255, cached_target_hp = 255;
static u8 hud_dirty_all = 1, hud_dirty_msg, hud_dirty_xp, hud_dirty_area, hud_dirty_status, hud_dirty_target;

typedef struct { u8 sk, lvl; } LevelUp;
static LevelUp lvlq[6];
static int lvlq_n;

void msg_col(const char *s, int col) {
    msgs[0] = msgs[1];
    int i = 0;
    for (; s[i] && i < 95; i++) msgs[1].text[i] = s[i];
    msgs[1].text[i] = 0;
    msgs[1].col = col;
    msgs[1].timer = 240;
    hud_dirty_msg = 1;
}
void msg(const char *s) { msg_col(s, TC_WHITE); }

void xp_drop(int sk, u32 tenths) {
    // merge into an existing drop of the same skill if it is still fresh
    for (int i = 0; i < 3; i++)
        if (drops[i].timer > 60 && drops[i].sk == sk) {
            drops[i].amount += tenths;
            drops[i].timer = 90;
            hud_dirty_xp = 1;
            return;
        }
    drops[0] = drops[1];
    drops[1] = drops[2];
    drops[2].sk = sk;
    drops[2].amount = tenths;
    drops[2].timer = 90;
    hud_dirty_xp = 1;
}

void level_up(int sk, int lvl) {
    if (lvlq_n < 6) {
        lvlq[lvlq_n].sk = sk;
        lvlq[lvlq_n++].lvl = lvl;
    }
}

void ui_set_target(int n) {
    target_npc = n;
    cached_target_hp = 255;
    hud_dirty_target = 1;
}

static void hud_clear_rect(int x, int y, int w, int h) {
    cv_fill(&cv_text, x, y, w, h, 0);
    cv_fill(&cv_panel, x, y, w, h, 0);
}

static void hud_draw_status(void) {
    hud_clear_rect(150, 132, 90, 28);
    int max = skill_level(SK_HITPOINTS);
    draw_panel(154, 134, 84, 24, 5);
    text_spaced(162, 137, "HP", TC_CYAN, 1);
    char *p = sbuf;
    p = sadd_num(p, pl.hp);
    p = sadd(p, " / ");
    sadd_num(p, max);
    text_right(232, 137, sbuf, pl.hp * 4 <= max ? TC_RED : TC_WHITE);
    draw_bar(160, 149, 74, pl.hp, max, pl.hp * 4 <= max ? PC_RED : PC_GREEN, PC_GREEN_D);
}

static void hud_draw_msgs(void) {
    hud_clear_rect(0, 116, 150, 44);
    int any = 0, w = 0;
    for (int i = 0; i < 2; i++)
        if (msgs[i].timer) {
            any = 1;
            int tw = text_width(msgs[i].text);
            if (tw > w) w = tw;
        }
    if (!any) return;
    char lines[3][48];
    int y = 147;
    for (int i = 1; i >= 0; i--) {
        if (!msgs[i].timer) continue;
        int n = text_wrap(msgs[i].text, 144, lines, 3);
        for (int k = n - 1; k >= 0 && y >= 116; k--) {
            text_draw(6, y, lines[k], msgs[i].col);
            y -= 10;
        }
    }
}

static s16 drop_icon_x[3], drop_icon_y[3];
static u8 drop_icon_on[3];

static void hud_draw_xp(void) {
    hud_clear_rect(130, 76, 110, 56);
    int row = 0;
    for (int i = 0; i < 3; i++) drop_icon_on[i] = 0;
    for (int i = 2; i >= 0; i--) {
        if (!drops[i].timer) continue;
        int rise = drops[i].timer > 84 ? (drops[i].timer - 84) : 0;
        int y = 114 - row * 16 + rise;
        char *p = sbuf;
        p = sadd(p, "+");
        p = sadd_num(p, drops[i].amount / 10);
        if (drops[i].amount % 10) {
            p = sadd(p, ".");
            *p++ = '0' + drops[i].amount % 10;
            *p = 0;
        }
        sadd(p, " xp");
        int x = text_right(234, y, sbuf, drops[i].timer < 20 ? TC_DIM : TC_CYAN);
        drop_icon_on[i] = drops[i].timer >= 20;
        drop_icon_x[i] = x - text_width(sbuf) - 18;
        drop_icon_y[i] = y - 4;
        row++;
    }
}

static void hud_draw_area(void) {
    hud_clear_rect(0, 0, 128, 22);
    if (!area_timer) return;
    char up[32];
    upper(up, regions[area_region].name);
    int w = text_spaced_width(up, 2);
    draw_panel(-6, 4, w + 26, 16, 4);
    cv_fill(&cv_panel, 4, 7, 2, 10, PC_EDGE);
    text_spaced(10, 7, up, TC_WHITE, 2);
}

static void hud_draw_target(void) {
    hud_clear_rect(128, 0, 112, 26);
    if (target_npc < 0 || !npcs[(int)target_npc].active) return;
    Npc *n = &npcs[(int)target_npc];
    const NpcDef *d = &npc_defs[n->kind];
    draw_panel(130, 3, 106, 22, 4);
    text_draw(136, 5, d->name, TC_WHITE);
    char *p = sadd(sbuf, "Lv ");
    sadd_num(p, d->level);
    text_right(230, 5, sbuf, TC_GOLD);
    draw_bar(135, 17, 96, n->hp, d->hp, PC_RED, PC_RED);
}

void ui_hud_reset(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    hud_dirty_all = 1;
    cached_hp = 255;
}

void ui_hud_update(void) {
    for (int i = 0; i < 2; i++)
        if (msgs[i].timer && !--msgs[i].timer) hud_dirty_msg = 1;
    for (int i = 0; i < 3; i++)
        if (drops[i].timer) {
            drops[i].timer--;
            if (drops[i].timer == 0 || drops[i].timer > 84 || drops[i].timer == 19) hud_dirty_xp = 1;
        }
    int r = world_region();
    if (r != area_region) {
        area_region = r;
        music_for_region(regions[r].name);
        area_timer = 150;
        hud_dirty_area = 1;
    }
    if (area_timer && !--area_timer) hud_dirty_area = 1;
    if (pl.hp != cached_hp) {
        cached_hp = pl.hp;
        hud_dirty_status = 1;
    }
    if (target_npc >= 0) {
        Npc *n = &npcs[(int)target_npc];
        if (!n->active) {
            target_npc = -1;
            hud_dirty_target = 1;
        } else if (n->hp != cached_target_hp) {
            cached_target_hp = n->hp;
            hud_dirty_target = 1;
        }
        if (pl.action != ACT_COMBAT && !n->in_combat && !n->hpbar_timer) {
            target_npc = -1;
            hud_dirty_target = 1;
        }
    }
    if (hud_dirty_all) {
        hud_dirty_all = 0;
        hud_dirty_msg = hud_dirty_xp = hud_dirty_area = hud_dirty_status = hud_dirty_target = 1;
    }
    if (hud_dirty_status) hud_draw_status();
    if (hud_dirty_msg) hud_draw_msgs();
    if (hud_dirty_xp) hud_draw_xp();
    if (hud_dirty_area) hud_draw_area();
    if (hud_dirty_target) hud_draw_target();
    hud_dirty_status = hud_dirty_msg = hud_dirty_xp = hud_dirty_area = hud_dirty_target = 0;

    if (game_state == GS_WORLD) {
        int q = quest_pending();
        if (q >= 0) {
            quest_complete_popup(q);
            return;
        }
    }
    if (lvlq_n && game_state == GS_WORLD) {
        game_state = GS_LEVELUP;
        sfx(SFX_LEVEL);
        cv_clear(&cv_text);
        cv_clear(&cv_panel);
        LevelUp *l = &lvlq[0];
        draw_panel(30, 46, 180, 62, 8);
        text_spaced(84, 52, "LEVEL UP!", TC_GOLD, 3);
        draw_rule(40, 64, 160);
        char *p = sadd(sbuf, "Congratulations! Your ");
        sadd(p, skill_names[l->sk]);
        text_center(120, 70, sbuf, TC_WHITE);
        p = sadd(sbuf, "level is now ");
        p = sadd_num(p, l->lvl);
        sadd(p, ".");
        text_center(120, 81, sbuf, TC_CYAN);
        draw_button(170, 94, 'A');
        text_draw(180, 94, "OK", TC_GREY);
        icons_clear();
        icon_at(40, 54, skill_icons[l->sk]);
        icon_at(184, 54, skill_icons[l->sk]);
        gfx_set_world_fade(10);
    }
}

// OSRS-style "Quest complete!" scroll with the rewards, quest points and a fanfare
void quest_complete_popup(int q) {
    const QuestDef *d = &quest_defs[q];
    game_state = GS_QUESTDONE;
    player_stop();
    sfx(SFX_QUEST);
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    gfx_set_world_fade(8);
    gfx_set_mosaic(1);
    gfx_set_ui_alpha(13, 0);
    int rows = 1 + d->nrewards;
    int h = 78 + rows * 10;
    int y0 = (160 - h) / 2;
    draw_panel(18, y0, 204, h, 8);
    cv_fill(&cv_panel, 26, y0 + 3, 188, 1, PC_GOLD);
    cv_fill(&cv_panel, 26, y0 + h - 4, 188, 1, PC_GOLD);
    text_spaced(120 - text_spaced_width("QUEST COMPLETE!", 3) / 2, y0 + 8, "QUEST COMPLETE!", TC_GOLD, 3);
    draw_rule(28, y0 + 20, 184);
    text_center(120, y0 + 24, "Congratulations! You have completed", TC_WHITE);
    text_center(120, y0 + 35, d->name, TC_YELLOW);
    draw_cell(28, y0 + 48, 22, 22, 0);
    icon_at(31, y0 + 51, items[d->icon].icon);
    text_spaced(60, y0 + 48, "YOU ARE AWARDED", TC_CYAN, 1);
    int y = y0 + 60;
    char *p = sadd_num(sbuf, d->qp);
    sadd(p, d->qp == 1 ? " Quest Point" : " Quest Points");
    text_draw(64, y, sbuf, TC_WHITE);
    y += 10;
    for (int i = 0; i < d->nrewards; i++, y += 10) {
        const QReward *r = &d->rewards[i];
        if (r->type == QR_XP) {
            p = sadd_num(sbuf, r->amount);
            p = sadd(p, " ");
            p = sadd(p, skill_names[r->skill]);
            sadd(p, " XP");
        } else if (r->type == QR_ITEM) {
            p = sbuf;
            if (r->amount > 1) {
                p = sadd_num(p, r->amount);
                p = sadd(p, r->item == I_COINS ? " " : " x ");
            }
            sadd(p, items[r->item].name);
        } else {
            sadd(sbuf, r->text);
        }
        text_draw(64, y, sbuf, TC_WHITE);
    }
    p = sadd(sbuf, "Quest Points: ");
    sadd_num(p, quest_points());
    text_draw(28, y0 + h - 16, sbuf, TC_GOLD);
    draw_button(178, y0 + h - 16, 'A');
    text_draw(188, y0 + h - 16, "OK", TC_GREY);
}

void questdone_update(void) {
    if (keys_pressed & (KEY_A | KEY_B)) {
        sfx(SFX_OK);
        icons_clear();
        gfx_set_world_fade(16);
        gfx_set_mosaic(0);
        gfx_set_ui_alpha(12, 0);
        game_state = GS_WORLD;
        ui_hud_reset();
    }
}

void levelup_update(void) {
    if (keys_pressed & (KEY_A | KEY_B)) {
        sfx(SFX_OK);
        for (int i = 1; i < lvlq_n; i++) lvlq[i - 1] = lvlq[i];
        lvlq_n--;
        icons_clear();
        gfx_set_world_fade(16);
        game_state = GS_WORLD;
        ui_hud_reset();
    }
}

// ---------------------------------------------------------------------------------------
// Common menu chrome
static void draw_clock(void) {
    cv_fill(&cv_text, 150, 2, 90, 12, 0);
    u32 secs = pl.playtime / 60;
    char *p = sbuf;
    p = sadd_num(p, secs / 3600);
    *p++ = ':';
    *p++ = '0' + (secs / 60 % 60) / 10;
    *p++ = '0' + (secs / 60 % 60) % 10;
    *p++ = ':';
    *p++ = '0' + (secs % 60) / 10;
    *p++ = '0' + (secs % 60) % 10;
    *p = 0;
    text_right(232, 4, sbuf, TC_WHITE);
    text_spaced(232 - text_width(sbuf) - 30, 4, "TIME", TC_CYAN, 1);
}

static void draw_header(const char *title) {
    char up[24];
    upper(up, title);
    cv_fill(&cv_panel, 8, 5, 3, 9, PC_EDGE);
    text_spaced(15, 4, up, TC_WHITE, 2);
    draw_rule(8, 16, 224);
    draw_clock();
}

static void draw_footer(const char *desc, const char *a_label, const char *b_label) {
    if (desc) text_draw(10, 150, desc, TC_GREY);
    int x = 232;
    if (b_label) {
        x -= text_width(b_label);
        text_draw(x, 150, b_label, TC_GREY);
        x -= 11;
        draw_button(x, 150, 'B');
        x -= 8;
    }
    if (a_label) {
        x -= text_width(a_label);
        text_draw(x, 150, a_label, TC_GREY);
        x -= 11;
        draw_button(x, 150, 'A');
    }
}

// ---------------------------------------------------------------------------------------
// Popup (context menu)
static struct {
    u8 open, n, sel;
    s16 x, y;
    const char *opts[6];
    u8 ids[6];
} pop;

static s16 pop_rx, pop_ry, pop_rw, pop_rh;

static void pop_reset(int x, int y) {
    pop.open = 1;
    pop.n = 0;
    pop.sel = 0;
    pop.x = x;
    pop.y = y;
}
static void pop_add(const char *s, int id) {
    pop.opts[pop.n] = s;
    pop.ids[pop.n++] = id;
}
static void pop_draw(void) {
    int w = 0;
    for (int i = 0; i < pop.n; i++) {
        int tw = text_width(pop.opts[i]);
        if (tw > w) w = tw;
    }
    w += 22;
    int h = pop.n * 12 + 8;
    int x = pop.x, y = pop.y;
    if (x + w > 236) x = 236 - w;
    if (y + h > 146) y = 146 - h;
    cv_fill(&cv_text, x, y, w, h, 0);
    draw_panel(x, y, w, h, 4);
    pop_rx = x;
    pop_ry = y;
    pop_rw = w;
    pop_rh = h;
    for (int i = 0; i < pop.n; i++) {
        int yy = y + 4 + i * 12;
        if (i == pop.sel) {
            draw_highlight(x + 3, yy, w - 6, 11);
            cursor_to(x - 6, yy + 2);
        }
        text_draw(x + 12, yy + 1, pop.opts[i], i == pop.sel ? TC_WHITE : TC_GREY);
    }
}
// returns chosen id, -2 when cancelled, -1 while open
static int pop_input(void) {
    if (keys_repeat & KEY_UP) {
        pop.sel = (pop.sel + pop.n - 1) % pop.n;
        sfx(SFX_MOVE);
        return -3;
    }
    if (keys_repeat & KEY_DOWN) {
        pop.sel = (pop.sel + 1) % pop.n;
        sfx(SFX_MOVE);
        return -3;
    }
    if (keys_pressed & KEY_A) {
        pop.open = 0;
        sfx(SFX_OK);
        return pop.ids[pop.sel];
    }
    if (keys_pressed & KEY_B) {
        pop.open = 0;
        sfx(SFX_BACK);
        return -2;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------
// Item grid (inventory, bank, shop)
#define GRID_X 12
#define GRID_Y 26
static void draw_grid(const Slot *slots, int count, int first, int sel) {
    for (int k = 0; k < 28; k++) {
        int i = first + k;
        int cx = GRID_X + (k % 7) * 20, cy = GRID_Y + (k / 7) * 22;
        draw_cell(cx, cy, 19, 20, i == sel);
        if (i >= count || !slots[i].id) continue;
        icon_at(cx + 2, cy + 2, items[slots[i].id].icon);
        if (slots[i].qty > 1 || (items[slots[i].id].flags & IF_STACK)) {
            char n[12];
            u32 q = slots[i].qty;
            if (q >= 100000) {
                fmt_int(n, q / 1000);
                char *e = n;
                while (*e) e++;
                e[0] = 'K';
                e[1] = 0;
            } else {
                fmt_int(n, q);
            }
            text_draw(cx + 1, cy, n, q >= 100000 ? TC_WHITE : TC_YELLOW);
        }
    }
    if (sel >= first && sel < first + 28) {
        int k = sel - first;
        cursor_to(GRID_X + (k % 7) * 20 - 7, GRID_Y + (k / 7) * 22 + 6);
    }
}

static void draw_item_detail(int id, int qty, int x, int y, int w, int price_mode, int price) {
    if (!id) {
        text_draw(x + 6, y + 6, "Empty", TC_DIM);
        return;
    }
    const ItemDef *d = &items[id];
    char lines[3][48];
    int n = text_wrap(d->name, w - 12, lines, 2);
    int yy = y + 5;
    for (int i = 0; i < n; i++, yy += 10) text_draw(x + 6, yy, lines[i], TC_GOLD);
    yy += 2;
    cv_fill(&cv_panel, x + 6, yy, w - 12, 1, PC_EDGE_DIM);
    yy += 4;
    if (qty > 1) {
        text_draw(x + 6, yy, "Amount", TC_GREY);
        fmt_num(sbuf, qty);
        text_right(x + w - 6, yy, sbuf, TC_WHITE);
        yy += 10;
    }
    if (d->flags & IF_EQUIP) {
        static const char *const lab[3] = {"Attack", "Strength", "Defence"};
        int v[3] = {d->atk, d->str, d->def};
        for (int i = 0; i < 3; i++) {
            if (!v[i]) continue;
            text_draw(x + 6, yy, lab[i], TC_GREY);
            char *p = sbuf;
            if (v[i] > 0) p = sadd(p, "+");
            fmt_int(p, v[i]);
            text_right(x + w - 6, yy, sbuf, v[i] > 0 ? TC_GREEN : TC_RED);
            yy += 10;
        }
        if (d->req > 1) {
            text_draw(x + 6, yy, d->slot == EQ_WEAPON ? "Req Att" : "Req Def", TC_GREY);
            fmt_int(sbuf, d->req);
            int have = skill_level(d->slot == EQ_WEAPON ? SK_ATTACK : SK_DEFENCE);
            text_right(x + w - 6, yy, sbuf, have >= d->req ? TC_WHITE : TC_RED);
            yy += 10;
        }
    }
    if (d->heal) {
        text_draw(x + 6, yy, "Heals", TC_GREY);
        fmt_int(sbuf, d->heal);
        text_right(x + w - 6, yy, sbuf, TC_GREEN);
        yy += 10;
    }
    if (price_mode) {
        text_draw(x + 6, yy, price_mode == 1 ? "Price" : "Sells", TC_GREY);
        char *p = sadd_num(sbuf, price);
        sadd(p, " gp");
        text_right(x + w - 6, yy, sbuf, TC_GOLD);
    } else if (id != I_COINS) {
        text_draw(x + 6, yy, "Value", TC_GREY);
        char *p = sadd_num(sbuf, d->value);
        sadd(p, " gp");
        text_right(x + w - 6, yy, sbuf, TC_DIM);
    }
}

// ---------------------------------------------------------------------------------------
// START menu
enum { P_MAIN, P_ITEMS, P_EQUIP, P_SKILLS, P_QUESTS, P_COMBAT, P_SYSTEM };
#define MAIN_N 6
static u8 page, main_sel, item_sel, eq_sel, eq_choose, eq_cand_sel, sk_sel, cb_sel, sys_sel;
static u8 q_sel, q_view, q_scroll;
static u8 cand[28], cand_n;
static s8 anim;          // >0 opening, <0 closing
static u8 redraw;
static const char *sys_note;
u16 redraw_lines;

static const char *const main_items[MAIN_N] = {"Items", "Equipment", "Skills", "Quests", "Combat", "System"};
static const char *const main_desc[MAIN_N] = {
    "Use and manage items.", "Change your gear.", "Review your skills.", "Your quest journal.",
    "Pick an attack style.", "Save and options.",
};

static void draw_main(void) {
    draw_header("Menu");
    draw_panel(6, 22, 84, 112, 6);
    for (int i = 0; i < MAIN_N; i++) {
        int y = 30 + i * 17;
        if (i == main_sel) {
            draw_highlight(9, y - 3, 78, 15);
            cursor_to(1, y);
        }
        text_draw(16, y, main_items[i], i == main_sel ? TC_WHITE : TC_GREY);
    }
    // character card
    draw_panel(96, 22, 138, 70, 6);
    draw_cell(102, 28, 36, 38, 0);
    show_portrait = 1;
    portrait_x = 104;
    portrait_y = 30;
    text_draw(144, 27, "Adventurer", TC_WHITE);
    char *p = sadd(sbuf, "Combat Lv ");
    sadd_num(p, combat_level());
    text_draw(144, 38, sbuf, TC_GOLD);
    int max = skill_level(SK_HITPOINTS);
    text_spaced(144, 51, "HP", TC_CYAN, 1);
    p = sadd_num(sbuf, pl.hp);
    p = sadd(p, " / ");
    sadd_num(p, max);
    text_right(228, 51, sbuf, TC_WHITE);
    draw_bar(143, 62, 88, pl.hp, max, PC_GREEN, PC_GREEN_D);
    text_draw(104, 75, regions[world_region()].name, TC_GREY);
    p = sadd_num(sbuf, inv_count(I_COINS));
    sadd(p, " gp");
    text_right(228, 75, sbuf, TC_GOLD);
    // stats card
    draw_panel(96, 96, 138, 38, 6);
    text_draw(104, 99, "Total level", TC_GREY);
    fmt_num(sbuf, total_level());
    text_right(228, 99, sbuf, TC_WHITE);
    u32 txp = 0;
    for (int i = 0; i < SK_COUNT; i++) txp += pl.xp[i] / 10;
    text_draw(104, 110, "Total XP", TC_GREY);
    fmt_num(sbuf, txp);
    text_right(228, 110, sbuf, TC_WHITE);
    text_draw(104, 121, "Quest points", TC_GREY);
    fmt_num(sbuf, quest_points());
    text_right(228, 121, sbuf, TC_GOLD);
    draw_footer(main_desc[main_sel], "Select", "Close");
}

static void draw_items(void) {
    draw_header("Items");
    draw_panel(6, 20, 152, 98, 5);
    draw_grid(pl.inv, 28, 0, item_sel);
    draw_panel(162, 20, 72, 98, 5);
    Slot *s = &pl.inv[item_sel];
    draw_item_detail(s->id, s->qty, 162, 20, 72, 0, 0);
    draw_panel(6, 122, 228, 24, 4);
    char lines[2][48];
    int n = s->id ? text_wrap(items[s->id].examine, 212, lines, 2) : 0;
    if (!s->id) text_draw(14, 129, "An empty inventory slot.", TC_DIM);
    for (int i = 0; i < n; i++) text_draw(14, 125 + i * 10 + (n == 1 ? 4 : 0), lines[i], TC_GREY);
    char *p = sadd(sbuf, "Free slots ");
    sadd_num(p, inv_free());
    draw_footer(sbuf, s->id ? "Use" : 0, "Back");
}

static const char *const slot_names[EQ_COUNT] = {"HEAD", "WEAPON", "BODY", "SHIELD", "LEGS", "FEET", "NECK"};

static void build_candidates(void) {
    cand_n = 0;
    for (int i = 0; i < 28; i++) {
        int id = pl.inv[i].id;
        if (id && (items[id].flags & IF_EQUIP) && items[id].slot == eq_sel) cand[cand_n++] = i;
    }
}

static void draw_bonus_row(int x, int y, int w, const char *lab, int val, int delta, int show_delta) {
    text_draw(x, y, lab, TC_GREY);
    char *p = sbuf;
    if (val >= 0) p = sadd(p, "+");
    fmt_int(p, val);
    int xr = x + w;
    if (show_delta && delta) {
        char d[8];
        char *q = d;
        if (delta > 0) q = sadd(q, "+");
        fmt_int(q, delta);
        xr -= text_width(d) + 4;
        text_right(x + w, y, d, delta > 0 ? TC_GREEN : TC_RED);
    }
    text_right(xr, y, sbuf, TC_WHITE);
}

static void draw_equip(void) {
    draw_header("Equipment");
    draw_panel(6, 20, 120, 126, 5);
    for (int i = 0; i < EQ_COUNT; i++) {
        int y = 23 + i * 17;
        int id = pl.equip[i];
        if (i == eq_sel) {
            draw_highlight(9, y - 1, 114, 17);
            if (!eq_choose) cursor_to(1, y + 5);
        }
        draw_cell(12, y - 1, 18, 17, 0);
        if (id) icon_at(13, y - 1, items[id].icon);
        text_spaced(34, y - 1, slot_names[i], TC_CYAN, 1);
        text_draw(34, y + 7, id ? items[id].name : "-", id ? TC_WHITE : TC_DIM);
    }
    draw_panel(130, 20, 104, 126, 5);
    int atk = equip_bonus(0), str = equip_bonus(1), def = equip_bonus(2);
    if (!eq_choose) {
        // paper-doll: the same composited sprite as in the world, at 2x
        draw_cell(148, 24, 68, 42, 0);
        show_portrait = 1;
        portrait_x = 166;
        portrait_y = 28;
        draw_rule(136, 69, 92);
        draw_bonus_row(138, 73, 88, "Attack", atk, 0, 0);
        draw_bonus_row(138, 84, 88, "Strength", str, 0, 0);
        draw_bonus_row(138, 95, 88, "Defence", def, 0, 0);
        int eff_s = skill_level(SK_STRENGTH) + (pl.style == STYLE_AGGRESSIVE ? 3 : 0) + 8;
        int maxhit = (eff_s * (str + 64) + 320) / 640;
        text_draw(138, 110, "Max hit", TC_GREY);
        fmt_int(sbuf, maxhit < 1 ? 1 : maxhit);
        text_right(226, 110, sbuf, TC_GOLD);
        text_draw(138, 121, "Combat Lv", TC_GREY);
        fmt_int(sbuf, combat_level());
        text_right(226, 121, sbuf, TC_GOLD);
        draw_footer("Pick a slot.", "Change", "Back");
    } else {
        text_spaced(138, 25, slot_names[eq_sel], TC_CYAN, 2);
        draw_rule(136, 36, 92);
        int n = cand_n + (pl.equip[eq_sel] ? 1 : 0);
        if (!n) text_draw(138, 42, "Nothing to equip.", TC_DIM);
        int hover_id = -1;
        for (int i = 0; i < n && i < 5; i++) {
            int y = 40 + i * 13;
            const char *name;
            if (i < cand_n) name = items[pl.inv[cand[i]].id].name;
            else name = "Remove";
            if (i == eq_cand_sel) {
                draw_highlight(133, y - 2, 98, 12);
                cursor_to(125, y + 1);
                hover_id = i < cand_n ? pl.inv[cand[i]].id : 0;
            }
            text_draw(140, y, name, i == eq_cand_sel ? TC_WHITE : TC_GREY);
        }
        if (hover_id >= 0) {
            const ItemDef *now = &items[pl.equip[eq_sel]], *nw = &items[hover_id];
            draw_rule(136, 106, 92);
            draw_bonus_row(138, 110, 88, "Atk", atk, nw->atk - now->atk, 1);
            draw_bonus_row(138, 121, 88, "Str", str, nw->str - now->str, 1);
            draw_bonus_row(138, 132, 88, "Def", def, nw->def - now->def, 1);
        }
        draw_footer("Choose an item.", "Equip", "Cancel");
    }
}

static void draw_skills(void) {
    draw_header("Skills");
    draw_panel(6, 20, 228, 92, 5);
    for (int i = 0; i < SK_COUNT; i++) {
        int c = i / 5, r = i % 5;
        int x = 10 + c * 112, y = 25 + r * 17;
        if (i == sk_sel) {
            draw_highlight(x, y - 1, 108, 16);
            cursor_to(x - 8, y + 4);
        }
        icon_at(x + 4, y - 1, skill_icons[i]);
        text_draw(x + 24, y + 3, skill_names[i], i == sk_sel ? TC_WHITE : TC_GREY);
        fmt_int(sbuf, skill_level(i));
        text_right(x + 102, y + 3, sbuf, TC_GOLD);
    }
    draw_panel(6, 115, 228, 32, 4);
    int lvl = skill_level(sk_sel);
    u32 xp = pl.xp[sk_sel] / 10;
    text_draw(14, 118, skill_names[sk_sel], TC_GOLD);
    char *p = sadd(sbuf, "XP ");
    sadd_num(p, xp);
    text_draw(80, 118, sbuf, TC_WHITE);
    if (lvl < 99) {
        u32 next = xp_table[lvl + 1], cur = xp_table[lvl];
        p = sadd(sbuf, "Next ");
        sadd_num(p, next - xp);
        text_right(226, 118, sbuf, TC_GREY);
        draw_bar(13, 133, 214, xp - cur, next - cur, PC_BLUE, PC_BLUE_D);
    } else {
        text_right(226, 118, "Max level!", TC_GOLD);
        draw_bar(13, 133, 214, 1, 1, PC_GOLD, PC_GOLD);
    }
    p = sadd(sbuf, "Total level ");
    sadd_num(p, total_level());
    draw_footer(sbuf, 0, "Back");
}

static void draw_combat(void) {
    static const char *const styles[3] = {"Accurate", "Aggressive", "Defensive"};
    static const char *const style_xp[3] = {"Attack XP", "Strength XP", "Defence XP"};
    draw_header("Combat");
    draw_panel(6, 20, 228, 44, 5);
    int w = pl.equip[EQ_WEAPON];
    draw_cell(12, 26, 18, 18, 0);
    if (w) icon_at(13, 27, items[w].icon);
    text_draw(36, 26, w ? items[w].name : "Unarmed", TC_WHITE);
    char *p = sadd(sbuf, "Combat Lv ");
    sadd_num(p, combat_level());
    text_draw(36, 37, sbuf, TC_GOLD);
    text_draw(14, 50, "Fight with a style that suits you.", TC_DIM);
    draw_panel(6, 68, 228, 78, 5);
    text_spaced(14, 72, "ATTACK STYLE", TC_CYAN, 2);
    draw_rule(12, 83, 216);
    for (int i = 0; i < 4; i++) {
        int y = 88 + i * 14;
        if (i == cb_sel) {
            draw_highlight(10, y - 2, 220, 13);
            cursor_to(3, y + 1);
        }
        if (i < 3) {
            cv_fill(&cv_panel, 18, y + 2, 5, 5, pl.style == i ? PC_EDGE : PC_BAR_BG);
            text_draw(30, y, styles[i], i == cb_sel ? TC_WHITE : TC_GREY);
            text_right(224, y, style_xp[i], pl.style == i ? TC_CYAN : TC_DIM);
        } else {
            text_draw(30, y, "Auto retaliate", i == cb_sel ? TC_WHITE : TC_GREY);
            text_right(224, y, pl.autoret ? "ON" : "OFF", pl.autoret ? TC_GREEN : TC_RED);
        }
    }
    draw_footer("Hitpoints always train.", "Set", "Back");
}

static void draw_system(void) {
    static const char *const opts[3] = {"Save game", "Music", "Close menu"};
    draw_header("System");
    draw_panel(6, 22, 140, 66, 6);
    for (int i = 0; i < 3; i++) {
        int y = 31 + i * 18;
        if (i == sys_sel) {
            draw_highlight(9, y - 3, 134, 15);
            cursor_to(1, y);
        }
        text_draw(16, y, opts[i], i == sys_sel ? TC_WHITE : TC_GREY);
        if (i == 1) text_right(136, y, pl.music ? "ON" : "OFF", pl.music ? TC_GREEN : TC_RED);
    }
    draw_panel(150, 22, 84, 66, 6);
    text_spaced(158, 27, "SAVE DATA", TC_CYAN, 1);
    draw_rule(156, 38, 72);
    text_draw(158, 44, save_exists() ? "Slot in use" : "No save yet", TC_GREY);
    if (sys_note) {
        char lines[2][48];
        int n = text_wrap(sys_note, 70, lines, 2);
        for (int i = 0; i < n; i++) text_draw(158, 58 + i * 10, lines[i], TC_GREEN);
    }
    draw_footer("Saved to cartridge SRAM.", "Select", "Back");
}

static void set_page(int p);

// ---------------------------------------------------------------------------------------
// Quest journal: list (red = not started, yellow = in progress, green = complete) and an
// OSRS-style journal per quest with completed steps struck through.
static int quest_colour(int q) {
    return quest_done(q) ? TC_GREEN : quest_stage(q) ? TC_YELLOW : TC_RED;
}

static void draw_quest_row(int q) {
    int y = 24 + q * 9;
    if (q == q_sel) {
        draw_highlight(9, y - 1, 130, 10);
        cursor_to(1, y + 1);
    }
    text_draw(16, y, quest_defs[q].name, quest_colour(q));
}

static void draw_quest_side(void) {
    int total = 0, done = 0, started = 0;
    for (int q = 0; q < QUEST_COUNT; q++) {
        total += quest_defs[q].qp;
        if (quest_done(q)) done++;
        else if (quest_stage(q)) started++;
    }
    text_spaced(153, 25, "QUEST POINTS", TC_CYAN, 1);
    draw_rule(152, 36, 76);
    char *p = sadd_num(sbuf, quest_points());
    text_big(176, 40, sbuf);
    p = sadd(sbuf, "of ");
    sadd_num(p, total);
    text_draw(200, 50, sbuf, TC_GREY);
    draw_bar(151, 64, 78, quest_points(), total, PC_GOLD, PC_GOLD);
    static const char *const lab[3] = {"Complete", "Started", "To do"};
    int val[3] = {done, started, QUEST_COUNT - done - started};
    static const u8 col[3] = {TC_GREEN, TC_YELLOW, TC_RED};
    for (int i = 0; i < 3; i++) {
        text_draw(153, 74 + i * 11, lab[i], col[i]);
        fmt_int(sbuf, val[i]);
        text_right(226, 74 + i * 11, sbuf, TC_WHITE);
    }
    draw_rule(152, 109, 76);
    const QuestDef *d = &quest_defs[q_sel];
    const char *st = quest_done(q_sel) ? "Completed" : quest_stage(q_sel) ? "In progress" : "Not started";
    text_draw(153, 113, st, quest_colour(q_sel));
    if (d->req_qp && !quest_stage(q_sel)) {
        p = sadd(sbuf, "Needs ");
        p = sadd_num(p, d->req_qp);
        sadd(p, " QP");
        text_draw(153, 124, sbuf, quest_points() >= d->req_qp ? TC_GREY : TC_RED);
    } else {
        p = sadd(sbuf, "Reward ");
        p = sadd_num(p, d->qp);
        sadd(p, " QP");
        text_draw(153, 124, sbuf, TC_GREY);
    }
}

static void draw_quest_list(void) {
    draw_header("Quests");
    draw_panel(6, 20, 136, 126, 5);
    for (int q = 0; q < QUEST_COUNT; q++) draw_quest_row(q);
    draw_panel(146, 20, 88, 126, 5);
    draw_quest_side();
    draw_footer(0, "Journal", "Back");
}

// moving the selection only redraws the two rows involved and the side panel
static void quest_list_move(int old) {
    for (int k = 0; k < 2; k++) {
        int q = k ? q_sel : old, y = 23 + q * 9;
        cv_fill(&cv_text, 9, y, 131, 10, 0);
        cv_fill(&cv_panel, 9, y, 131, 10, PC_FILL);
        draw_quest_row(q);
    }
    cv_fill(&cv_text, 148, 22, 84, 122, 0);
    cv_fill(&cv_panel, 148, 22, 84, 122, PC_FILL);
    draw_quest_side();
}

// journal lines are built into a buffer, then drawn with scrolling
#define JL_MAX 40
static EWRAM_BSS char jl_text[JL_MAX][48];
static u8 jl_col[JL_MAX], jl_strike[JL_MAX], jl_n;

static void jl_add(const char *s, int col, int strike, int indent) {
    char lines[10][48];
    int n = text_wrap(s, 210 - indent, lines, 10);
    for (int i = 0; i < n && jl_n < JL_MAX; i++, jl_n++) {
        char *d = jl_text[jl_n];
        int k = 0;
        for (int sp = 0; sp < indent / 3; sp++) d[k++] = ' ';
        for (const char *c = lines[i]; *c && k < 47; c++) d[k++] = *c;
        d[k] = 0;
        jl_col[jl_n] = col;
        jl_strike[jl_n] = strike;
    }
}

static void build_journal(int q) {
    const QuestDef *d = &quest_defs[q];
    int st = quest_stage(q), done = quest_done(q);
    jl_n = 0;
    if (!st) {
        jl_add(d->journal[0], TC_WHITE, 0, 0);
        if (d->req_qp) {
            char *p = sadd(sbuf, "To start this quest I need ");
            p = sadd_num(p, d->req_qp);
            sadd(p, " quest points.");
            jl_add(sbuf, quest_points() >= d->req_qp ? TC_GREEN : TC_RED, 0, 0);
        }
        return;
    }
    int last = done ? d->done : st + 1;
    for (int s = 1; s < last && s < d->done; s++) {
        int cur = !done && s == st;
        jl_add(d->journal[s], cur ? TC_WHITE : TC_DIM, !cur, 0);
        if (cur && d->nneeds && d->need_stage == s) {
            for (int i = 0; i < d->nneeds; i++) {
                const QNeed *nd = &d->needs[i];
                char *p = sbuf;
                int have = inv_count(nd->item), given = 0;
                if (nd->flag == 0xFE) {
                    p = sadd(p, items[nd->item].name);
                    p = sadd(p, ": ");
                    p = sadd_num(p, pl.qvar[q] > nd->qty ? nd->qty : pl.qvar[q]);
                    p = sadd(p, "/");
                    p = sadd_num(p, nd->qty);
                    sadd(p, " given");
                    given = pl.qvar[q] >= nd->qty;
                } else {
                    given = nd->flag != 0xFF && ((pl.qvar[q] >> nd->flag) & 1);
                    if (nd->qty > 1) {
                        p = sadd_num(p, nd->qty);
                        p = sadd(p, " x ");
                    }
                    p = sadd(p, items[nd->item].name);
                    if (!given) {
                        p = sadd(p, " (have ");
                        p = sadd_num(p, have);
                        sadd(p, ")");
                    }
                }
                int ok = given || (nd->flag != 0xFE && have >= nd->qty);
                jl_add(sbuf, given ? TC_DIM : ok ? TC_GREEN : TC_ORANGE, given, 9);
            }
        }
    }
    if (done) jl_add("QUEST COMPLETE!", TC_GOLD, 0, 0);
}

#define JL_VIS 11
static void draw_journal(void) {
    const QuestDef *d = &quest_defs[q_sel];
    draw_header("Journal");
    draw_panel(6, 20, 228, 126, 5);
    text_draw(14, 24, d->name, TC_GOLD);
    text_right(226, 24, quest_done(q_sel) ? "Complete" : quest_stage(q_sel) ? "In progress" : "Not started", quest_colour(q_sel));
    draw_rule(12, 35, 216);
    for (int i = 0; i < JL_VIS && q_scroll + i < jl_n; i++) {
        int k = q_scroll + i, y = 38 + i * 10;
        text_draw(14, y, jl_text[k], jl_col[k]);
        if (jl_strike[k]) {
            int w = text_width(jl_text[k]);
            int x0 = 14;
            for (const char *c = jl_text[k]; *c == ' '; c++) x0 += font_width[0] + 1, w -= font_width[0] + 1;
            cv_fill(&cv_text, x0, y + 4, w, 1, TC_GREY);
        }
    }
    if (jl_n > JL_VIS) {
        cv_fill(&cv_panel, 229, 38, 2, 106, PC_BAR_BG);
        cv_fill(&cv_panel, 229, 38 + q_scroll * 106 / jl_n, 2, JL_VIS * 106 / jl_n, PC_EDGE);
    }
    draw_footer(jl_n > JL_VIS ? "Up/Down scroll" : 0, 0, "Back");
}

static void draw_quests(void) {
    if (q_view) draw_journal();
    else draw_quest_list();
}

static void quests_input(void) {
    if (q_view) {
        int max = jl_n > JL_VIS ? jl_n - JL_VIS : 0;
        if ((keys_repeat & KEY_UP) && q_scroll > 0) q_scroll--, redraw = 1, sfx(SFX_MOVE);
        if ((keys_repeat & KEY_DOWN) && q_scroll < max) q_scroll++, redraw = 1, sfx(SFX_MOVE);
        if (keys_pressed & (KEY_B | KEY_A)) {
            q_view = 0;
            cursor_hide();
            sfx(SFX_BACK);
            redraw = 1;
        }
        return;
    }
    int s = q_sel;
    if (keys_repeat & KEY_UP) s = (s + QUEST_COUNT - 1) % QUEST_COUNT;
    if (keys_repeat & KEY_DOWN) s = (s + 1) % QUEST_COUNT;
    if (s != q_sel) {
        int old = q_sel;
        q_sel = s;
        sfx(SFX_MOVE);
        quest_list_move(old);
    }
    if (keys_pressed & KEY_A) {
        build_journal(q_sel);
        q_view = 1;
        q_scroll = jl_n > JL_VIS ? jl_n - JL_VIS : 0;   // show the current step
        cursor_hide();
        sfx(SFX_OK);
        redraw = 1;
    }
    if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
}

static void menu_redraw(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    show_portrait = 0;
    switch (page) {
    case P_MAIN: draw_main(); break;
    case P_ITEMS: draw_items(); break;
    case P_EQUIP: draw_equip(); break;
    case P_SKILLS: draw_skills(); break;
    case P_QUESTS: draw_quests(); break;
    case P_COMBAT: draw_combat(); break;
    case P_SYSTEM: draw_system(); break;
    }
    if (pop.open) pop_draw();
}

static void set_page(int p) {
    page = p;
    pop.open = 0;
    eq_choose = 0;
    q_view = 0;
    sys_note = 0;
    slide = 10;
    cursor_hide();
    redraw = 1;
}

void menu_open(void) {
    game_state = GS_MENU;
    anim = 1;
    page = P_MAIN;
    pop.open = 0;
    slide = 12;
    cursor_hide();
    sfx(SFX_OK);
    menu_redraw();
    gfx_set_ui_alpha(0, 1);
}

static void menu_close(void) {
    anim = -1;
    sfx(SFX_BACK);
}

static void menu_close_now(void) {
    anim = 0;
    pop.open = 0;
    gfx_set_world_fade(16);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(12, 0);
    show_portrait = 0;
    icons_clear();
    cursor_hide();
    gfx_set_player_palette();
    game_state = GS_WORLD;
    ui_hud_reset();
}

static void item_action(int slot, int act) {
    Slot *s = &pl.inv[slot];
    int id = s->id;
    const ItemDef *d = &items[id];
    switch (act) {
    case 0: {  // primary
        if (d->flags & IF_EAT) {
            int max = skill_level(SK_HITPOINTS);
            pl.hp += d->heal;
            if (pl.hp > max) pl.hp = max;
            s->id = 0;
            s->qty = 0;
            char *p = sadd(sbuf, "You eat the ");
            char *start = p;
            p = sadd(p, d->name);
            sadd(p, ".");
            for (char *q = start; *q; q++)
                if (*q >= 'A' && *q <= 'Z') *q += 32;
            msg(sbuf);
            sfx(SFX_EAT);
        } else if (d->flags & IF_BURY) {
            s->id = 0;
            s->qty = 0;
            msg("You dig a hole in the ground... You bury the bones.");
            add_xp(SK_PRAYER, 45);
            sfx(SFX_PICKUP);
        } else if (d->flags & IF_LIGHT) {
            player_start_firemaking(slot);
            menu_close();
        } else if (d->flags & IF_EQUIP) {
            int need = d->slot == EQ_WEAPON ? skill_level(SK_ATTACK) : skill_level(SK_DEFENCE);
            if (need < d->req) {
                char *p = sadd(sbuf, d->slot == EQ_WEAPON ? "You need Attack level " : "You need Defence level ");
                p = sadd_num(p, d->req);
                sadd(p, ".");
                msg_col(sbuf, TC_RED);
                sfx(SFX_ERR);
                break;
            }
            int old = pl.equip[d->slot];
            pl.equip[d->slot] = id;
            s->id = old;
            s->qty = old ? 1 : 0;
            gfx_set_player_palette();
            sfx(SFX_OK);
        }
        break;
    }
    case 3:  // quest use: close the menu at once and run the item's script
        menu_close_now();
        quest_item(slot);
        break;
    case 1:  // drop
        drop_item(pl.x, pl.y, id, s->qty);
        s->id = 0;
        s->qty = 0;
        sfx(SFX_PICKUP);
        break;
    }
}

static void items_input(void) {
    if (pop.open) {
        int r = pop_input();
        if (r == -3) redraw = 1;
        if (r >= 0) item_action(item_sel, r);
        if (game_state != GS_MENU) return;
        if (r != -1) redraw = 1;
        return;
    }
    int c = item_sel % 7, rr = item_sel / 7;
    if (keys_repeat & KEY_LEFT) c = (c + 6) % 7;
    if (keys_repeat & KEY_RIGHT) c = (c + 1) % 7;
    if (keys_repeat & KEY_UP) rr = (rr + 3) % 4;
    if (keys_repeat & KEY_DOWN) rr = (rr + 1) % 4;
    if (rr * 7 + c != item_sel) {
        item_sel = rr * 7 + c;
        sfx(SFX_MOVE);
        redraw = 1;
    }
    if (keys_pressed & KEY_A) {
        Slot *s = &pl.inv[item_sel];
        if (!s->id) return;
        const ItemDef *d = &items[s->id];
        pop_reset(GRID_X + (item_sel % 7) * 20 + 22, GRID_Y + (item_sel / 7) * 22);
        if (quest_item_usable(s->id)) pop_add(s->id == I_SPADE ? "Dig" : (s->id == I_MESSAGE || s->id == I_PIRATE_MSG || s->id == I_NOTES) ? "Read" : "Use", 3);
        if (d->flags & IF_EAT) pop_add("Eat", 0);
        else if (d->flags & IF_BURY) pop_add("Bury", 0);
        else if (d->flags & IF_LIGHT) pop_add("Light", 0);
        else if (d->flags & IF_EQUIP) pop_add(d->slot == EQ_WEAPON ? "Wield" : "Wear", 0);
        pop_add("Drop", 1);
        pop_add("Cancel", 2);
        sfx(SFX_OK);
        redraw = 1;
    }
    if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
}

static void equip_input(void) {
    if (!eq_choose) {
        int s = eq_sel;
        if (keys_repeat & KEY_UP) s = (s + EQ_COUNT - 1) % EQ_COUNT;
        if (keys_repeat & KEY_DOWN) s = (s + 1) % EQ_COUNT;
        if (s != eq_sel) {
            eq_sel = s;
            sfx(SFX_MOVE);
            redraw = 1;
        }
        if (keys_pressed & KEY_A) {
            build_candidates();
            eq_choose = 1;
            eq_cand_sel = 0;
            sfx(SFX_OK);
            redraw = 1;
        }
        if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
        return;
    }
    int n = cand_n + (pl.equip[eq_sel] ? 1 : 0);
    if (n > 5) n = 5;
    if (n && (keys_repeat & KEY_UP)) {
        eq_cand_sel = (eq_cand_sel + n - 1) % n;
        sfx(SFX_MOVE);
        redraw = 1;
    }
    if (n && (keys_repeat & KEY_DOWN)) {
        eq_cand_sel = (eq_cand_sel + 1) % n;
        sfx(SFX_MOVE);
        redraw = 1;
    }
    if ((keys_pressed & KEY_A) && n) {
        if (eq_cand_sel < cand_n) {
            item_action(cand[eq_cand_sel], 0);
        } else if (!inv_free()) {
            msg("You don't have enough inventory space.");
            sfx(SFX_ERR);
        } else {
            inv_add(pl.equip[eq_sel], 1);
            pl.equip[eq_sel] = 0;
            gfx_set_player_palette();
            sfx(SFX_OK);
        }
        eq_choose = 0;
        cursor_hide();
        redraw = 1;
    }
    if (keys_pressed & KEY_B) {
        eq_choose = 0;
        cursor_hide();
        sfx(SFX_BACK);
        redraw = 1;
    }
}

void menu_update(void) {
    if (anim > 0) {
        if (anim <= 10) {
            gfx_set_world_fade(16 - anim * 9 / 10);
            gfx_set_mosaic(anim >= 6 ? 1 : 0);
            gfx_set_ui_alpha(anim * 13 / 10, 1);
            anim++;
        } else {
            gfx_set_ui_alpha(13, 0);
            anim = 0;
        }
    } else if (anim < 0) {
        int t = -anim;
        gfx_set_world_fade(7 + t * 9 / 6);
        gfx_set_mosaic(t < 3 ? 1 : 0);
        gfx_set_ui_alpha(13 - t * 13 / 6, 1);
        if (++t > 6) {
            anim = 0;
            gfx_set_world_fade(16);
            gfx_set_mosaic(0);
            gfx_set_ui_alpha(12, 0);
            show_portrait = 0;
            icons_clear();
            cursor_hide();
            gfx_set_player_palette();
            game_state = GS_WORLD;
            ui_hud_reset();
            return;
        }
        anim = -t;
        return;
    }
    if (keys_pressed & KEY_START) {
        menu_close();
        return;
    }
    switch (page) {
    case P_MAIN: {
        int s = main_sel;
        if (keys_repeat & KEY_UP) s = (s + MAIN_N - 1) % MAIN_N;
        if (keys_repeat & KEY_DOWN) s = (s + 1) % MAIN_N;
        if (s != main_sel) {
            main_sel = s;
            sfx(SFX_MOVE);
            redraw = 1;
        }
        if (keys_pressed & KEY_A) {
            sfx(SFX_OK);
            set_page(P_ITEMS + main_sel);
        }
        if (keys_pressed & KEY_B) menu_close();
        break;
    }
    case P_ITEMS: items_input(); break;
    case P_EQUIP: equip_input(); break;
    case P_QUESTS: quests_input(); break;
    case P_SKILLS: {
        int c = sk_sel / 5, r = sk_sel % 5;
        if (keys_repeat & (KEY_LEFT | KEY_RIGHT)) c ^= 1;
        if (keys_repeat & KEY_UP) r = (r + 4) % 5;
        if (keys_repeat & KEY_DOWN) r = (r + 1) % 5;
        if (c * 5 + r != sk_sel) {
            sk_sel = c * 5 + r;
            sfx(SFX_MOVE);
            redraw = 1;
        }
        if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
        break;
    }
    case P_COMBAT: {
        int s = cb_sel;
        if (keys_repeat & KEY_UP) s = (s + 3) % 4;
        if (keys_repeat & KEY_DOWN) s = (s + 1) % 4;
        if (s != cb_sel) {
            cb_sel = s;
            sfx(SFX_MOVE);
            redraw = 1;
        }
        if (keys_pressed & KEY_A) {
            if (cb_sel < 3) pl.style = cb_sel;
            else pl.autoret ^= 1;
            sfx(SFX_OK);
            redraw = 1;
        }
        if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
        break;
    }
    case P_SYSTEM: {
        int s = sys_sel;
        if (keys_repeat & KEY_UP) s = (s + 2) % 3;
        if (keys_repeat & KEY_DOWN) s = (s + 1) % 3;
        if (s != sys_sel) {
            sys_sel = s;
            sfx(SFX_MOVE);
            redraw = 1;
        }
        if (keys_pressed & KEY_A) {
            if (sys_sel == 0) {
                save_game();
                sys_note = "Game saved.";
                sfx(SFX_LEVEL);
            } else if (sys_sel == 1) {
                pl.music ^= 1;
                music_enable(pl.music);
                sfx(SFX_OK);
            } else {
                menu_close();
                return;
            }
            redraw = 1;
        }
        if (keys_pressed & KEY_B) set_page(P_MAIN), sfx(SFX_BACK);
        break;
    }
    }
    if (game_state != GS_MENU || anim < 0) return;
    if (redraw) {
        redraw = 0;
        extern volatile u32 snd_play_frame;
        u32 vb = snd_play_frame;
        int vc = REG_VCOUNT;
        menu_redraw();
        // debug: scanlines a page redraw took (VBlank, which bumps snd_play_frame, starts at line 160)
        int vc2 = REG_VCOUNT;
        redraw_lines = (snd_play_frame - vb) * 228 + (vc2 >= 160 ? vc2 - 160 : vc2 + 68) - (vc >= 160 ? vc - 160 : vc + 68);
    }
    // refresh the play clock once a second
    if (pl.playtime % 60 == 0) draw_clock();
}

// ---------------------------------------------------------------------------------------
// Dialogue: a frosted box with a name tag and a 2x portrait of whoever speaks (NPC on the
// left, the player on the right, none for narration), typewriter text, and an option list
// for choices. Quest scripts (quests.c) feed it pages and choices; plain townsfolk use the
// page list in their NpcDef.
static const char *const *dlg_pages;
static int dlg_page, dlg_chars, dlg_kind;
static char dlg_lines[4][48];
static int dlg_nlines, dlg_total, dlg_tx;
static u8 dlg_script, dlg_choice, dlg_sel;
static DlgEvent dlg_ev;
static s16 dlg_portrait = -1;           // NPC kind, SPK_PLAYER, or -1
static s16 dlg_px, dlg_py;

#define DLG_Y 104
// the first page clears the whole screen (HUD); later pages only the dialogue area
static void dialog_clear(void) {
    if (game_state == GS_DIALOG) {
        cv_clear_rows(&cv_text, 8, 19);
        cv_clear_rows(&cv_panel, 8, 19);
    } else {
        cv_clear(&cv_text);
        cv_clear(&cv_panel);
    }
}

static s16 dlg_last_speaker = -1;      // speaker of the page on screen, -1 if none

static void dialog_layout_say(int speaker, const char *text, int prebroken) {
    cursor_hide();
    if (game_state == GS_DIALOG && !dlg_choice && speaker == dlg_last_speaker) {
        // same speaker as the page on screen: keep the box, only replace the text
        cv_clear_rows(&cv_text, 14, 18);
        goto text;
    }
    dialog_clear();
    icons_clear();
    dlg_choice = 0;
    dlg_last_speaker = speaker;
    draw_panel(6, DLG_Y, 228, 50, 6);
    const char *name = 0;
    dlg_portrait = -1;
    dlg_tx = 16;
    if (speaker == SPK_PLAYER) {
        name = "Adventurer";
        draw_cell(194, DLG_Y + 6, 36, 38, 0);
        dlg_portrait = SPK_PLAYER;
        dlg_px = 196;
        dlg_py = DLG_Y + 9;
    } else if (speaker != SPK_NONE) {
        name = npc_defs[speaker].name;
        draw_cell(10, DLG_Y + 6, 36, 38, 0);
        dlg_portrait = speaker;
        dlg_px = 12;
        dlg_py = DLG_Y + 9;
        dlg_tx = 54;
    } else {
        dlg_tx = 34;
    }
    if (name) {
        int w = text_spaced_width(name, 1) + 16;
        int x = speaker == SPK_PLAYER ? 226 - w : 14;
        draw_panel(x, DLG_Y - 9, w, 14, 3);
        text_spaced(x + 8, DLG_Y - 7, name, TC_GOLD, 1);
    }
text:
    if (prebroken) {
        // quest pages are wrapped at build time, lines separated by '\n'
        dlg_nlines = 0;
        while (*text && dlg_nlines < 3) {
            int k = 0;
            while (*text && *text != '\n' && k < 47) dlg_lines[dlg_nlines][k++] = *text++;
            dlg_lines[dlg_nlines++][k] = 0;
            if (*text == '\n') text++;
        }
    } else {
        dlg_nlines = text_wrap(text, 172, dlg_lines, 3);
    }
    dlg_total = 0;
    for (int i = 0; i < dlg_nlines; i++) {
        const char *p = dlg_lines[i];
        while (*p++) dlg_total++;
    }
    dlg_chars = 0;
    show_arrow = 0;
}

static void dialog_choice_row(int i, int y) {
    int yy = y + 20 + i * 12;
    if (i == dlg_sel) {
        draw_highlight(10, yy - 1, 220, 12);
        cursor_to(4, yy + 1);
    }
    text_draw(20, yy, dlg_ev.opt[i], i == dlg_sel ? TC_WHITE : TC_GREY);
}

static int dialog_choice_y(void) { return 154 - (dlg_ev.nopt * 12 + 22); }

static void dialog_layout_choice(void) {
    dialog_clear();
    icons_clear();
    dlg_choice = 1;
    dlg_last_speaker = -1;
    dlg_portrait = -1;
    show_arrow = 0;
    int n = dlg_ev.nopt;
    int y = dialog_choice_y();
    draw_panel(6, y, 228, n * 12 + 22, 6);
    text_spaced(16, y + 5, "SELECT AN OPTION", TC_CYAN, 2);
    draw_rule(14, y + 16, 212);
    dlg_choice = 2;   // the option rows follow next frame (keeps each frame's work small)
}

// moving the selection only redraws the two rows involved
static void dialog_choice_move(int old) {
    int y = dialog_choice_y();
    for (int k = 0; k < 2; k++) {
        int i = k ? dlg_sel : old, yy = y + 19 + i * 12;
        cv_fill(&cv_text, 8, yy, 224, 12, 0);
        cv_fill(&cv_panel, 8, yy, 224, 12, PC_FILL);
        dialog_choice_row(i, y);
    }
}

static void dialog_enter(void) {
    if (game_state == GS_DIALOG) return;
    game_state = GS_DIALOG;
    player_stop();
    gfx_set_ui_alpha(13, 0);
    sfx(SFX_OK);
}

static void dialog_close(void) {
    dlg_last_speaker = -1;
    show_arrow = 0;
    dlg_portrait = -1;
    dlg_choice = 0;
    cursor_hide();
    icons_clear();
    game_state = GS_WORLD;
    gfx_set_ui_alpha(12, 0);
    ui_hud_reset();
}

// next page/choice of the running quest script; closes the dialogue when it ends
static void dialog_vm_next(void) {
    if (qvm_step(&dlg_ev)) {
        int first = game_state != GS_DIALOG;
        if (first) {
            dlg_last_speaker = -1;
            cv_clear(&cv_text);
            cv_clear(&cv_panel);
        }
        dialog_enter();
        if (dlg_ev.type == DLG_SAY) {
            dialog_layout_say(dlg_ev.speaker, dlg_ev.text, 1);
        } else {
            dlg_sel = 0;
            dialog_layout_choice();
        }
        return;
    }
    dlg_script = 0;
    int after = qvm_after();
    if (after == 1) shop_open();
    else if (after == 2) bank_open();
    else if (game_state == GS_DIALOG) dialog_close();
}

void dialog_run_script(void) {
    dlg_script = 1;
    dialog_vm_next();
}

void dialog_open(int kind, int index) {
    (void)index;
    dlg_script = 0;
    dlg_kind = kind;
    dlg_pages = npc_defs[kind].dialog;
    dlg_page = 0;
    if (!dlg_pages) return;
    dlg_last_speaker = -1;
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    dialog_enter();
    dialog_layout_say(kind, dlg_pages[0], 0);
}

static void dialog_draw_chars(int from, int to) {
    int k = 0;
    for (int l = 0; l < dlg_nlines; l++) {
        int x = dlg_tx;
        for (const char *p = dlg_lines[l]; *p; p++, k++) {
            char c[2] = {*p, 0};
            if (k >= from && k < to) text_draw(x, DLG_Y + 9 + l * 12, c, TC_WHITE);
            x += font_width[(u8)*p - 32] + 1;
        }
    }
}

void dialog_update(void) {
    if (dlg_choice == 2) {
        for (int i = 0; i < dlg_ev.nopt; i++) dialog_choice_row(i, dialog_choice_y());
        dlg_choice = 1;
        return;
    }
    if (dlg_choice) {
        int n = dlg_ev.nopt;
        if (keys_repeat & (KEY_UP | KEY_DOWN)) {
            int old = dlg_sel;
            dlg_sel = (dlg_sel + ((keys_repeat & KEY_UP) ? n - 1 : 1)) % n;
            sfx(SFX_MOVE);
            dialog_choice_move(old);
        }
        if (keys_pressed & KEY_A) {
            sfx(SFX_OK);
            cursor_hide();
            qvm_choose(dlg_sel);
            dialog_vm_next();
        }
        return;
    }
    if (dlg_chars < dlg_total) {
        int step = (keys & KEY_A) ? 4 : 1;
        int to = dlg_chars + step;
        if (to > dlg_total) to = dlg_total;
        dialog_draw_chars(dlg_chars, to);
        if ((frame & 3) == 0) sfx(SFX_MOVE);
        dlg_chars = to;
        if (keys_pressed & KEY_B) {
            dialog_draw_chars(dlg_chars, dlg_total);
            dlg_chars = dlg_total;
        }
        return;
    }
    show_arrow = 1;
    if (keys_pressed & (KEY_A | KEY_B)) {
        sfx(SFX_OK);
        if (dlg_script) {
            dialog_vm_next();
            return;
        }
        if (dlg_pages[dlg_page + 1] && !(keys_pressed & KEY_B)) {
            dlg_page++;
            dialog_layout_say(dlg_kind, dlg_pages[dlg_page], 0);
            return;
        }
        const NpcDef *d = &npc_defs[dlg_kind];
        if (d->flags & NF_BANK) {
            show_arrow = 0;
            dlg_portrait = -1;
            bank_open();
        } else if (d->flags & NF_SHOP) {
            show_arrow = 0;
            dlg_portrait = -1;
            shop_open();
        } else {
            dialog_close();
        }
    }
}

static void draw_dialog_portrait(void) {
    if (game_state != GS_DIALOG || dlg_portrait < 0) return;
    if (dlg_portrait == SPK_PLAYER) {
        avatar_draw_portrait(dlg_px, dlg_py, 1);
        return;
    }
    const NpcDef *d = &npc_defs[dlg_portrait];
    int t = npc_portrait_tile(dlg_portrait);
    if (d->sprite == SPR_KIND_DEMON) obj_sprite(dlg_px, dlg_py, t, d->pal, 1, 0, 0, OBJF_32);
    else obj_sprite(dlg_px, dlg_py, t, d->pal, 1, 0, 1, OBJF_AFFINE2X | (d->sprite == SPR_KIND_GHOST ? OBJF_SEMI : 0));
}

// ---------------------------------------------------------------------------------------
// Bank & shop share a two-tab grid screen
static u8 tab, store_sel, store_scroll, inv_sel2, store_is_bank;
static const char *store_note;

typedef struct { u16 id, qty; } StockItem;
static const u16 shop_stock[] = {
    I_TINDERBOX, I_NET, I_ROD, I_FEATHER, I_BRONZE_AXE, I_IRON_AXE, I_STEEL_AXE, I_BRONZE_PICK,
    I_IRON_PICK, I_BRONZE_SWORD, I_IRON_SWORD, I_WOODEN_SHIELD, I_BRONZE_HELM, I_LEATHER_BODY,
    I_LEATHER_CHAPS, I_BOOTS, I_SHRIMP, I_COOKED_MEAT, I_STEEL_PICK, I_BRONZE_DAGGER, I_IRON_MACE,
    I_BRONZE_FULL_HELM, I_BRONZE_CHAINBODY, I_IRON_BOOTS, I_BUCKET, I_POT, I_SHEARS, I_EYE_NEWT,
};
#define SHOP_N (int)(sizeof(shop_stock) / sizeof(shop_stock[0]))
static Slot shop_slots[28];

static int sell_price(int id) { return items[id].value * 2 / 5; }

static void store_draw(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    draw_header(store_is_bank ? "Bank" : "General Store");
    // tabs
    const char *t0 = store_is_bank ? "BANK" : "STOCK";
    int x = 120;
    for (int i = 0; i < 2; i++) {
        const char *t = i == 0 ? t0 : "INVENTORY";
        int w = text_spaced_width(t, 1) + 12;
        if (i == tab) {
            cv_fill(&cv_panel, x, 19, w, 1, PC_EDGE);
            cv_fill(&cv_panel, x, 20, w, 9, PC_HL3);
        }
        text_spaced(x + 6, 20, t, i == tab ? TC_WHITE : TC_DIM, 1);
        x += w + 4;
    }
    draw_button(98, 20, 'L');
    draw_button(x, 20, 'R');
    // grid
    draw_panel(6, 31, 152, 91, 5);
    const Slot *slots;
    int count, sel, first = 0;
    if (tab == 0) {
        slots = store_is_bank ? pl.bank : shop_slots;
        count = store_is_bank ? 96 : SHOP_N;
        sel = store_sel;
        first = store_scroll * 7;
    } else {
        slots = pl.inv;
        count = 28;
        sel = inv_sel2;
    }
    // grid shifted down by 6 px vs. the items page
    for (int k = 0; k < 28; k++) {
        int i = first + k;
        int cx = GRID_X + (k % 7) * 20, cy = 36 + (k / 7) * 21;
        draw_cell(cx, cy, 19, 20, i == sel);
        if (i >= count || !slots[i].id) continue;
        icon_at(cx + 2, cy + 2, items[slots[i].id].icon);
        if ((tab == 0 && store_is_bank) || slots[i].qty > 1 || (items[slots[i].id].flags & IF_STACK)) {
            if (!(tab == 0 && !store_is_bank)) {
                char n[12];
                fmt_int(n, slots[i].qty);
                text_draw(cx + 1, cy, n, TC_YELLOW);
            }
        }
    }
    if (sel >= first && sel < first + 28) {
        int k = sel - first;
        cursor_to(GRID_X + (k % 7) * 20 - 7, 36 + (k / 7) * 21 + 6);
    }
    if (tab == 0 && store_is_bank) {
        // scrollbar
        int rows = 14, vis = 4;
        cv_fill(&cv_panel, 153, 36, 2, 82, PC_BAR_BG);
        cv_fill(&cv_panel, 153, 36 + store_scroll * 82 / rows, 2, vis * 82 / rows, PC_EDGE);
    }
    draw_panel(162, 31, 72, 91, 5);
    const Slot *s = (sel < count) ? &slots[sel] : 0;
    int id = s ? s->id : 0;
    int pm = store_is_bank ? 0 : (tab == 0 ? 1 : 2);
    int price = !id ? 0 : pm == 1 ? items[id].value : sell_price(id);
    draw_item_detail(id, (tab == 0 && !store_is_bank) ? 1 : (s ? s->qty : 0), 162, 31, 72, pm, price);
    draw_panel(6, 125, 228, 21, 4);
    char *p = sadd(sbuf, "Coins: ");
    p = sadd_num(p, inv_count(I_COINS));
    text_draw(14, 131, store_note ? store_note : sbuf, store_note ? TC_CYAN : TC_GOLD);
    if (store_is_bank) {
        draw_button(146, 131, 'S');
        text_draw(166, 131, "Deposit all", TC_GREY);
    }
    const char *a = store_is_bank ? (tab == 0 ? "Withdraw" : "Deposit") : (tab == 0 ? "Buy" : "Sell");
    draw_footer("L/R switch tabs", a, "Leave");
    if (pop.open) pop_draw();
}

static void store_open_common(int bank) {
    store_is_bank = bank;
    tab = 0;
    store_sel = store_scroll = inv_sel2 = 0;
    store_note = 0;
    pop.open = 0;
    game_state = bank ? GS_BANK : GS_SHOP;
    player_stop();
    cursor_hide();
    gfx_set_world_fade(7);
    gfx_set_mosaic(1);
    gfx_set_ui_alpha(13, 0);
    sfx(SFX_OK);
    store_draw();
}

void bank_open(void) { store_open_common(1); }
void shop_open(void) {
    for (int i = 0; i < 28; i++) {
        shop_slots[i].id = i < SHOP_N ? shop_stock[i] : 0;
        shop_slots[i].qty = shop_slots[i].id == I_FEATHER ? 10 : 1;
    }
    store_open_common(0);
}

static void store_leave(void) {
    sfx(SFX_BACK);
    game_state = GS_WORLD;
    pop.open = 0;
    cursor_hide();
    icons_clear();
    gfx_set_world_fade(16);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(12, 0);
    ui_hud_reset();
}

static void store_transact(int amount) {
    if (store_is_bank) {
        if (tab == 0) {
            Slot *b = &pl.bank[store_sel];
            if (!b->id) return;
            int n = amount > b->qty ? b->qty : amount;
            int added = inv_add(b->id, n);
            if (!added) {
                store_note = "Your inventory is full.";
                sfx(SFX_ERR);
                return;
            }
            b->qty -= added;
            if (!b->qty) b->id = 0;
        } else {
            Slot *s = &pl.inv[inv_sel2];
            if (!s->id) return;
            int id = s->id;
            int n = inv_count(id);
            if (n > amount) n = amount;
            if (bank_add(id, n)) inv_remove(id, n);
            else store_note = "Your bank is full.";
        }
        sfx(SFX_PICKUP);
    } else {
        if (tab == 0) {
            int id = shop_slots[store_sel].id;
            if (!id) return;
            for (int k = 0; k < amount; k++) {
                int qty = id == I_FEATHER ? 10 : 1;
                int cost = items[id].value * qty;
                if (inv_count(I_COINS) < cost) {
                    store_note = "You don't have enough coins.";
                    sfx(SFX_ERR);
                    return;
                }
                if (!inv_free() && !((items[id].flags & IF_STACK) && inv_count(id))) {
                    store_note = "You don't have enough inventory space.";
                    sfx(SFX_ERR);
                    return;
                }
                inv_remove(I_COINS, cost);
                inv_add(id, qty);
            }
            sfx(SFX_COINS);
        } else {
            Slot *s = &pl.inv[inv_sel2];
            int id = s->id;
            if (!id || id == I_COINS) return;
            int n = inv_count(id);
            if (n > amount) n = amount;
            int gain = sell_price(id) * n;
            inv_remove(id, n);
            if (gain) inv_add(I_COINS, gain);
            sfx(SFX_COINS);
        }
    }
}

static void store_update(void) {
    if (pop.open) {
        int r = pop_input();
        if (r >= 0 && r < 100) store_transact(r);
        if (r != -1) store_draw();
        return;
    }
    int changed = 0;
    if (keys_pressed & (KEY_L | KEY_R)) {
        tab ^= 1;
        store_note = 0;
        sfx(SFX_MOVE);
        cursor_hide();
        changed = 1;
    }
    int count = tab == 0 ? (store_is_bank ? 96 : SHOP_N) : 28;
    int sel = tab == 0 ? store_sel : inv_sel2;
    int c = sel % 7, r = sel / 7;
    int rows = (count + 6) / 7;
    if (keys_repeat & KEY_LEFT) c = (c + 6) % 7;
    if (keys_repeat & KEY_RIGHT) c = (c + 1) % 7;
    if (keys_repeat & KEY_UP) r = (r + rows - 1) % rows;
    if (keys_repeat & KEY_DOWN) r = (r + 1) % rows;
    int ns = r * 7 + c;
    if (ns >= count) ns = count - 1;
    if (ns != sel) {
        sfx(SFX_MOVE);
        changed = 1;
        store_note = 0;
        if (tab == 0) {
            store_sel = ns;
            if (store_is_bank) {
                if (ns / 7 < store_scroll) store_scroll = ns / 7;
                if (ns / 7 >= store_scroll + 4) store_scroll = ns / 7 - 3;
            }
        } else {
            inv_sel2 = ns;
        }
    }
    if ((keys_pressed & KEY_SELECT) && store_is_bank) {
        for (int i = 0; i < 28; i++)
            if (pl.inv[i].id && bank_add(pl.inv[i].id, pl.inv[i].qty)) pl.inv[i].id = pl.inv[i].qty = 0;
        store_note = "You deposit your inventory.";
        sfx(SFX_PICKUP);
        changed = 1;
    }
    if (keys_pressed & KEY_A) {
        const Slot *s = tab == 0 ? (store_is_bank ? &pl.bank[store_sel] : &shop_slots[store_sel]) : &pl.inv[inv_sel2];
        if (s->id && !(tab == 1 && !store_is_bank && s->id == I_COINS)) {
            int k = tab == 0 ? store_sel : inv_sel2;
            pop_reset(GRID_X + (k % 7) * 20 + 22, 36 + ((k - (tab == 0 ? store_scroll * 7 : 0)) / 7) * 21);
            if (store_is_bank) {
                pop_add(tab == 0 ? "Withdraw 1" : "Deposit 1", 1);
                pop_add(tab == 0 ? "Withdraw 5" : "Deposit 5", 5);
                pop_add(tab == 0 ? "Withdraw All" : "Deposit All", 99);
            } else {
                pop_add(tab == 0 ? "Buy 1" : "Sell 1", 1);
                pop_add(tab == 0 ? "Buy 5" : "Sell 5", 5);
                pop_add(tab == 0 ? "Buy 10" : "Sell All", tab == 0 ? 10 : 99);
            }
            pop_add("Cancel", 200);
            sfx(SFX_OK);
            changed = 1;
        }
    }
    if (keys_pressed & (KEY_B | KEY_START)) {
        store_leave();
        return;
    }
    if (changed) store_draw();
}

void bank_update(void) { store_update(); }
void shop_update(void) { store_update(); }

// ---------------------------------------------------------------------------------------
// Title screen
static u8 title_sel, title_phase;
static s16 title_t;

static void title_draw(void) {
    cv_clear(&cv_text);
    cv_clear(&cv_panel);
    icons_clear();
    text_big(120, 22, "RuneScape");
    draw_rule(48, 46, 144);
    text_spaced(120 - text_spaced_width("2007  DEMAKE", 3) / 2, 51, "2007  DEMAKE", TC_CYAN, 3);
    text_center(120, 63, "Game Boy Advance Edition", TC_GREY);
    draw_panel(72, 86, 96, 44, 6);
    int has = save_exists();
    static const char *const opts[2] = {"New Game", "Continue"};
    for (int i = 0; i < 2; i++) {
        int y = 94 + i * 16;
        if (i == title_sel) {
            draw_highlight(75, y - 3, 90, 14);
            cursor_to(67, y);
        }
        int col = (i == 1 && !has) ? TC_DIM : i == title_sel ? TC_WHITE : TC_GREY;
        text_draw(88, y, opts[i], col);
    }
    text_center(120, 146, "A fan-made tribute. Not affiliated with Jagex.", TC_DIM);
}

void title_open(void) {
    game_state = GS_TITLE;
    music_play(MUS_TITLE);
    title_sel = save_exists() ? 1 : 0;
    title_phase = 0;
    title_t = 0;
    cursor_hide();
    gfx_set_world_fade(6);
    gfx_set_mosaic(0);
    gfx_set_ui_alpha(13, 0);
    title_draw();
}

void title_update(void) {
    // slow camera drift over Lumbridge
    title_t++;
    cam_x = START_X * 16 - 280 + (title_t >> 2) % 400;
    cam_y = START_Y * 16 - 120 + ((title_t >> 3) % 120);
    if (title_phase) {
        if (++title_phase > 12) {
            game_state = GS_WORLD;
            gfx_set_world_fade(16);
            gfx_set_ui_alpha(12, 0);
            ui_hud_reset();
            msg_col("Welcome to RuneScape.", TC_GOLD);
            if (title_sel == 0) msg("Talk to the Lumbridge Guide east of the castle for tips.");
            music_enable(pl.music);
        } else {
            gfx_set_world_fade(title_phase > 6 ? 6 + (title_phase - 6) * 10 / 6 : 6 - title_phase);
            gfx_set_ui_alpha(13 - title_phase, 1);
        }
        return;
    }
    int s = title_sel;
    if (keys_repeat & (KEY_UP | KEY_DOWN)) s ^= 1;
    if (s == 1 && !save_exists()) s = 0;
    if (s != title_sel) {
        title_sel = s;
        sfx(SFX_MOVE);
        title_draw();
    }
    if (keys_pressed & (KEY_A | KEY_START)) {
        sfx(SFX_LEVEL);
        world_init();
        if (title_sel == 1 && load_game()) {
            world_after_load();
        } else {
            world_new_game();
        }
        cursor_hide();
        icons_clear();
        title_phase = 1;
        // snap camera to the player for the fade in (without treating the A press as an
        // interaction with whatever the loaded player faces)
        keys_pressed = 0;
        world_update();
    }
}

// ---------------------------------------------------------------------------------------
void ui_init(void) {
    cv_init();
    gfx_set_ui_alpha(12, 0);
}

// what the UI is waiting for (read by test scripts): 1 dialogue page shown, 2 choice box,
// 3 quest complete scroll, 4 level-up popup, 5 free in the world
u8 ui_wait;

void ui_draw_sprites(void) {
    ui_wait = game_state == GS_DIALOG ? (dlg_choice == 1 ? 2 : dlg_choice ? 0 : dlg_chars >= dlg_total ? 1 : 0)
            : game_state == GS_QUESTDONE ? 3 : game_state == GS_LEVELUP ? 4 : game_state == GS_WORLD ? 5 : 0;
    // smooth cursor glide with a little horizontal bob
    if (cursor_tx > -50) {
        cursor_x += (cursor_tx - cursor_x + (cursor_tx > cursor_x ? 1 : 0)) / 2;
        cursor_y += (cursor_ty - cursor_y + (cursor_ty > cursor_y ? 1 : 0)) / 2;
        int bob = ((frame >> 3) & 3) == 1 ? 1 : ((frame >> 3) & 3) == 3 ? -1 : 0;
        obj_sprite(cursor_x + bob + slide, cursor_y, SPR_CURSOR, 6, 0, 0, 0, OBJF_8x8);
    }
    if (game_state == GS_WORLD)
        for (int i = 0; i < 3; i++)
            if (drop_icon_on[i] && drops[i].timer) obj_icon(drop_icon_x[i], drop_icon_y[i], skill_icons[drops[i].sk], 1);
    if (show_arrow) obj_sprite(220, 142 + ((frame >> 4) & 1), SPR_ARROWD, 6, 0, 0, 0, OBJF_8x8);
    for (int i = 0; i < ui_icon_n; i++) {
        int x = ui_icons[i].x + slide, y = ui_icons[i].y;
        if (pop.open && x + 16 > pop_rx && x < pop_rx + pop_rw && y + 16 > pop_ry && y < pop_ry + pop_rh) continue;
        obj_icon(x, y, ui_icons[i].icon, 1);
    }
    if (show_portrait) avatar_draw_portrait(portrait_x + slide, portrait_y, 1);   // 2x composited player
    draw_dialog_portrait();
    gfx_set_hl_pulse((frame >> 1) & 63);
    if (slide) {
        slide = slide * 3 / 4;
        if (slide < 2) slide = 0;
    }
    gfx_set_ui_scroll(-slide);
}
