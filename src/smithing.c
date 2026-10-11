// Smithing: smelting ore into bars at furnaces, hammering bars into items at anvils, and the
// Superheat Item spell. Levels, XP, materials, outputs and ticks follow the OSRS Wiki.
#include "world_internal.h"

// furnace interface order: bar, ore, second ore x count, level, ticks, XP, how many, tool needed.
// Cannonballs pour a steel bar into an ammo mould.
const SmeltDef smelt_defs[SMELT_N] = {
    {I_BRONZE_BAR, I_COPPER_ORE, I_TIN_ORE, 1, 1, 5, 62, 1, 0},
    {I_BLURITE_BAR, I_BLURITE_ORE, 0, 0, 13, 4, 80, 1, 0},
    {I_IRON_BAR, I_IRON_ORE, 0, 0, 15, 4, 125, 1, 0},
    {I_SILVER_BAR, I_SILVER_ORE, 0, 0, 20, 4, 137, 1, 0},
    {I_STEEL_BAR, I_IRON_ORE, I_COAL, 2, 30, 4, 175, 1, 0},
    {I_GOLD_BAR, I_GOLD_ORE, 0, 0, 40, 5, 225, 1, 0},
    {I_MITHRIL_BAR, I_MITHRIL_ORE, I_COAL, 4, 50, 4, 300, 1, 0},
    {I_ADAMANT_BAR, I_ADAMANT_ORE, I_COAL, 6, 70, 4, 375, 1, 0},
    {I_RUNE_BAR, I_RUNE_ORE, I_COAL, 8, 85, 4, 500, 1, 0},
    {I_CANNONBALL, I_STEEL_BAR, 0, 0, 35, 8, 256, 4, I_AMMO_MOULD},
};

