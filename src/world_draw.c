// World sprites: player, NPCs, ground items, fires and hitsplats, depth sorted.
#include "world_internal.h"

typedef struct { s16 y; s16 who; } SortEnt;   // who: npc index (can exceed 127), -1 = player

static void lunge_offset(int dir, int lunge, int *ox, int *oy) {
    int amt = lunge > 4 ? 8 - lunge : lunge;
    if (amt > 3) amt = 3;
    *ox = DX[dir] * amt;
    *oy = SDY[dir] * amt;
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
    if (home_cast) {
        act = AVA_HOME;
        t = home_cast;
    } else if (busy) switch (pl.action) {
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
        case ACT_SMITH:
            act = AVA_MINE;                // hammering, a blow every other tick
            tool = I_HAMMER;
            t = (pl.action_timer & 1) * TICK_FRAMES + tick_frames;
            break;
        case ACT_FISH:
            act = fish_spots[pl.target].kind ? AVA_ROD : AVA_NET;
            tool = act == AVA_ROD ? I_ROD : I_NET;
            t = since_tick;
            break;
        case ACT_COMBAT: {
            Npc *n = &npcs[pl.target];
            int cb = combat_type();
            if (n->active && cb != CB_MELEE && npc_in_reach(pl.target)) {
                // bow drawn / staff raised, released just after each shot
                int sp = weapon_speed(), cd = pl.attack_cd > sp ? sp : pl.attack_cd;
                act = cb == CB_RANGED ? AVA_SHOOT : AVA_CAST;
                t = (sp - cd) * TICK_FRAMES + tick_frames;
            } else if (n->active && adjacent(pl.x, pl.y, n->x, n->y)) {
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
    int fx = game_state == GS_WORLD || game_state == GS_DIALOG || game_state == GS_TITLE || game_state == GS_TARGET;
    // hitsplats on top
    for (int i = 0; i < MAX_SPLATS && fx; i++)
        if (splats[i].timer) obj_sprite(splats[i].wx - cam_x, splats[i].wy - cam_y - (splats[i].timer > 34 ? 40 - splats[i].timer : 6) + 4, DYN_TILE_BASE + i * 4, 6, 1, 0, 1, 0);

    // arrows and spells in flight, then the target marker (a bobbing chevron over the NPC)
    for (int i = 0; i < MAX_PROJ && fx; i++) {
        const Proj *p = &projs[i];
        if (!p->active) continue;
        const Npc *n = &npcs[p->npc];
        int tx = n->px, ty = n->py - 4;
        int x = p->sx + (tx - p->sx) * p->t / p->dur, y = p->sy + (ty - p->sy) * p->t / p->dur;
        int icon = p->icon, hf = 0, vf = 0;
        if (icon == ICON_PROJ_ARROW_H) {
            // one of three arrow drawings, mirrored into eight directions
            int dx = tx - p->sx, dy = ty - p->sy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (ay * 2 < ax) icon = ICON_PROJ_ARROW_H, hf = dx < 0;
            else if (ax * 2 < ay) icon = ICON_PROJ_ARROW_V, vf = dy > 0;
            else icon = ICON_PROJ_ARROW_D, hf = dx < 0, vf = dy > 0;
        }
        obj_icon_flip(x - cam_x, y - cam_y, icon, 1, hf, vf);
    }
    // home teleport: blue light rising around the player once the book is closed
    if (home_cast >= 14 * TICK_FRAMES && fx) {
        int cx = pl.px - cam_x, cy = pl.py - cam_y + 13, fast = home_cast >= 20 * TICK_FRAMES;
        for (int k = 0; k < (fast ? 8 : 4); k++) {
            int span = fast ? 24 : 40, rise = (home_cast * (fast ? 2 : 1) + k * span / 2) % span;
            int x = cx - 12 + (k & 3) * 8 + ((k >> 2) ? 4 : 0) + (((home_cast >> 3) + k) & 1);
            obj_icon(x - 4, cy - 12 - rise, ICON_HT_SPARK, 1);
        }
    }
    if (lock_target >= 0 && fx && npcs[lock_target].active) {
        const Npc *n = &npcs[lock_target];
        int bob = (frame >> 3) & 3;
        obj_icon(n->px - cam_x, n->py - cam_y - 22 + (bob == 3 ? 1 : bob), ICON_TARGET_MARK, 1);
    }

    SortEnt ents[MAX_NPCS + 1];
    int ne = 0;
    ents[ne].y = pl.py;
    ents[ne++].who = -1;
    int plane = pl.plane;   // floors share pixel space: draw the player's
    for (int i = 0; i < npc_hi; i++) {
        Npc *n = &npcs[i];
        if (!n->active || n->plane != plane) continue;
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
        if (n->active && n->hpbar_timer && fx && n->plane == pl.plane) draw_hpbar(n->px - cam_x, n->py - cam_y, n->hp, npc_defs[n->kind].hp);
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
    // home teleport: the rune circle on the ground, traced a quarter at a time, under the player
    if (home_cast && fx && (home_cast < 20 * TICK_FRAMES || (frame & 4))) {
        int cx = pl.px - cam_x + 8, cy = pl.py - cam_y + 13;
        int q = home_cast / 45;   // quarters traced so far
        obj_icon_flip(cx - 16, cy - 16, ICON_HT_RING, 2, 0, 0);
        if (q >= 1) obj_icon_flip(cx, cy - 16, ICON_HT_RING, 2, 1, 0);
        if (q >= 2) obj_icon_flip(cx, cy, ICON_HT_RING, 2, 1, 1);
        if (q >= 3) obj_icon_flip(cx - 16, cy, ICON_HT_RING, 2, 0, 1);
    }
    if (!fx) return;
    for (int i = 0; i < MAX_FIRES; i++)
        if (fires[i].active && fires[i].plane == pl.plane) obj_sprite(TILE_PX(fires[i].x) - cam_x, TILE_PY(fires[i].y) - cam_y, SPR_FIRE0 + ((frame >> 3) % 3) * 4, 6, 2, (frame >> 4) & 1, 1, OBJF_MOSAIC);
    for (int i = 0; i < FISHSPOT_COUNT; i++)
        if (fish_spots[i].plane == pl.plane) obj_sprite(TILE_PX(fish_spots[i].x) - cam_x, TILE_PY(fish_spots[i].y) - cam_y, SPR_SPOT0 + ((frame / 12 + i) % 3) * 4, 6, 2, 0, 1, OBJF_MOSAIC);
    for (int i = 0; i < MAX_GROUND; i++)
        if (ground[i].id && ground[i].plane == pl.plane) obj_icon(TILE_PX(ground[i].x) - cam_x, TILE_PY(ground[i].y) - cam_y, items[ground[i].id].icon, 2);
}
