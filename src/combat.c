// Combat: hit rolls, single-way combat lock, player melee / ranged / magic attacks and their
// projectiles, NPC attacks, drops and deaths.
#include "world_internal.h"

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
u8 foe_timer;

int foe_free(int i) {
    return foe < 0 || foe == i || !foe_timer || !npcs[foe].active;
}
void foe_engage(int i) {
    foe = i;
    foe_timer = FOE_TICKS;
}
void foe_release(int i) {
    if (foe == i) foe = -1;
}

// ---------------------------------------------------------------------------------------
// Attack types. A bow shoots arrows from the ammo slot, knives are thrown from the weapon slot;
// a staff casts the autocast spell (or
// hits like a club when none is set); anything else is melee.
u8 cast_once;   // spell picked in the spellbook, cast once at the target

int combat_type(void) {
    int f = items[pl.equip[EQ_WEAPON]].flags;
    if (cast_once) return CB_MAGIC;
    if (f & IF_RANGED) return CB_RANGED;
    if ((f & IF_MAGIC) && pl.spell) return CB_MAGIC;
    return CB_MELEE;
}

int thrown_weapon(int id) { return (items[id].flags & (IF_RANGED | IF_STACK)) == (IF_RANGED | IF_STACK) && items[id].slot == EQ_WEAPON; }

static int is_longbow(int w) {
    return w == I_LONGBOW || w == I_OAK_LONGBOW || w == I_WILLOW_LONGBOW || w == I_MAPLE_LONGBOW;
}

int attack_range(void) {
    if (cast_once) return 10;
    switch (combat_type()) {
    case CB_RANGED: {
        int r = thrown_weapon(pl.equip[EQ_WEAPON]) ? 4 : is_longbow(pl.equip[EQ_WEAPON]) ? 9 : 7;
        if (pl.style == STYLE_LONGRANGE) r += 2;
        return r > 10 ? 10 : r;
    }
    case CB_MAGIC: return 10;
    default: return 1;
    }
}

// OSRS: bronze and iron arrows fit any bow, steel needs oak, mithril willow, adamant maple
int ammo_ok(int arrow, int bow) {
    int need = arrow == I_BRONZE_ARROW || arrow == I_IRON_ARROW ? 1 : arrow == I_STEEL_ARROW ? 2
             : arrow == I_MITHRIL_ARROW ? 3 : arrow == I_ADAMANT_ARROW ? 4 : 99;
    int have = bow == I_SHORTBOW || bow == I_LONGBOW ? 1 : bow == I_OAK_SHORTBOW || bow == I_OAK_LONGBOW ? 2
             : bow == I_WILLOW_SHORTBOW || bow == I_WILLOW_LONGBOW ? 3 : bow == I_MAPLE_SHORTBOW || bow == I_MAPLE_LONGBOW ? 4 : 0;
    return need <= have;
}

// ticks between swings for the wielded weapon (OSRS speeds), by weapon class
static const u8 class_speed[] = {
    [W_NONE] = 4, [W_DAGGER] = 4, [W_SWORD] = 4, [W_SCIM] = 4, [W_LONG] = 5, [W_2H] = 7, [W_AXE] = 5,
    [W_BATTLEAXE] = 6, [W_MACE] = 4, [W_WARHAMMER] = 6, [W_CLAWS] = 4, [W_PICK] = 5,
};
int weapon_speed(void) {
    int cb = combat_type();   // (a manual cast also takes five ticks)
    if (cb == CB_MAGIC) return 5;
    if (cb == CB_RANGED) return (thrown_weapon(pl.equip[EQ_WEAPON]) ? 3 : is_longbow(pl.equip[EQ_WEAPON]) ? 6 : 4) - (pl.style == STYLE_RAPID);
    return class_speed[items[pl.equip[EQ_WEAPON]].wclass];   // unarmed and staves: 4
}

