// UI core: shared text helpers, the UI sprite list (icons, cursor, portrait, slide), common
// menu chrome (header, footer, clock), popup menus, the item grid and item detail panel, and
// the per-frame UI sprite pass. Screens live in hud.c, menu.c, dialog.c, store.c, title.c.
// Styled after Old School RuneScape's interface: stone windows with orange titles and yellow
// values, a parchment chatbox, OSRS level-up messages and quest scrolls, and the login screen.
#include "ui_internal.h"

char sbuf[96];

char *sadd(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}
char *sadd_num(char *p, u32 v) {
    char n[16];
    fmt_num(n, v);
    return sadd(p, n);
}
// centred text without the drop shadow (dark text on parchment)
void text_center_ns(int xc, int y, const char *s, int col) { text_draw_ns(xc - text_width(s) / 2, y, s, col); }

// ---------------------------------------------------------------------------------------
// UI sprite list (icons, cursor, portrait) rebuilt by page redraws, emitted every frame
UiIcon ui_icons[40];
int ui_icon_n;
s16 cursor_x = -100, cursor_y, cursor_tx = -100, cursor_ty;
u8 show_portrait;
s16 portrait_x, portrait_y;
u8 show_arrow;
s16 slide;   // UI horizontal slide offset (pixels)

void icons_clear(void) { ui_icon_n = 0; }
void icon_at(int x, int y, int icon) {
    if (ui_icon_n < 40) {
        ui_icons[ui_icon_n].x = x;
        ui_icons[ui_icon_n].y = y;
        ui_icons[ui_icon_n++].icon = icon;
    }
}
void cursor_to(int x, int y) {
    cursor_tx = x;
    cursor_ty = y;
    if (cursor_x < -50) {
        cursor_x = x;
        cursor_y = y;
    }
}
void cursor_hide(void) { cursor_x = cursor_tx = -100; }

// ---------------------------------------------------------------------------------------
// Common menu chrome
void draw_clock(void) {
    cv_fill(&cv_text, 168, 2, 72, 12, 0);
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
    text_right(232, 4, sbuf, TC_YELLOW);
}

// OSRS window title: orange, centred over an engraved rule, on a stone strip
void draw_header(const char *title) {
    draw_panel(4, 1, 232, 16, 0);
    int w = text_width(title);
    text_draw(w > 92 ? 10 : 120 - w / 2, 4, title, TC_ORANGE);   // long shop names start left of the clock
    draw_clock();
}

void draw_footer(const char *desc, const char *a_label, const char *b_label) {
    draw_panel(4, 147, 232, 13, 0);
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
Popup pop;

s16 pop_rx, pop_ry, pop_rw, pop_rh;

void pop_reset(int x, int y) {
    pop.open = 1;
    pop.n = 0;
    pop.sel = 0;
    pop.x = x;
    pop.y = y;
}
void pop_add(const char *s, int id) {
    pop.opts[pop.n] = s;
    pop.ids[pop.n++] = id;
}
void pop_draw(void) {
    // OSRS right-click menu: "Choose Option" on a black title strip, options in white,
    // the hovered one picked out in yellow
    int w = text_width("Choose Option");
    for (int i = 0; i < pop.n; i++) {
        int tw = text_width(pop.opts[i]);
        if (tw > w) w = tw;
    }
    w += 14;
    int h = pop.n * 12 + 18;
    int x = pop.x, y = pop.y;
    if (x + w > 236) x = 236 - w;
    if (y + h > 146) y = 146 - h;
    cv_fill(&cv_text, x, y, w, h, 0);
    draw_panel(x, y, w, h, 0);
    cv_fill(&cv_panel, x + 1, y + 1, w - 2, 12, PC_EDGE_DIM);
    text_draw_ns(x + 4, y + 2, "Choose Option", TC_DIM);
    pop_rx = x;
    pop_ry = y;
    pop_rw = w;
    pop_rh = h;
    for (int i = 0; i < pop.n; i++) {
        int yy = y + 15 + i * 12;
        if (i == pop.sel) {
            draw_highlight(x + 2, yy, w - 4, 12);
            cursor_to(x - 6, yy + 2);
        }
        text_draw(x + 5, yy + 1, pop.opts[i], i == pop.sel ? TC_YELLOW : TC_WHITE);
    }
}
// returns chosen id, -2 when cancelled, -1 while open
int pop_input(void) {
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
void draw_grid(const Slot *slots, int count, int first, int sel) {
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

void draw_item_detail(int id, int qty, int x, int y, int w, int price_mode, int price) {
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
        static const char *const lab[6] = {"Attack", "Strength", "Defence", "Ranged", "Rng str", "Magic"};
        int v[6] = {d->atk, d->str, d->def, d->ratk, d->rstr, d->matk};
        for (int i = 0; i < 6; i++) {
            if (!v[i]) continue;
            text_draw(x + 6, yy, lab[i], TC_GREY);
            char *p = sbuf;
            if (v[i] > 0) p = sadd(p, "+");
            fmt_int(p, v[i]);
            text_right(x + w - 6, yy, sbuf, v[i] > 0 ? TC_GREEN : TC_RED);
            yy += 10;
        }
        if (d->req > 1) {
            int sk = (d->flags & IF_RANGED) ? SK_RANGED : d->slot == EQ_WEAPON ? SK_ATTACK : SK_DEFENCE;
            text_draw(x + 6, yy, sk == SK_RANGED ? "Req Rng" : sk == SK_ATTACK ? "Req Att" : "Req Def", TC_GREY);
            fmt_int(sbuf, d->req);
            int have = skill_level(sk);
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
void ui_init(void) {
    cv_init();
    gfx_set_ui_alpha(12, 0);
}

// what the UI is waiting for (read by test scripts): 1 dialogue page shown, 2 choice box,
// 3 quest complete scroll, 4 level-up popup, 5 free in the world, 6 target menu, 7 furnace / anvil screen
u8 ui_wait;

void ui_draw_sprites(void) {
    ui_wait = game_state == GS_DIALOG ? dialog_wait_state()
            : game_state == GS_QUESTDONE ? 3 : game_state == GS_LEVELUP ? 4 : game_state == GS_WORLD ? 5 : game_state == GS_TARGET ? 6
            : game_state == GS_SMITH ? 7 : 0;
    // (no pointer sprite: like OSRS, the hovered row itself shows the selection)
    if (game_state == GS_WORLD) hud_draw_sprites();
    for (int i = 0; i < ui_icon_n; i++) {
        int x = ui_icons[i].x + slide, y = ui_icons[i].y;
        if (pop.open && x + 16 > pop_rx && x < pop_rx + pop_rw && y + 16 > pop_ry && y < pop_ry + pop_rh) continue;
        obj_icon(x, y, ui_icons[i].icon, 1);
    }
    if (show_portrait) avatar_draw_portrait(portrait_x + slide, portrait_y, 1);   // 2x composited player
    draw_dialog_portrait();
    title_flames();
    gfx_set_hl_pulse((frame >> 1) & 63);
    if (slide) {
        slide = slide * 3 / 4;
        if (slide < 2) slide = 0;
    }
    gfx_set_ui_scroll(-slide);
}
