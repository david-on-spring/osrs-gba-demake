// The OSRS standard spellbook (levels, runes and XP from the OSRS Wiki), in book order.
// Combat spells, curses, binds, Monster Inspect, Low / High Level Alchemy, Superheat Item and the
// Lumbridge, Varrock and Falador teleports work; spells that need systems the demake does not have yet
// (enchanting, orbs, god staffs, other destinations...) are listed and say so when cast.
#include "world_internal.h"

#define AIR I_AIR_RUNE
#define WATER I_WATER_RUNE
#define EARTH I_EARTH_RUNE
#define FIRE I_FIRE_RUNE
#define MIND I_MIND_RUNE
#define BODY I_BODY_RUNE
#define COSMIC I_COSMIC_RUNE
#define CHAOS I_CHAOS_RUNE
#define NATURE I_NATURE_RUNE
#define LAW I_LAW_RUNE
#define DEATH I_DEATH_RUNE
#define BLOOD I_BLOOD_RUNE
#define SOUL I_SOUL_RUNE
#define WRATH I_WRATH_RUNE
#define PW ICON_PROJ_WIND
#define PA ICON_PROJ_WATER
#define PE ICON_PROJ_EARTH
#define PF ICON_PROJ_FIRE
#define PC ICON_PROJ_CURSE
#define PB ICON_PROJ_BIND

// name, level, kind, max hit, tier, projectile, param, XP (tenths), runes, quantities
#define CB(n, l, m, g, ic, xp, r, q) {n, l, SPK_COMBAT, m, g, ic, 0, xp, r, q}
#define CU(n, l, bit, xp, r, q) {n, l, SPK_CURSE, 0, SPG_NONE, PC, bit, xp, r, q}
#define NA(n, l, xp, r, q) {n, l, SPK_NA, 0, SPG_NONE, 0, 0, xp, r, q}
#define R(...) {__VA_ARGS__}

