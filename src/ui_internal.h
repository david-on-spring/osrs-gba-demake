// Private interface shared by the UI modules (ui.c core, hud.c, menu.c, dialog.c, store.c,
// title.c). The rest of the game talks to the UI through game.h only.
#pragma once
#include "game.h"

// ui.c: text helpers (sbuf is a shared scratch line; build into it, draw, discard)
extern char sbuf[96];
char *sadd(char *p, const char *s);
char *sadd_num(char *p, u32 v);
void text_center_ns(int xc, int y, const char *s, int col);   // centred, no shadow (on parchment)

// ui.c: UI sprite list (icons, cursor, portrait) rebuilt by page redraws, emitted every frame
typedef struct { s16 x, y; u16 icon; } UiIcon;
extern UiIcon ui_icons[40];
extern int ui_icon_n;
extern s16 cursor_x, cursor_y, cursor_tx, cursor_ty;
extern u8 show_portrait;
extern s16 portrait_x, portrait_y;
extern u8 show_arrow;
extern s16 slide;                       // UI horizontal slide offset (pixels), eases to 0
void icons_clear(void);
void icon_at(int x, int y, int icon);
void cursor_to(int x, int y);
void cursor_hide(void);

// ui.c: common menu chrome
void draw_clock(void);
void draw_header(const char *title);
void draw_footer(const char *desc, const char *a_label, const char *b_label);

// ui.c: popup (context menu)
typedef struct {
    u8 open, n, sel;
    s16 x, y;
    const char *opts[6];
    u8 ids[6];
} Popup;
extern Popup pop;
extern s16 pop_rx, pop_ry, pop_rw, pop_rh;
void pop_reset(int x, int y);
void pop_add(const char *s, int id);
void pop_draw(void);
int pop_input(void);                    // chosen id; -1 idle, -2 cancelled (B), -3 moved

// ui.c: item grid (inventory, bank, shop)
#define GRID_X 12
#define GRID_Y 26
void draw_grid(const Slot *slots, int count, int first, int sel);
void draw_item_detail(int id, int qty, int x, int y, int w, int price_mode, int price);

// hud.c
void hud_clear_state(void);             // forget messages, XP drops and the combat target

// per-module hooks called from ui_draw_sprites()
void hud_draw_sprites(void);
int dialog_wait_state(void);
void draw_dialog_portrait(void);
void title_flames(void);