// anvil interface order: weapons, armour, then ammunition and parts (and the tier's specials)
static const SmithDef anvil_bronze[] = {
    {I_BRONZE_DAGGER, 1, 1, 1}, {I_BRONZE_SWORD, 4, 1, 1}, {I_BRONZE_SCIMITAR, 5, 2, 1},
    {I_BRONZE_LONGSWORD, 6, 2, 1}, {I_BRONZE_2H, 14, 3, 1}, {I_BRONZE_AXE, 1, 1, 1},
    {I_BRONZE_MACE, 2, 1, 1}, {I_BRONZE_WARHAMMER, 9, 3, 1}, {I_BRONZE_BATTLEAXE, 10, 3, 1},
    {I_BRONZE_CLAWS, 13, 2, 1}, {I_BRONZE_CHAINBODY, 11, 3, 1}, {I_BRONZE_LEGS, 16, 3, 1},
    {I_BRONZE_SKIRT, 16, 3, 1}, {I_BRONZE_BODY, 18, 5, 1}, {I_BRONZE_HELM, 3, 1, 1},
    {I_BRONZE_FULL_HELM, 7, 2, 1}, {I_BRONZE_SHIELD, 8, 2, 1}, {I_BRONZE_KITE, 12, 3, 1},
    {I_BRONZE_NAILS, 4, 1, 15}, {I_BRONZE_DART_TIP, 4, 1, 10}, {I_BRONZE_ARROWTIPS, 5, 1, 15},
    {I_BRONZE_KNIFE, 7, 1, 5}, {I_BRONZE_JAVELIN_TIPS, 6, 1, 5}, {I_BRONZE_BOLTS, 3, 1, 10},
    {I_BRONZE_LIMBS, 6, 1, 1}, {I_BRONZE_WIRE, 4, 1, 1},
};
static const SmithDef anvil_blurite[] = {
    {I_BLURITE_BOLTS, 8, 1, 10}, {I_BLURITE_LIMBS, 13, 1, 1},
};
static const SmithDef anvil_iron[] = {
    {I_IRON_DAGGER, 15, 1, 1}, {I_IRON_SWORD, 19, 1, 1}, {I_IRON_SCIMITAR, 20, 2, 1},
    {I_IRON_LONGSWORD, 21, 2, 1}, {I_IRON_2H, 29, 3, 1}, {I_IRON_AXE, 16, 1, 1},
    {I_IRON_MACE, 17, 1, 1}, {I_IRON_WARHAMMER, 24, 3, 1}, {I_IRON_BATTLEAXE, 25, 3, 1},
    {I_IRON_CLAWS, 28, 2, 1}, {I_IRON_CHAINBODY, 26, 3, 1}, {I_IRON_LEGS, 31, 3, 1},
    {I_IRON_SKIRT, 31, 3, 1}, {I_IRON_BODY, 33, 5, 1}, {I_IRON_HELM, 18, 1, 1},
    {I_IRON_FULL_HELM, 22, 2, 1}, {I_IRON_SHIELD, 23, 2, 1}, {I_IRON_KITE, 27, 3, 1},
    {I_IRON_NAILS, 19, 1, 15}, {I_IRON_DART_TIP, 19, 1, 10}, {I_IRON_ARROWTIPS, 20, 1, 15},
    {I_IRON_KNIFE, 22, 1, 5}, {I_IRON_JAVELIN_TIPS, 21, 1, 5}, {I_IRON_BOLTS, 18, 1, 10},
    {I_IRON_LIMBS, 23, 1, 1}, {I_IRON_SPIT, 17, 1, 1}, {I_OIL_LANTERN_FRAME, 26, 1, 1},
};
static const SmithDef anvil_steel[] = {
    {I_STEEL_DAGGER, 30, 1, 1}, {I_STEEL_SWORD, 34, 1, 1}, {I_STEEL_SCIMITAR, 35, 2, 1},
    {I_STEEL_LONGSWORD, 36, 2, 1}, {I_STEEL_2H, 44, 3, 1}, {I_STEEL_AXE, 31, 1, 1},
    {I_STEEL_MACE, 32, 1, 1}, {I_STEEL_WARHAMMER, 39, 3, 1}, {I_STEEL_BATTLEAXE, 40, 3, 1},
    {I_STEEL_CLAWS, 43, 2, 1}, {I_STEEL_CHAINBODY, 41, 3, 1}, {I_STEEL_LEGS, 46, 3, 1},
    {I_STEEL_SKIRT, 46, 3, 1}, {I_STEEL_BODY, 48, 5, 1}, {I_STEEL_HELM, 33, 1, 1},
    {I_STEEL_FULL_HELM, 37, 2, 1}, {I_STEEL_SHIELD, 38, 2, 1}, {I_STEEL_KITE, 42, 3, 1},
    {I_STEEL_NAILS, 34, 1, 15}, {I_STEEL_DART_TIP, 34, 1, 10}, {I_STEEL_ARROWTIPS, 35, 1, 15},
    {I_STEEL_KNIFE, 37, 1, 5}, {I_STEEL_JAVELIN_TIPS, 36, 1, 5}, {I_STEEL_BOLTS, 33, 1, 10},
    {I_STEEL_LIMBS, 36, 1, 1}, {I_STEEL_STUDS, 36, 1, 1},
};
static const SmithDef anvil_mithril[] = {
    {I_MITHRIL_DAGGER, 50, 1, 1}, {I_MITHRIL_SWORD, 54, 1, 1}, {I_MITHRIL_SCIMITAR, 55, 2, 1},
    {I_MITHRIL_LONGSWORD, 56, 2, 1}, {I_MITHRIL_2H, 64, 3, 1}, {I_MITHRIL_AXE, 51, 1, 1},
    {I_MITHRIL_MACE, 52, 1, 1}, {I_MITHRIL_WARHAMMER, 59, 3, 1}, {I_MITHRIL_BATTLEAXE, 60, 3, 1},
    {I_MITHRIL_CLAWS, 63, 2, 1}, {I_MITHRIL_CHAINBODY, 61, 3, 1}, {I_MITHRIL_LEGS, 66, 3, 1},
    {I_MITHRIL_SKIRT, 66, 3, 1}, {I_MITHRIL_BODY, 68, 5, 1}, {I_MITHRIL_HELM, 53, 1, 1},
    {I_MITHRIL_FULL_HELM, 57, 2, 1}, {I_MITHRIL_SHIELD, 58, 2, 1}, {I_MITHRIL_KITE, 62, 3, 1},
    {I_MITHRIL_NAILS, 54, 1, 15}, {I_MITHRIL_DART_TIP, 54, 1, 10}, {I_MITHRIL_ARROWTIPS, 55, 1, 15},
    {I_MITHRIL_KNIFE, 57, 1, 5}, {I_MITHRIL_JAVELIN_TIPS, 56, 1, 5}, {I_MITHRIL_BOLTS, 53, 1, 10},
    {I_MITHRIL_LIMBS, 56, 1, 1},
};
static const SmithDef anvil_adamant[] = {
    {I_ADAMANT_DAGGER, 70, 1, 1}, {I_ADAMANT_SWORD, 74, 1, 1}, {I_ADAMANT_SCIMITAR, 75, 2, 1},
    {I_ADAMANT_LONGSWORD, 76, 2, 1}, {I_ADAMANT_2H, 84, 3, 1}, {I_ADAMANT_AXE, 71, 1, 1},
    {I_ADAMANT_MACE, 72, 1, 1}, {I_ADAMANT_WARHAMMER, 79, 3, 1}, {I_ADAMANT_BATTLEAXE, 80, 3, 1},
    {I_ADAMANT_CLAWS, 83, 2, 1}, {I_ADAMANT_CHAINBODY, 81, 3, 1}, {I_ADAMANT_LEGS, 86, 3, 1},
    {I_ADAMANT_SKIRT, 86, 3, 1}, {I_ADAMANT_BODY, 88, 5, 1}, {I_ADAMANT_HELM, 73, 1, 1},
    {I_ADAMANT_FULL_HELM, 77, 2, 1}, {I_ADAMANT_SHIELD, 78, 2, 1}, {I_ADAMANT_KITE, 82, 3, 1},
    {I_ADAMANT_NAILS, 74, 1, 15}, {I_ADAMANT_DART_TIP, 74, 1, 10}, {I_ADAMANT_ARROWTIPS, 75, 1, 15},
    {I_ADAMANT_KNIFE, 77, 1, 5}, {I_ADAMANT_JAVELIN_TIPS, 76, 1, 5}, {I_ADAMANT_BOLTS, 73, 1, 10},
    {I_ADAMANT_LIMBS, 76, 1, 1},
};
static const SmithDef anvil_rune[] = {
    {I_RUNE_DAGGER, 85, 1, 1}, {I_RUNE_SWORD, 89, 1, 1}, {I_RUNE_SCIMITAR, 90, 2, 1},
    {I_RUNE_LONGSWORD, 91, 2, 1}, {I_RUNE_2H, 99, 3, 1}, {I_RUNE_AXE, 86, 1, 1},
    {I_RUNE_MACE, 87, 1, 1}, {I_RUNE_WARHAMMER, 94, 3, 1}, {I_RUNE_BATTLEAXE, 95, 3, 1},
    {I_RUNE_CLAWS, 98, 2, 1}, {I_RUNE_CHAINBODY, 96, 3, 1}, {I_RUNE_LEGS, 99, 3, 1},
    {I_RUNE_SKIRT, 99, 3, 1}, {I_RUNE_BODY, 99, 5, 1}, {I_RUNE_HELM, 88, 1, 1},
    {I_RUNE_FULL_HELM, 92, 2, 1}, {I_RUNE_SHIELD, 93, 2, 1}, {I_RUNE_KITE, 97, 3, 1},
    {I_RUNE_NAILS, 89, 1, 15}, {I_RUNE_DART_TIP, 89, 1, 10}, {I_RUNE_ARROWTIPS, 90, 1, 15},
    {I_RUNE_KNIFE, 92, 1, 5}, {I_RUNE_JAVELIN_TIPS, 91, 1, 5}, {I_RUNE_BOLTS, 88, 1, 10},
    {I_RUNE_LIMBS, 91, 1, 1},
};
#define TIER(bar, xp, name, list) {bar, xp, name, list, sizeof(list) / sizeof(list[0])}
// XP is per bar used
const AnvilTier anvil_tiers[ANVIL_TIERS] = {
    TIER(I_BRONZE_BAR, 125, "bronze", anvil_bronze), TIER(I_BLURITE_BAR, 175, "blurite", anvil_blurite),
    TIER(I_IRON_BAR, 250, "iron", anvil_iron),
    TIER(I_STEEL_BAR, 375, "steel", anvil_steel), TIER(I_MITHRIL_BAR, 500, "mithril", anvil_mithril),
    TIER(I_ADAMANT_BAR, 625, "adamantite", anvil_adamant), TIER(I_RUNE_BAR, 750, "runite", anvil_rune),
};