const SpellDef spells[] = {
    {"None", 0, SPK_NA, 0, 0, 0, 0, 0, R(0), R(0)},
    {"Lumbridge Home Teleport", 0, SPK_TELE, 0, 0, 0, 1, 0, R(0), R(0)},
    NA("Minigame Teleport", 0, 0, R(0), R(0)),
    CB("Wind Strike", 1, 2, SPG_STRIKE, PW, 55, R(AIR, MIND), R(1, 1)),
    CU("Confuse", 3, CU_ATK5, 130, R(EARTH, WATER, BODY), R(2, 3, 1)),
    NA("Enchant Crossbow Bolt (Opal)", 4, 90, R(AIR, COSMIC), R(2, 1)),
    CB("Water Strike", 5, 4, SPG_STRIKE, PA, 75, R(AIR, WATER, MIND), R(1, 1, 1)),
    NA("Lvl-1 Enchant", 7, 175, R(WATER, COSMIC), R(1, 1)),
    NA("Enchant Crossbow Bolt (Sapphire)", 7, 175, R(WATER, COSMIC, MIND), R(1, 1, 1)),
    CB("Earth Strike", 9, 6, SPG_STRIKE, PE, 95, R(AIR, EARTH, MIND), R(1, 2, 1)),
    CU("Weaken", 11, CU_STR5, 210, R(EARTH, WATER, BODY), R(2, 3, 1)),
    CB("Fire Strike", 13, 8, SPG_STRIKE, PF, 115, R(AIR, FIRE, MIND), R(2, 3, 1)),
    NA("Enchant Crossbow Bolt (Jade)", 14, 190, R(EARTH, COSMIC), R(2, 1)),
    NA("Bones to Bananas", 15, 250, R(EARTH, WATER, NATURE), R(2, 2, 1)),
    CB("Wind Bolt", 17, 9, SPG_BOLT, PW, 135, R(AIR, CHAOS), R(2, 1)),
    CU("Curse", 19, CU_DEF5, 290, R(EARTH, WATER, BODY), R(3, 2, 1)),
    {"Bind", 20, SPK_BIND, 0, SPG_NONE, PB, 8, 300, R(EARTH, WATER, NATURE), R(3, 3, 2)},
    {"Low Level Alchemy", 21, SPK_ALCH, 0, SPG_NONE, 0, 40, 310, R(FIRE, NATURE), R(3, 1)},
    CB("Water Bolt", 23, 10, SPG_BOLT, PA, 165, R(AIR, WATER, CHAOS), R(2, 2, 1)),
    NA("Enchant Crossbow Bolt (Pearl)", 24, 290, R(WATER, COSMIC), R(2, 1)),
    {"Varrock Teleport", 25, SPK_TELE, 0, SPG_NONE, 0, 2, 350, R(AIR, FIRE, LAW), R(3, 1, 1)},
    NA("Lvl-2 Enchant", 27, 370, R(AIR, COSMIC), R(3, 1)),
    NA("Enchant Crossbow Bolt (Emerald)", 27, 370, R(AIR, COSMIC, NATURE), R(3, 1, 1)),
    CB("Earth Bolt", 29, 11, SPG_BOLT, PE, 195, R(AIR, EARTH, CHAOS), R(2, 3, 1)),
    NA("Enchant Crossbow Bolt (Red Topaz)", 29, 330, R(FIRE, COSMIC), R(2, 1)),
    {"Lumbridge Teleport", 31, SPK_TELE, 0, SPG_NONE, 0, 1, 410, R(AIR, EARTH, LAW), R(3, 1, 1)},
    NA("Telekinetic Grab", 33, 430, R(AIR, LAW), R(1, 1)),
    CB("Fire Bolt", 35, 12, SPG_BOLT, PF, 225, R(AIR, FIRE, CHAOS), R(3, 4, 1)),
    {"Falador Teleport", 37, SPK_TELE, 0, SPG_NONE, 0, 3, 480, R(AIR, WATER, LAW), R(3, 1, 1)},
    {"Crumble Undead", 39, SPK_COMBAT, 15, SPG_NONE, PE, 1, 245, R(AIR, EARTH, CHAOS), R(2, 2, 1)},
    NA("Teleport to House", 40, 300, R(AIR, EARTH, LAW), R(1, 1, 1)),
    CB("Wind Blast", 41, 13, SPG_BLAST, PW, 255, R(AIR, DEATH), R(3, 1)),
    {"Monster Inspect", 42, SPK_INSPECT, 0, SPG_NONE, 0, 0, 305, R(BODY, MIND), R(2, 2)},
    {"Superheat Item", 43, SPK_HEAT, 0, SPG_NONE, 0, 0, 530, R(FIRE, NATURE), R(4, 1)},
    NA("Camelot Teleport", 45, 555, R(AIR, LAW), R(5, 1)),
    CB("Water Blast", 47, 14, SPG_BLAST, PA, 285, R(AIR, WATER, DEATH), R(3, 3, 1)),
    NA("Kourend Castle Teleport", 48, 580, R(FIRE, WATER, LAW), R(1, 1, 2)),
    NA("Lvl-3 Enchant", 49, 590, R(FIRE, COSMIC), R(5, 1)),
    NA("Enchant Crossbow Bolt (Ruby)", 49, 590, R(FIRE, BLOOD, COSMIC), R(5, 1, 1)),
    NA("Iban Blast", 50, 300, R(FIRE, DEATH), R(5, 1)),
    {"Snare", 50, SPK_BIND, 3, SPG_NONE, PB, 16, 600, R(EARTH, WATER, NATURE), R(4, 4, 3)},
    NA("Magic Dart", 50, 300, R(DEATH, MIND), R(1, 4)),
    NA("Ardougne Teleport", 51, 610, R(WATER, LAW), R(2, 2)),
    CB("Earth Blast", 53, 15, SPG_BLAST, PE, 315, R(AIR, EARTH, DEATH), R(3, 4, 1)),
    NA("Civitas illa Fortis Teleport", 54, 640, R(EARTH, FIRE, LAW), R(1, 1, 2)),
    {"High Level Alchemy", 55, SPK_ALCH, 0, SPG_NONE, 0, 60, 650, R(FIRE, NATURE), R(5, 1)},
    NA("Charge Water Orb", 56, 660, R(WATER, COSMIC), R(30, 3)),
    NA("Summon Boat", 56, 660, R(EARTH, WATER, LAW), R(1, 1, 2)),
    NA("Lvl-4 Enchant", 57, 670, R(EARTH, COSMIC), R(10, 1)),
    NA("Enchant Crossbow Bolt (Diamond)", 57, 670, R(EARTH, COSMIC, LAW), R(10, 1, 2)),
    NA("Watchtower Teleport", 58, 680, R(EARTH, LAW), R(2, 2)),
    CB("Fire Blast", 59, 16, SPG_BLAST, PF, 345, R(AIR, FIRE, DEATH), R(4, 5, 1)),
    NA("Saradomin Strike", 60, 350, R(AIR, FIRE, BLOOD), R(4, 2, 2)),
    NA("Claws of Guthix", 60, 350, R(AIR, FIRE, BLOOD), R(4, 1, 2)),
    NA("Flames of Zamorak", 60, 350, R(AIR, FIRE, BLOOD), R(1, 4, 2)),
    NA("Charge Earth Orb", 60, 700, R(EARTH, COSMIC), R(30, 3)),
    NA("Bones to Peaches", 60, 355, R(EARTH, WATER, NATURE), R(2, 4, 2)),
    NA("Trollheim Teleport", 61, 680, R(FIRE, LAW), R(2, 2)),
    CB("Wind Wave", 62, 17, SPG_WAVE, PW, 360, R(AIR, BLOOD), R(5, 1)),
    NA("Charge Fire Orb", 63, 730, R(FIRE, COSMIC), R(30, 3)),
    NA("Ape Atoll Teleport", 64, 740, R(FIRE, WATER, LAW), R(2, 2, 2)),
    CB("Water Wave", 65, 18, SPG_WAVE, PA, 375, R(AIR, WATER, BLOOD), R(5, 7, 1)),
    CU("Vulnerability", 66, CU_DEF10, 760, R(EARTH, WATER, SOUL), R(5, 5, 1)),
    NA("Charge Air Orb", 66, 760, R(AIR, COSMIC), R(30, 3)),
    NA("Teleport to Boat", 67, 770, R(EARTH, WATER, LAW), R(2, 2, 2)),
    NA("Lvl-5 Enchant", 68, 780, R(EARTH, WATER, COSMIC), R(15, 15, 1)),
    NA("Enchant Crossbow Bolt (Dragonstone)", 68, 780, R(EARTH, COSMIC, SOUL), R(15, 1, 1)),
    CB("Earth Wave", 70, 19, SPG_WAVE, PE, 400, R(AIR, EARTH, BLOOD), R(5, 7, 1)),
    CU("Enfeeble", 73, CU_STR10, 830, R(EARTH, WATER, SOUL), R(8, 8, 1)),
    NA("Teleother Lumbridge", 74, 840, R(EARTH, LAW, SOUL), R(1, 1, 1)),
    CB("Fire Wave", 75, 20, SPG_WAVE, PF, 425, R(AIR, FIRE, BLOOD), R(5, 7, 1)),
    {"Entangle", 79, SPK_BIND, 5, SPG_NONE, PB, 24, 890, R(EARTH, WATER, NATURE), R(5, 5, 4)},
    CU("Stun", 80, CU_ATK10, 900, R(EARTH, WATER, SOUL), R(12, 12, 1)),
    NA("Tele Block", 80, 800, R(CHAOS, DEATH, LAW), R(1, 1, 1)),
    NA("Charge", 80, 1800, R(AIR, FIRE, BLOOD), R(3, 3, 3)),
    CB("Wind Surge", 81, 21, SPG_SURGE, PW, 445, R(AIR, WRATH), R(7, 1)),
    NA("Teleother Falador", 82, 920, R(WATER, LAW, SOUL), R(1, 1, 1)),
    CB("Water Surge", 85, 22, SPG_SURGE, PA, 465, R(AIR, WATER, WRATH), R(7, 10, 1)),
    NA("Teleport to Target", 85, 450, R(CHAOS, DEATH, LAW), R(1, 1, 1)),
    NA("Lvl-6 Enchant", 87, 970, R(EARTH, FIRE, COSMIC), R(20, 20, 1)),
    NA("Enchant Crossbow Bolt (Onyx)", 87, 970, R(FIRE, COSMIC, DEATH), R(20, 1, 1)),
    CB("Earth Surge", 90, 23, SPG_SURGE, PE, 482, R(AIR, EARTH, WRATH), R(7, 10, 1)),
    NA("Teleother Camelot", 90, 1000, R(LAW, SOUL), R(1, 2)),
    NA("Lvl-7 Enchant", 93, 1100, R(BLOOD, COSMIC, SOUL), R(20, 1, 20)),
    CB("Fire Surge", 95, 24, SPG_SURGE, PF, 505, R(AIR, FIRE, WRATH), R(7, 10, 1)),
};
const u8 spell_count = sizeof(spells) / sizeof(spells[0]);

