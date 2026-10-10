// Targeting: L picks the nearest NPC on screen (and cycles outwards on each press), B lets
// go, and A opens an OSRS right-click style menu for the target (Attack, Talk-to, Examine).
// The target stays selected when the player walks off, so a bow or a spell can be used on it
// from a distance. The HUD shows its name and level; world_draw.c marks it.
#include "ui_internal.h"

s16 lock_target = -1;

void target_set(int npc) {
    lock_target = npc;
    ui_set_target(npc);
}

static int cheb(int i) {
    int dx = npcs[i].x - pl.x, dy = npcs[i].y - pl.y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

// NPCs on screen in the player's zone, nearest first; the one after the current target
void target_cycle(void) {
    s16 cand[32];
    int n = 0, zone = world_zone(pl.x, pl.y);
    for (int i = 0; i < MAX_NPCS && n < 32; i++) {
        Npc *np = &npcs[i];
        if (!np->active) continue;
        int sx = np->px - cam_x, sy = np->py - cam_y;
        if (sx < -8 || sx > SCREEN_W - 8 || sy < -8 || sy > SCREEN_H - 8) continue;
        if (world_zone(np->x, np->y) != zone) continue;
        const NpcDef *d = &npc_defs[np->kind];
        if (!(d->flags & NF_ATTACK) && !npc_talkable(i)) continue;
        // insertion sort by distance (stable, so ties keep spawn order)
        int k = n++, di = cheb(i);
        while (k > 0 && cheb(cand[k - 1]) > di) {
            cand[k] = cand[k - 1];
            k--;
        }
        cand[k] = i;
    }
    if (!n) {
        sfx(SFX_ERR);
        return;
    }
    int next = 0;
    for (int k = 0; k < n; k++)
        if (cand[k] == lock_target) next = (k + 1) % n;
    target_set(cand[next]);
    sfx(SFX_MOVE);
}

// ---------------------------------------------------------------------------------------
// The menu: "Attack Goblin (level-2)", "Talk-to Hans", "Examine ...", "Cancel"
enum { TM_ATTACK, TM_TALK, TM_EXAMINE, TM_CANCEL };
static char opt_buf[3][40];

static void build_opt(char *dst, const char *verb, int npc, int lvl) {
    const NpcDef *d = &npc_defs[npcs[npc].kind];
    char *p = sadd(dst, verb);
    p = sadd(p, " ");
    p = sadd(p, d->name);
    if (lvl) {
        p = sadd(p, " (level-");
        p = sadd_num(p, d->level);
        sadd(p, ")");
    }
}

static void tm_draw(void) {
    pop_draw();
}

void target_menu_open(void) {
    int i = lock_target;
    const NpcDef *d = &npc_defs[npcs[i].kind];
    pop_reset(0, 0);
    if (d->flags & NF_ATTACK) {
        build_opt(opt_buf[0], "Attack", i, 1);
        pop_add(opt_buf[0], TM_ATTACK);
    }
    if (npc_talkable(i)) {
        build_opt(opt_buf[1], "Talk-to", i, 0);
        pop_add(opt_buf[1], TM_TALK);
    }
    build_opt(opt_buf[2], "Examine", i, 0);
    pop_add(opt_buf[2], TM_EXAMINE);
    pop_add("Cancel", TM_CANCEL);
    // beside the NPC, on whichever side keeps it in view (width as pop_draw works it out)
    int w = text_width("Choose Option");
    for (int k = 0; k < pop.n; k++)
        if (text_width(pop.opts[k]) > w) w = text_width(pop.opts[k]);
    w += 14;
    int sx = npcs[i].px - cam_x, sy = npcs[i].py - cam_y;
    int x = sx + 8 < SCREEN_W / 2 ? sx + 20 : sx - w - 4, y = sy - 8;
    pop.x = x < 4 ? 4 : x;
    pop.y = y < 4 ? 4 : y;
    game_state = GS_TARGET;
    sfx(SFX_OK);
    tm_draw();
}

static void tm_close(void) {
    pop.open = 0;
    cursor_hide();
    game_state = GS_WORLD;
    ui_hud_reset();
}

void target_menu_update(void) {
    int i = lock_target;
    if (i < 0 || !npcs[i].active) {
        tm_close();
        return;
    }
    int r = pop_input();
    if (r == -3) tm_draw();
    if (r == -1 || r == -3) return;
    tm_close();
    switch (r) {
    case TM_ATTACK: player_walk_to(i, ACT_COMBAT); break;
    case TM_TALK: player_walk_to(i, ACT_TALK); break;
    case TM_EXAMINE: {
        const char *e = npc_examine(npcs[i].kind);
        if (e) {
            msg(e);
        } else {
            char *p = sadd(sbuf, "It's ");
            p = sadd(p, npc_defs[npcs[i].kind].name);
            sadd(p, ".");
            msg(sbuf);
        }
        break;
    }
    }
}