// the target can be hit from where the player stands: next to it for melee, else within range
// with nothing in the way
int npc_in_reach(int i) {
    Npc *n = &npcs[i];
    int r = attack_range();
    if (n->plane != pl.plane) return 0;
    if (r <= 1) return adjacent(pl.x, pl.y, n->x, n->y);
    int dx = n->x - pl.x, dy = n->y - pl.y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx > dy ? dx : dy) <= r && clear_shot(pl.x, pl.y, n->x, n->y, pl.plane);
}

// Confuse/Weaken/Curse lower a stat by 5%, Vulnerability/Enfeeble/Stun by 10%
static int cursed(const Npc *n, int base, int bit5, int bit10) {
    int pct = (n->curse & bit10) ? 10 : (n->curse & bit5) ? 5 : 0;
    return base * (100 - pct) / 100;
}

// a hit lands: hitsplat, XP for the attack type, maybe a kill
static void land_hit(int i, int dmg, int cb, int spell) {
    Npc *n = &npcs[i];
    if (!n->active) return;
    if (dmg > n->hp) dmg = n->hp;
    n->hpbar_timer = 200;
    splat_spawn(n->px, n->py, dmg);
    sfx(dmg ? SFX_HIT : SFX_MISS);
    if (cb == CB_MAGIC) add_xp(SK_MAGIC, spells[spell].xp + dmg * 20);   // a splash still earns the base XP
    if (!dmg) return;
    if (cb == CB_RANGED && pl.style == STYLE_LONGRANGE) {
        add_xp(SK_RANGED, dmg * 20);
        add_xp(SK_DEFENCE, dmg * 20);
    } else if (cb == CB_RANGED) {
        add_xp(SK_RANGED, dmg * 40);
    } else if (cb == CB_MELEE) {
        int sk = pl.style == STYLE_ACCURATE ? SK_ATTACK : pl.style == STYLE_AGGRESSIVE ? SK_STRENGTH : SK_DEFENCE;
        add_xp(sk, dmg * 40);
    }
    add_xp(SK_HITPOINTS, dmg * 40 / 3);
    n->hp -= dmg;
    if (!n->hp) npc_die(i);
}

// ---------------------------------------------------------------------------------------
// Projectiles: arrows and spells fly from the player to the target; the hit lands on arrival
Proj projs[MAX_PROJ];

static void proj_launch(int i, int dmg, int cb, int spell, int icon, int drop, int hit) {
    Proj *p = &projs[0];
    for (int k = 0; k < MAX_PROJ; k++)
        if (!projs[k].active) {
            p = &projs[k];
            break;
        }
    Npc *n = &npcs[i];
    int dx = n->x - pl.x, dy = n->y - pl.y;
    int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    p->active = 1;
    p->npc = i;
    p->dmg = dmg;
    p->cb = cb;
    p->spell = spell;
    p->icon = icon;
    p->drop = drop;
    p->hit = hit;
    p->t = 0;
    p->dur = 6 + d * 3;
    p->sx = pl.px;
    p->sy = pl.py - 4;
}

void proj_update(void) {
    for (int k = 0; k < MAX_PROJ; k++) {
        Proj *p = &projs[k];
        if (!p->active || ++p->t < p->dur) continue;
        p->active = 0;
        Npc *n = &npcs[p->npc];
        if (!n->active) continue;
        // most arrows can be picked up again under the target
        if (p->drop && rnd_range(100) < 80) drop_item(n->x, n->y, n->plane, p->drop, 1);
        const SpellDef *sp = &spells[p->spell];
        if (p->cb == CB_MAGIC && (sp->kind == SPK_CURSE || sp->kind == SPK_BIND)) {
            // dmg holds hit (1) or splash (0) for curses; binds also deal a little damage
            if (p->hit) {
                if (sp->kind == SPK_CURSE) n->curse |= sp->param;
                else n->bound = sp->param;
            }
            if (sp->kind == SPK_CURSE) {
                n->hpbar_timer = 200;
                splat_spawn(n->px, n->py, 0);
                add_xp(SK_MAGIC, sp->xp);
                sfx(p->hit ? SFX_HIT : SFX_MISS);
                continue;
            }
        }
        land_hit(p->npc, p->dmg, p->cb, p->spell);
    }
}