// OSRS: a Strike/Bolt/Blast/Wave/Surge spell hits as hard as the best one of its tier the
// player has the level for
int spell_maxhit(int s) {
    const SpellDef *sp = &spells[s];
    int m = sp->maxhit, lvl = skill_level(SK_MAGIC);
    if (!sp->group) return m;
    for (int k = 1; k < spell_count; k++)
        if (spells[k].group == sp->group && spells[k].level <= lvl && spells[k].maxhit > m) m = spells[k].maxhit;
    return m;
}

// an elemental staff is an endless supply of its rune
static int staff_rune(void) {
    switch (pl.equip[EQ_WEAPON]) {
    case I_STAFF_AIR: return I_AIR_RUNE;
    case I_STAFF_WATER: return I_WATER_RUNE;
    case I_STAFF_EARTH: return I_EARTH_RUNE;
    case I_STAFF_FIRE: return I_FIRE_RUNE;
    default: return 0;
    }
}

int spell_runes_ok(int s) {
    const SpellDef *sp = &spells[s];
    int free_rune = staff_rune();
    for (int k = 0; k < 4; k++)
        if (sp->rune[k] && sp->rune[k] != free_rune && inv_count(sp->rune[k]) < sp->qty[k]) return 0;
    return 1;
}

int spell_castable(int s, int quiet) {
    const SpellDef *sp = &spells[s];
    if (!s) return 0;
    if (sp->kind == SPK_NA) {
        if (!quiet) msg_col("That spell is not available yet.", TC_ORANGE);
        return 0;
    }
    if (skill_level(SK_MAGIC) < sp->level) {
        if (!quiet) {
            char buf[48], *p = buf;
            const char *a = "You need a Magic level of ";
            while (*a) *p++ = *a++;
            fmt_int(p, sp->level);
            while (*p) p++;
            *p++ = '.';
            *p = 0;
            msg_col(buf, TC_RED);
        }
        return 0;
    }
    if (!spell_runes_ok(s)) {
        if (!quiet) msg_col("You do not have enough runes to cast this spell.", TC_RED);
        return 0;
    }
    return 1;
}