// the job in progress (test scripts read these): furnace or anvil, smelt_defs / anvil_tiers
// index, item in the tier's list, and how many more to make
u8 smith_furnace, smith_k, smith_i;
u16 smith_left;

int smelt_max(int k) {
    const SmeltDef *d = &smelt_defs[k];
    if (d->tool && !inv_count(d->tool)) return 0;
    int n = inv_count(d->ore);
    if (d->ore2) {
        int m = inv_count(d->ore2) / d->n2;
        if (m < n) n = m;
    }
    return n;
}

int smith_max(int t, int i) { return inv_count(anvil_tiers[t].bar) / anvil_tiers[t].list[i].bars; }

// the tier whose bars the player has, best first (-1: none)
int anvil_tier_owned(void) {
    for (int t = ANVIL_TIERS - 1; t >= 0; t--)
        if (inv_count(anvil_tiers[t].bar)) return t;
    return -1;
}

static char *put(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}
static char *put_lower(char *p, const char *s) {
    for (; *s; s++) *p++ = (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s;
    *p = 0;
    return p;
}
static char *put_num(char *p, int n) {
    fmt_int(p, n);
    while (*p) p++;
    return p;
}
// "steel" for a Steel bar
static char *put_metal(char *p, int bar) {
    p = put_lower(p, items[bar].name) - 4;
    *p = 0;
    return p;
}
static void need_level(int lvl, const char *what) {
    char buf[64], *p = put(buf, "You need a Smithing level of ");
    p = put_num(p, lvl);
    p = put(p, " to ");
    put(p, what);
    msg(buf);
    sfx(SFX_ERR);
}

int smith_start(int furnace, int k, int i, int count) {
    if (count <= 0) return 0;
    if (furnace) {
        const SmeltDef *d = &smelt_defs[k];
        if (skill_level(SK_SMITHING) < d->level) {
            char what[40];
            if (d->tool) put(what, "make cannonballs");
            else put_metal(put(what, "smelt "), d->bar);
            need_level(d->level, what);
            return 0;
        }
        if (d->tool && !inv_count(d->tool)) {
            msg("You need an ammo mould to make cannonballs.");
            sfx(SFX_ERR);
            return 0;
        }
        if (!smelt_max(k)) {
            char buf[64], *p = put(buf, d->tool ? "You don't have any bars to pour into " : "You don't have the ore to smelt ");
            if (d->tool) p = put(p, "the mould");
            else p = put_metal(p, d->bar);
            put(p, ".");
            msg(buf);
            sfx(SFX_ERR);
            return 0;
        }
    } else {
        const SmithDef *s = &anvil_tiers[k].list[i];
        if (!inv_count(I_HAMMER)) {
            msg("You need a hammer to work the metal with.");
            sfx(SFX_ERR);
            return 0;
        }
        if (skill_level(SK_SMITHING) < s->level) {
            char what[48], *p = put(what, "make ");
            p = put(p, (s->qty > 1) ? "" : "a ");
            put_lower(p, items[s->item].name);
            need_level(s->level, what);
            return 0;
        }
        if (!smith_max(k, i)) {
            msg("You don't have enough bars to make that.");
            sfx(SFX_ERR);
            return 0;
        }
    }
    smith_furnace = furnace;
    smith_k = k;
    smith_i = i;
    smith_left = count > 999 ? 999 : count;
    pl.action = furnace ? ACT_SMELT : ACT_SMITH;
    pl.tx = pl.x + DX[pl.dir];
    pl.ty = pl.y + DY[pl.dir];
    pl.action_timer = 0;
    if (furnace) msg(smelt_defs[k].tool ? "You heat the steel bar into a liquid state."
                     : smelt_defs[k].ore2 == I_TIN_ORE ? "You smelt the copper and tin together in the furnace."
                     : smelt_defs[k].ore2 ? "You place the ore and coal into the furnace." : "You place the ore into the furnace.");
    return 1;
}

static int is_bolts(int id) {
    return id == I_BRONZE_BOLTS || id == I_IRON_BOLTS || id == I_STEEL_BOLTS || id == I_MITHRIL_BOLTS ||   // (not blurite)
           id == I_ADAMANT_BOLTS || id == I_RUNE_BOLTS;
}

// one bar from the ores in the inventory. Iron alone (no coal) refines half the time, unless
// superheated or a ring of forging is worn (each bar uses one of its 140 charges); goldsmith
// gauntlets make gold bars worth 56.2 XP.
#define FORGING_CHARGES 140
static void smelt_one(const SmeltDef *d, int sure) {
    inv_remove(d->ore, 1);
    if (d->ore2) inv_remove(d->ore2, d->n2);
    if (d->bar == I_IRON_BAR && !sure) {
        if (pl.equip[EQ_RING] == I_RING_FORGING) {
            if (++pl.forging >= FORGING_CHARGES) {
                pl.equip[EQ_RING] = 0;
                pl.forging = 0;
                msg_col("Your Ring of Forging has melted.", TC_RED);
            }
        } else if (rnd_range(2)) {
            msg("The ore is too impure and you fail to refine it.");
            return;
        }
    }
    inv_add(d->bar, d->out);
    add_xp(SK_SMITHING, d->bar == I_GOLD_BAR && pl.equip[EQ_HANDS] == I_GOLDSMITH_GAUNTLETS ? 562 : d->xp);
    if (d->tool) {
        msg("You remove the cannonballs from the mould.");
        return;
    }
    char buf[48];
    put(put_metal(put(buf, "You retrieve a bar of "), d->bar), ".");
    msg(buf);
}

// one game tick at the furnace or anvil
void smith_tick(void) {
    int t = world_type(pl.tx, pl.ty, pl.plane);
    if (t != (smith_furnace ? T_FURNACE : T_ANVIL) || !smith_left) {
        player_stop();
        return;
    }
    int ticks;
    if (smith_furnace) ticks = smelt_defs[smith_k].ticks;
    else ticks = is_bolts(anvil_tiers[smith_k].list[smith_i].item) ? 3 : 5;
    int done = ++pl.action_timer >= ticks;
    if (done) pl.action_timer = 0;
    if (!smith_furnace && pl.action_timer % 2 == 0) sfx(SFX_MINE);   // the hammer rings on the anvil
    if (!done) return;
    if (smith_furnace) {
        const SmeltDef *d = &smelt_defs[smith_k];
        if (!smelt_max(smith_k)) {
            player_stop();
            return;
        }
        pl.lunge = 6;
        sfx(SFX_FIRE);
        smelt_one(d, 0);
        if (!--smith_left || !smelt_max(smith_k)) player_stop();
    } else {
        const AnvilTier *a = &anvil_tiers[smith_k];
        const SmithDef *s = &a->list[smith_i];
        if (!smith_max(smith_k, smith_i) || !inv_count(I_HAMMER)) {
            player_stop();
            return;
        }
        inv_remove(a->bar, s->bars);
        inv_add(s->item, s->qty);
        add_xp(SK_SMITHING, a->xp * s->bars);
        char buf[64], *p = put(buf, "You hammer the ");
        p = put(p, a->metal);
        p = put(p, " and make ");
        const char *n = items[s->item].name;
        char c = n[0] | 32;
        p = put(p, s->qty > 1 ? "some " : (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an " : "a ");
        p = put_lower(p, n);
        put(p, ".");
        msg(buf);
        if (!--smith_left || !smith_max(smith_k, smith_i)) player_stop();
    }
}

// Superheat Item: smelts the ore without a furnace (iron with two coal makes steel, as at a
// furnace; iron never fails)
int spell_superheat(int s, int slot) {
    const SpellDef *sp = &spells[s];
    int ore = pl.inv[slot].id;
    if (sp->kind != SPK_HEAT || !ore) return 0;
    int k = -1;
    for (int i = SMELT_N - 1; i >= 0; i--) {
        const SmeltDef *d = &smelt_defs[i];
        if ((d->ore != ore && d->ore2 != ore) || d->tool) continue;   // (cannonballs need a furnace)
        if (ore == I_COAL) continue;
        // iron: steel when there is coal for it and the level allows, else iron
        if (d->bar == I_STEEL_BAR && (inv_count(I_COAL) < 2 || skill_level(SK_SMITHING) < d->level)) continue;
        k = i;
        break;
    }
    if (k < 0) {
        msg("You need to cast superheat item on ore.");
        sfx(SFX_ERR);
        return 0;
    }
    const SmeltDef *d = &smelt_defs[k];
    if (skill_level(SK_SMITHING) < d->level) {
        char what[40];
        put_metal(put(what, "smelt "), d->bar);
        need_level(d->level, what);
        return 0;
    }
    if (!smelt_max(k)) {
        char buf[64], *p = buf;
        if (d->ore2 == I_TIN_ORE) {
            put(p, "You need one copper ore and one tin ore to make bronze.");
        } else {
            p = put(p, "You need ");
            p = put_num(p, d->n2);
            p = put(p, " heaps of coal to smelt ");
            put(put_metal(p, d->bar), ".");
        }
        msg(buf);
        sfx(SFX_ERR);
        return 0;
    }
    if (!spell_castable(s, 0)) return 0;
    spell_pay(s);
    smelt_one(d, 1);
    add_xp(SK_MAGIC, sp->xp);
    sfx(SFX_CAST);
    return 1;
}

// Doric's anvils, in his house north of Falador, are his until Doric's Quest is done
static int doric_anvil(int x, int y, int p) {
    for (int i = 0; i < npc_hi; i++) {
        const Npc *n = &npcs[i];
        if (n->kind == NPC_DORIC && n->plane == p && n->hx - x <= 6 && x - n->hx <= 6 && n->hy - y <= 6 && y - n->hy <= 6)
            return 1;
    }
    return 0;
}

void smith_interact(int t, int x, int y) {
    if (t == T_ANVIL) {
        if (doric_anvil(x, y, pl.plane) && !quest_done(Q_DORIC)) {
            msg("Doric won't let you use his anvils until you've helped him.");
            sfx(SFX_ERR);
            return;
        }
        if (!inv_count(I_HAMMER)) {
            msg("You need a hammer to work the metal with.");
            sfx(SFX_ERR);
            return;
        }
        if (anvil_tier_owned() < 0) {
            msg("You don't have any bars to smith.");
            sfx(SFX_ERR);
            return;
        }
    }
    smithy_open(t == T_FURNACE);
}
