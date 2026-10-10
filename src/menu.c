// START menu: inventory, worn equipment, skills, quest list and journal, combat options
// and settings.
#include "ui_internal.h"

enum { P_MAIN, P_ITEMS, P_EQUIP, P_SKILLS, P_QUESTS, P_COMBAT, P_SYSTEM };
#define MAIN_N 6
static u8 page, main_sel, item_sel, eq_sel, eq_choose, eq_cand_sel, sk_sel, cb_sel, sys_sel;
static u8 q_sel, q_view, q_scroll;
static u8 cand[28], cand_n;
static s8 anim;          // >0 opening, <0 closing
static u8 redraw;
static const char *sys_note;
u16 redraw_lines;

static const char *const main_items[MAIN_N] = {"Inventory", "Worn Equipment", "Skills", "Quest List", "Combat Options", "Settings"};
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
    text_draw(144, 27, player_name, TC_WHITE);
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
    draw_header("Inventory");
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

static const char *const slot_names[EQ_COUNT] = {"Head", "Weapon", "Body", "Shield", "Legs", "Feet", "Neck"};

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
    draw_header("Worn Equipment");
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
        text_spaced(138, 25, slot_names[eq_sel], TC_CYAN, 1);
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
        draw_bar(13, 133, 214, xp - cur, next - cur, PC_GOLD, PC_GOLD);
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
    draw_header("Combat Options");
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
    text_spaced(14, 72, "Attack style", TC_CYAN, 1);
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
    static const char *const opts[4] = {"Save game", "Music", "Logout", "Close menu"};
    draw_header("Settings");
    draw_panel(6, 22, 140, 84, 6);
    for (int i = 0; i < 4; i++) {
        int y = 31 + i * 18;
        if (i == sys_sel) {
            draw_highlight(9, y - 3, 134, 15);
            cursor_to(1, y);
        }
        text_draw(16, y, opts[i], i == sys_sel ? TC_WHITE : TC_GREY);
        if (i == 1) text_right(136, y, pl.music ? "ON" : "OFF", pl.music ? TC_GREEN : TC_RED);
    }
    draw_panel(150, 22, 84, 66, 6);
    text_spaced(158, 27, "Save data", TC_CYAN, 1);
    draw_rule(156, 38, 72);
    char *sp = sadd(sbuf, "Slot ");
    sadd_num(sp, save_slot() + 1);
    text_draw(158, 44, sbuf, TC_GREY);
    if (sys_note) {
        char lines[2][48];
        int n = text_wrap(sys_note, 70, lines, 2);
        for (int i = 0; i < n; i++) text_draw(158, 58 + i * 10, lines[i], TC_GREEN);
    }
    draw_footer(sys_sel == 2 ? "Saves, then logs out." : "Saved to cartridge SRAM.", "Select", "Back");
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
    text_spaced(153, 25, "Quest points", TC_CYAN, 1);
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
    draw_header("Quest List");
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
    // OSRS journal: a parchment scroll, current step in dark blue, finished ones struck through
    draw_parchment(6, 20, 228, 126);
    text_draw_ns(14, 24, d->name, TC_DKRED);
    const char *st = quest_done(q_sel) ? "Complete" : quest_stage(q_sel) ? "In progress" : "Not started";
    text_draw_ns(226 - text_width(st), 24, st, TC_BLACK);
    cv_fill(&cv_panel, 12, 35, 216, 1, PC_PARCH_D);
    for (int i = 0; i < JL_VIS && q_scroll + i < jl_n; i++) {
        int k = q_scroll + i, y = 38 + i * 10;
        int c = jl_col[k];
        c = c == TC_WHITE ? TC_DKBLUE : c == TC_GREEN ? TC_BLACK : c == TC_DIM ? TC_BLACK : TC_DKRED;
        text_draw_ns(14, y, jl_text[k], c);
        if (jl_strike[k]) {
            int w = text_width(jl_text[k]);
            int x0 = 14;
            for (const char *c = jl_text[k]; *c == ' '; c++) x0 += font_width[0] + 1, w -= font_width[0] + 1;
            cv_fill(&cv_text, x0, y + 4, w, 1, TC_BLACK);
        }
    }
    if (jl_n > JL_VIS) {
        cv_fill(&cv_panel, 229, 38, 2, 106, PC_PARCH_D);
        cv_fill(&cv_panel, 229, 38 + q_scroll * 106 / jl_n, 2, JL_VIS * 106 / jl_n, PC_EDGE_DIM);
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
        if (keys_repeat & KEY_UP) s = (s + 3) % 4;
        if (keys_repeat & KEY_DOWN) s = (s + 1) % 4;
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
            } else if (sys_sel == 2) {
                // OSRS logout: progress is kept, back to the login screen
                save_game();
                menu_close_now();
                ui_hud_reset();
                title_open();
                return;
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