int spell_targeted(int s) {
    int k = spells[s].kind;
    return k == SPK_COMBAT || k == SPK_CURSE || k == SPK_BIND || k == SPK_INSPECT;
}

void spell_pay(int s) {
    const SpellDef *sp = &spells[s];
    int free_rune = staff_rune();
    for (int k = 0; k < 4; k++)
        if (sp->rune[k] && sp->rune[k] != free_rune) inv_remove(sp->rune[k], sp->qty[k]);
}

// Lumbridge Home Teleport: a 24-tick cast (kneel and draw the rune circle, read the spellbook,
// raise the arms as blue light gathers, vanish), then a 30-minute cooldown. Moving or being
// attacked interrupts it; it can't be started in combat.
u16 home_cast;

static int in_combat(void) {
    if (pl.action == ACT_COMBAT || foe_timer) return 1;
    for (int i = 0; i < npc_hi; i++)
        if (npcs[i].active && npcs[i].in_combat) return 1;
    return 0;
}

static int home_start(void) {
    if (pl.home_cd) {
        int mins = (pl.home_cd + 99) / 100;
        char buf[64], *p = buf;
        const char *a = "You need to wait another ";
        while (*a) *p++ = *a++;
        fmt_int(p, mins);
        while (*p) p++;
        a = mins == 1 ? " minute to cast this spell." : " minutes to cast this spell.";
        while (*a) *p++ = *a++;
        *p = 0;
        msg_col(buf, TC_RED);
        return 0;
    }
    if (in_combat()) {
        msg_col("You can't use this teleport while in combat.", TC_RED);
        return 0;
    }
    player_stop();
    pl.dir = DIR_DOWN;
    home_cast = 1;
    sfx(SFX_CAST);
    return 1;
}