void proj_clear(void) {
    for (int k = 0; k < MAX_PROJ; k++) projs[k].active = 0;
}

// what the wielded weapon hits with in OSRS (its main style): swords, daggers and pickaxes
// stab; scimitars, longswords, 2h swords, claws and axes slash; fists, maces, warhammers and
// staves crush
enum { HIT_STAB, HIT_SLASH, HIT_CRUSH };
static const u8 class_hit[] = {
    [W_NONE] = HIT_CRUSH, [W_DAGGER] = HIT_STAB, [W_SWORD] = HIT_STAB, [W_SCIM] = HIT_SLASH,
    [W_LONG] = HIT_SLASH, [W_2H] = HIT_SLASH, [W_AXE] = HIT_SLASH, [W_BATTLEAXE] = HIT_SLASH,
    [W_MACE] = HIT_CRUSH, [W_WARHAMMER] = HIT_CRUSH, [W_CLAWS] = HIT_SLASH, [W_PICK] = HIT_STAB,
};
static int melee_type(void) { return class_hit[items[pl.equip[EQ_WEAPON]].wclass]; }

// OSRS defence rolls: Defence level with the stab/slash/crush or ranged bonus; spells are
// defended with the Magic level and the magic bonus (curses lower Defence, not Magic)
static int npc_def_roll(int i, int cb) {
    const Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    if (cb == CB_MAGIC) return ((d->mag ? d->mag : 1) + 9) * (d->mdef + 64);
    int t = melee_type(), bonus = cb == CB_RANGED ? d->rdef : t == HIT_STAB ? d->defb : t == HIT_SLASH ? d->dslash : d->dcrush;
    return (cursed(n, d->def, CU_DEF5, CU_DEF10) + 9) * (bonus + 64);
}

static void engage(int i) {
    Npc *n = &npcs[i];
    foe_engage(i);
    n->in_combat = 1;
    n->hpbar_timer = 200;
    if (!n->attack_cd) n->attack_cd = 2;
}

// Delrith only takes damage from Silverlight
static int demon_guard(int i, int dmg) {
    if (npcs[i].kind == NPC_DELRITH && pl.equip[EQ_WEAPON] != I_SILVERLIGHT) {
        if (dmg) msg_col("Only Silverlight can harm the demon!", TC_ORANGE);
        return 0;
    }
    return dmg;
}

