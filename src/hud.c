// HUD: game messages, XP drops, the area name banner, HP orb, combat target, level-up
// popups and the quest complete scroll.
#include "ui_internal.h"

typedef struct { char text[96]; u8 col; u16 timer; } Msg;
static Msg msgs[2];
typedef struct { u8 sk, timer; u16 amount; } XpDrop;
static XpDrop drops[3];
static int area_region = -1, area_timer;
static s16 target_npc = -1, cached_target = -1;
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

// OSRS hitpoints orb: the number beside a round orb that drains from the top as you lose
// health (red liquid on black), with the heart icon drawn over it (see ui_draw_sprites)
static s16 orb_icon = -1;
static void hud_draw_status(void) {
    hud_clear_rect(176, 128, 64, 32);
    int max = skill_level(SK_HITPOINTS);
    int cx = 222, cy = 143, r = 11;
    int level = (2 * r + 1) * (max - pl.hp) / (max ? max : 1);   // drained rows from the top
    for (int dy = -r - 1; dy <= r + 1; dy++)
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > (r + 1) * (r + 1)) continue;
            int col = d2 > r * r ? PC_EDGE_DIM : d2 > (r - 1) * (r - 1) ? PC_EDGE : dy + r < level ? PC_EDGE_DIM : PC_RED;
            cv_pset(&cv_panel, cx + dx, cy + dy, col);
        }
    char *p = sbuf;
    sadd_num(p, pl.hp);
    int pct = max ? pl.hp * 100 / max : 0;
    text_right(cx - r - 3, cy - 4, sbuf, pct > 66 ? TC_GREEN : pct > 33 ? TC_YELLOW : TC_RED);
    orb_icon = 1;
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
        int x = text_right(234, y, sbuf, drops[i].timer < 20 ? TC_DIM : TC_WHITE);
        drop_icon_on[i] = drops[i].timer >= 20;
        drop_icon_x[i] = x - text_width(sbuf) - 18;
        drop_icon_y[i] = y - 4;
        row++;
    }
}

static void hud_draw_area(void) {
    hud_clear_rect(0, 0, 128, 22);
    if (!area_timer) return;
    const char *name = regions[area_region].name;
    draw_panel(2, 3, text_width(name) + 12, 15, 0);
    text_draw(8, 6, name, TC_ORANGE);
}

// the combat target, or the NPC picked with L: name and level (and health for monsters)
static int shown_target(void) { return lock_target >= 0 ? lock_target : target_npc; }

static void hud_draw_target(void) {
    hud_clear_rect(128, 0, 112, 26);
    int t = shown_target();
    if (t < 0 || !npcs[t].active) return;
    Npc *n = &npcs[t];
    const NpcDef *d = &npc_defs[n->kind];
    int fight = d->flags & NF_ATTACK;
    draw_panel(130, 3, 106, fight ? 22 : 13, 0);
    text_draw(136, 5, d->name, lock_target >= 0 ? TC_YELLOW : TC_WHITE);
    if (!fight) return;
    char *p = sadd(sbuf, "Lv ");
    sadd_num(p, d->level);
    text_right(230, 5, sbuf, TC_GOLD);
    draw_bar(135, 17, 96, n->hp, d->hp, PC_RED, PC_RED);
}

// forget messages, XP drops and the combat target (logging out)
void hud_clear_state(void) {
    for (int i = 0; i < 2; i++) msgs[i].timer = 0;
    for (int i = 0; i < 3; i++) drops[i].timer = 0;
    target_npc = -1;
    lock_target = -1;
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
        } else if (pl.action != ACT_COMBAT && !n->in_combat && !n->hpbar_timer) {
            target_npc = -1;
            hud_dirty_target = 1;
        }
    }
    int t = shown_target();
    if (t != cached_target) {
        cached_target = t;
        hud_dirty_target = 1;
    }
    if (t >= 0 && npcs[t].hp != cached_target_hp) {
        cached_target_hp = npcs[t].hp;
        hud_dirty_target = 1;
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
        // OSRS: the chatbox turns into "Congratulations, you just advanced a X level."
        draw_parchment(4, 104, 232, 54);
        const char *nm = skill_names[l->sk];
        int an = nm[0] == 'A' || nm[0] == 'E' || nm[0] == 'I' || nm[0] == 'O' || nm[0] == 'U';
        char *p = sadd(sbuf, an ? "Congratulations, you just advanced an " : "Congratulations, you just advanced a ");
        p = sadd(p, nm);
        sadd(p, " level.");
        char lines[2][48];
        int n = text_wrap(sbuf, 180, lines, 2);
        for (int i = 0; i < n; i++) text_center_ns(132, 110 + i * 11, lines[i], TC_DKBLUE);
        p = sadd(sbuf, "Your ");
        p = sadd(p, nm);
        p = sadd(p, " level is now ");
        p = sadd_num(p, l->lvl);
        sadd(p, ".");
        text_center_ns(132, 112 + n * 11, sbuf, TC_BLACK);
        text_center_ns(132, 145, "Press A to continue", TC_BLUE);
        icons_clear();
        icon_at(14, 118, skill_icons[l->sk]);
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
    // OSRS quest scroll: parchment, "Congratulations!", the quest's icon, the rewards
    int rows = 1 + d->nrewards;
    int h = 70 + rows * 10;
    int y0 = (160 - h) / 2;
    draw_parchment(18, y0, 204, h);
    cv_fill(&cv_panel, 18, y0, 204, 3, PC_PARCH_D);          // rolled ends of the scroll
    cv_fill(&cv_panel, 18, y0 + h - 3, 204, 3, PC_PARCH_D);
    text_center_ns(120, y0 + 6, "Congratulations!", TC_DKRED);
    char *p = sadd(sbuf, "You have completed ");
    sadd(p, d->name);
    char lines[2][48];
    int nl = text_wrap(sbuf, 188, lines, 2);
    for (int i = 0; i < nl; i++) text_center_ns(120, y0 + 18 + i * 10, lines[i], TC_BLACK);
    int yb = y0 + 22 + nl * 10;
    icon_at(30, yb + 6, items[d->icon].icon);
    text_draw_ns(60, yb, "You are awarded:", TC_DKRED);
    int y = yb + 11;
    p = sadd_num(sbuf, d->qp);
    sadd(p, d->qp == 1 ? " Quest Point" : " Quest Points");
    text_draw_ns(64, y, sbuf, TC_BLACK);
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
        text_draw_ns(64, y, sbuf, TC_BLACK);
    }
    p = sadd(sbuf, "Quest Points: ");
    sadd_num(p, quest_points());
    text_draw_ns(28, y0 + h - 14, sbuf, TC_DKRED);
    text_draw_ns(212 - text_width("Press A"), y0 + h - 14, "Press A", TC_BLUE);
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

// XP drop skill icons and the HP orb icon (world only)
void hud_draw_sprites(void) {
    for (int i = 0; i < 3; i++)
        if (drop_icon_on[i] && drops[i].timer) obj_icon(drop_icon_x[i], drop_icon_y[i], skill_icons[drops[i].sk], 1);
    if (orb_icon > 0) obj_icon(214, 135, skill_icons[SK_HITPOINTS], 1);
}