void home_cancel(void) { home_cast = 0; }

// every world frame while casting
void home_update(void) {
    if (!home_cast) return;
    if (pl.moving || (keys_pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B))) {
        home_cast = 0;   // walked off or did something else
        return;
    }
    home_cast++;
    if (home_cast == 6 * TICK_FRAMES || home_cast == 14 * TICK_FRAMES) sfx(SFX_CAST);
    if (home_cast >= HOME_CAST_FRAMES) {
        home_cast = 0;
        pl.home_cd = HOME_COOLDOWN_TICKS;
        player_warp(START_X, START_Y, START_P, DIR_DOWN);
    }
}

int spell_cast_self(int s) {
    const SpellDef *sp = &spells[s];
    if (sp->kind != SPK_TELE || !spell_castable(s, 0)) return 0;
    if (!sp->level) return home_start();
    spell_pay(s);
    if (sp->xp) add_xp(SK_MAGIC, sp->xp);
    player_stop();
    sfx(SFX_CAST);
    switch (sp->param) {
    case 2: player_warp(TELE_VARROCK_X, TELE_VARROCK_Y, TELE_VARROCK_P, DIR_DOWN); break;
    case 3: player_warp(TELE_FALADOR_X, TELE_FALADOR_Y, TELE_FALADOR_P, DIR_DOWN); break;
    default: player_warp(START_X, START_Y, START_P, DIR_DOWN); break;
    }
    return 1;
}

// coins can't be alched, nor can quest items
int spell_on_item(int s) { return spells[s].kind == SPK_ALCH || spells[s].kind == SPK_HEAT; }

int spell_alch(int s, int slot) {
    const SpellDef *sp = &spells[s];
    Slot *it = &pl.inv[slot];
    if (sp->kind != SPK_ALCH || !it->id) return 0;
    if (it->id == I_COINS || shop_sell_price(SHOP_LUMBRIDGE, it->id) < 0) {
        msg("You can't cast this spell on that item.");
        sfx(SFX_ERR);
        return 0;
    }
    if (!spell_castable(s, 0)) return 0;
    int coins = items[it->id].value * sp->param / 100;
    if (!inv_count(I_COINS) && it->qty > 1 && !inv_free() && coins) {
        msg("You don't have enough inventory space.");
        return 0;
    }
    spell_pay(s);
    inv_remove(it->id, 1);
    if (coins) inv_add(I_COINS, coins);
    add_xp(SK_MAGIC, sp->xp);
    sfx(SFX_COINS);
    return 1;
}

int spell_autocast(int s) {
    const SpellDef *sp = &spells[s];
    if (sp->kind != SPK_COMBAT) {
        msg("You can only autocast combat spells.");
        return 0;
    }
    if (!(items[pl.equip[EQ_WEAPON]].flags & IF_MAGIC)) {
        msg("You need to wield a staff to autocast.");
        return 0;
    }
    if (skill_level(SK_MAGIC) < sp->level) return spell_castable(s, 0);
    pl.spell = s;
    char buf[48], *p = buf;
    const char *a = "Autocast: ", *n = sp->name;
    while (*a) *p++ = *a++;
    while (*n) *p++ = *n++;
    *p = 0;
    msg_col(buf, TC_CYAN);
    return 1;
}

// cast once at the L target, walking into range first; afterwards the player stands still
void spell_manual(int s) {
    if (!spell_castable(s, 0)) return;
    int t = lock_target;
    if (t < 0 || !npcs[t].active || !(npc_defs[npcs[t].kind].flags & NF_ATTACK)) {
        msg("Pick a target with L first.");
        sfx(SFX_ERR);
        return;
    }
    cast_once = s;
    player_walk_to(t, ACT_COMBAT);
}