static int player_shoot(int i) {
    int knife = thrown_weapon(pl.equip[EQ_WEAPON]) ? pl.equip[EQ_WEAPON] : 0;
    int arrow = knife ? knife : pl.equip[EQ_AMMO];
    if (knife && !pl.weapon_qty) {
        msg_col("You have run out of knives.", TC_RED);   // (never: the last one empties the slot)
        return 0;
    }
    if (!knife && (!arrow || !pl.ammo_qty)) {
        msg_col("There is no ammo left in your quiver.", TC_RED);
        return 0;
    }
    if (!knife && !ammo_ok(arrow, pl.equip[EQ_WEAPON])) {
        msg_col("You can't use that ammo with your bow.", TC_RED);
        return 0;
    }
    int eff = skill_level(SK_RANGED) + (pl.style == STYLE_ACCURATE ? 3 : 0) + 8;
    int a = eff * (equip_bonus(3) + 64);
    int maxhit = (eff * (equip_bonus(4) + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = demon_guard(i, roll(hit_chance256(a, npc_def_roll(i, CB_RANGED))) ? rnd_range(maxhit + 1) : 0);
    if (knife) {
        if (!--pl.weapon_qty) pl.equip[EQ_WEAPON] = 0;
    } else if (!--pl.ammo_qty) {
        pl.equip[EQ_AMMO] = 0;
    }
    sfx(SFX_BOW);
    proj_launch(i, dmg, CB_RANGED, 0, ICON_PROJ_ARROW_H, arrow, 1);
    return 1;
}

static char inspect_buf[96];
static char *sadd_c(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}
static char *sadd_n(char *p, int v) {
    fmt_int(p, v);
    while (*p) p++;
    return p;
}
static char *sbuf_npc(const char *name) { return sadd_c(inspect_buf, name); }

static int is_undead(int i) {
    int k = npc_defs[npcs[i].kind].sprite;
    return k == SPR_KIND_SKELETON || k == SPR_KIND_GHOST;
}

static int player_cast(int i) {
    int s = cast_once ? cast_once : pl.spell;
    const SpellDef *sp = &spells[s];
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    if (!spell_castable(s, 0)) return 0;
    if (sp->kind == SPK_COMBAT && sp->param && !is_undead(i)) {
        msg("This spell only affects skeletons, zombies, ghosts and shades.");
        return 0;
    }
    if (sp->kind == SPK_CURSE && (n->curse & sp->param)) {
        msg("Your target has already been affected by this spell.");
        return 0;
    }
    spell_pay(s);
    sfx(SFX_CAST);
    if (sp->kind == SPK_INSPECT) {
        char *p = sbuf_npc(d->name);
        p = sadd_c(p, ": level ");
        p = sadd_n(p, d->level);
        p = sadd_c(p, ", hitpoints ");
        p = sadd_n(p, n->hp);
        p = sadd_c(p, "/");
        p = sadd_n(p, d->hp);
        p = sadd_c(p, ", attack ");
        p = sadd_n(p, d->att);
        p = sadd_c(p, ", strength ");
        p = sadd_n(p, d->str);
        p = sadd_c(p, ", defence ");
        sadd_n(p, d->def);
        msg_col(inspect_buf, TC_CYAN);
        add_xp(SK_MAGIC, sp->xp);
        return 2;   // a look, not an attack
    }
    int a = (skill_level(SK_MAGIC) + 8) * (equip_bonus(5) + 64);
    int maxhit = spell_maxhit(s);
    // elemental weakness: that much more accuracy and damage for the matching Strike..Surge
    int el = !sp->group ? ELEM_NONE : sp->icon == ICON_PROJ_WIND ? ELEM_AIR : sp->icon == ICON_PROJ_WATER ? ELEM_WATER
           : sp->icon == ICON_PROJ_EARTH ? ELEM_EARTH : ELEM_FIRE;
    if (el && el == d->weak) {
        a = a * (100 + d->weakpct) / 100;
        maxhit += maxhit * d->weakpct / 100;
    }
    int hit = roll(hit_chance256(a, npc_def_roll(i, CB_MAGIC)));
    int dmg = 0;
    if (sp->kind == SPK_COMBAT || sp->kind == SPK_BIND) dmg = hit ? rnd_range(maxhit + 1) : 0;
    dmg = demon_guard(i, dmg);
    proj_launch(i, dmg, CB_MAGIC, s, sp->icon, 0, hit);
    return 1;
}

// one attack with whatever is wielded; 0 if it could not be made (no ammo or runes)
int player_attack(int i) {
    int cb = combat_type();
    if (cb != CB_MELEE) {
        int r = cb == CB_RANGED ? player_shoot(i) : player_cast(i);
        if (r == 1) engage(i);
        return r != 0;
    }
    int eff_a = skill_level(SK_ATTACK) + style_bonus(SK_ATTACK) + 8;
    int eff_s = skill_level(SK_STRENGTH) + style_bonus(SK_STRENGTH) + 8;
    int a = eff_a * (equip_bonus(0) + 64);
    int maxhit = (eff_s * (equip_bonus(1) + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = demon_guard(i, roll(hit_chance256(a, npc_def_roll(i, CB_MELEE))) ? rnd_range(maxhit + 1) : 0);
    engage(i);
    pl.lunge = 8;
    land_hit(i, dmg, CB_MELEE, 0);
    return 1;
}

void npc_attack(int i) {
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    int a, def, maxhit;
    if (d->magic_max) {
        // a spell: the NPC's Magic level against 70% of the player's Magic and 30% of their Defence
        a = (d->mag + 9) * 64;
        int eff = (skill_level(SK_MAGIC) * 7 + (skill_level(SK_DEFENCE) + style_bonus(SK_DEFENCE)) * 3) / 10 + 8;
        def = eff * 64;
        maxhit = d->magic_max;
    } else {
        a = (cursed(n, d->att, CU_ATK5, CU_ATK10) + 9) * (d->atkb + 64);
        int eff_d = skill_level(SK_DEFENCE) + style_bonus(SK_DEFENCE) + 8;
        def = eff_d * (equip_bonus(2) + 64);
        maxhit = ((cursed(n, d->str, CU_STR5, CU_STR10) + 9) * (d->strb + 64) + 320) / 640;   // 0 for chickens, rats, imps
    }
    int dmg = roll(hit_chance256(a, def)) ? rnd_range(maxhit + 1) : 0;
    if (dmg > pl.hp) dmg = pl.hp;
    foe_engage(i);
    home_cancel();   // combat interrupts the home teleport
    n->lunge = 8;
    pl.hpbar_timer = 200;
    splat_spawn(pl.px, pl.py, dmg);
    sfx(d->magic_max ? SFX_CAST : dmg ? SFX_HIT : SFX_MISS);
    pl.hp -= dmg;
    if (pl.action != ACT_COMBAT && pl.autoret && !pl.moving) {
        pl.action = ACT_COMBAT;
        pl.target = i;
        if (!pl.attack_cd) pl.attack_cd = 1;
        ui_set_target(i);
        target_set(i);
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
    home_cancel();
    proj_clear();
    target_set(-1);
    pl.x = START_X;
    pl.y = START_Y;
    pl.plane = START_P;
    pl.px = TILE_PX(pl.x);
    pl.py = TILE_PY(pl.y);
    pl.moving = 0;
    pl.dir = DIR_DOWN;
    pl.hp = skill_level(SK_HITPOINTS);
    pl.hpbar_timer = 0;
}

static void npc_drops(Npc *n) {
    int x = n->x, y = n->y, p = n->plane;
    switch (n->kind) {
    case NPC_COW:
        drop_item(x, y, p, I_BONES, 1);
        drop_item(x, y, p, I_RAW_BEEF, 1);
        drop_item(x, y, p, I_COWHIDE, 1);
        break;
    case NPC_CHICKEN:
        drop_item(x, y, p, I_BONES, 1);
        drop_item(x, y, p, I_RAW_CHICKEN, 1);
        drop_item(x, y, p, I_FEATHER, 5 + rnd_range(11));
        break;
    case NPC_GOBLIN: {
        drop_item(x, y, p, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 55) drop_item(x, y, p, I_COINS, 1 + rnd_range(15));
        else if (r < 63) drop_item(x, y, p, I_BRONZE_HELM, 1);
        else if (r < 70) drop_item(x, y, p, I_BRONZE_SHIELD, 1);
        else if (r < 75) drop_item(x, y, p, I_BRONZE_LEGS, 1);
        else if (r < 78) drop_item(x, y, p, I_LEATHER_BODY, 1);
        else if (r < 81) drop_item(x, y, p, I_BRONZE_DAGGER, 1);
        else if (r < 84) drop_item(x, y, p, I_BRONZE_FULL_HELM, 1);
        else if (r < 86) drop_item(x, y, p, I_BRONZE_CHAINBODY, 1);
        else if (r < 88) drop_item(x, y, p, I_BRONZE_SKIRT, 1);
        else if (r < 98) drop_item(x, y, p, I_GOBLIN_MAIL, 1);
        break;
    }
    case NPC_IMP: {
        static const u8 beads[4] = {I_RED_BEAD, I_YELLOW_BEAD, I_BLACK_BEAD, I_WHITE_BEAD};
        if (rnd_range(100) < 60) drop_item(x, y, p, beads[rnd_range(4)], 1);
        break;
    }
    case NPC_RAT:
        drop_item(x, y, p, I_BONES, 1);
        // the tail only drops while Hetty needs one (as in OSRS)
        if (quest_stage(Q_WITCH) == 1 && !has_item(I_RAT_TAIL)) drop_item(x, y, p, I_RAT_TAIL, 1);
        break;
    case NPC_SKELETON:
        drop_item(x, y, p, I_BONES, 1);
        break;
    case NPC_MAN:
    case NPC_WOMAN:
        drop_item(x, y, p, I_BONES, 1);
        if (rnd_range(100) < 70) drop_item(x, y, p, I_COINS, 3 + rnd_range(20));
        break;
    case NPC_DARK_WIZARD: {
        // OSRS dark wizards carry runes
        static const u8 runes[] = {I_AIR_RUNE, I_WATER_RUNE, I_EARTH_RUNE, I_FIRE_RUNE, I_MIND_RUNE, I_BODY_RUNE,
                                   I_CHAOS_RUNE, I_NATURE_RUNE, I_COSMIC_RUNE, I_LAW_RUNE, I_DEATH_RUNE, I_BLOOD_RUNE};
        static const u8 qty[] = {10, 10, 10, 10, 10, 8, 4, 3, 2, 2, 2, 1};
        drop_item(x, y, p, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 60) {
            int k = rnd_range(sizeof(runes));
            drop_item(x, y, p, runes[k], qty[k] + rnd_range(qty[k] + 1));
        } else if (r < 80) {
            drop_item(x, y, p, I_COINS, 5 + rnd_range(25));
        }
        break;
    }
    case NPC_WARRIOR:
    case NPC_BARBARIAN: {
        drop_item(x, y, p, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 50) drop_item(x, y, p, I_COINS, 5 + rnd_range(25));
        else if (r < 58) drop_item(x, y, p, I_BRONZE_SWORD, 1);
        else if (r < 64) drop_item(x, y, p, I_IRON_HELM, 1);
        else if (r < 68) drop_item(x, y, p, I_IRON_SWORD, 1);
        break;
    }
    case NPC_GUARD: {
        drop_item(x, y, p, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 60) drop_item(x, y, p, I_COINS, 10 + rnd_range(50));
        else if (r < 70) drop_item(x, y, p, I_IRON_HELM, 1);
        else if (r < 78) drop_item(x, y, p, I_IRON_SWORD, 1);
        else if (r < 84) drop_item(x, y, p, I_IRON_LEGS, 1);
        else if (r < 88) drop_item(x, y, p, I_STEEL_SWORD, 1);
        else if (r < 91) drop_item(x, y, p, I_IRON_BODY, 1);
        else if (r < 93) drop_item(x, y, p, I_IRON_SCIMITAR, 1);
        else if (r < 95) drop_item(x, y, p, I_IRON_KITE, 1);
        else if (r < 96) drop_item(x, y, p, I_IRON_FULL_HELM, 1);
        else if (r < 97) drop_item(x, y, p, I_IRON_CHAINBODY, 1);
        else if (r < 98) drop_item(x, y, p, I_STEEL_LONGSWORD, 1);
        else if (r < 99) {
            // rare table: a taste of the higher tiers (and, as on OSRS's rare drop table, a runite bar)
            static const u16 rare[] = {I_MITHRIL_SCIMITAR, I_MITHRIL_FULL_HELM, I_MITHRIL_KITE,
                                       I_STEEL_BODY, I_STEEL_LEGS, I_ADAMANT_SCIMITAR, I_MITHRIL_BODY,
                                       I_RUNE_SCIMITAR, I_RUNE_BAR};
            drop_item(x, y, p, rare[rnd_range(sizeof(rare) / sizeof(rare[0]))], 1);
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
    n->curse = n->bound = 0;
    occ_clear(n->x, n->y, n->plane);
    n->in_combat = 0;
    foe_release(i);
    n->respawn = npc_defs[n->kind].respawn;
    if (pl.target == i && pl.action == ACT_COMBAT) player_stop();
    if (lock_target == i) target_set(-1);
}
