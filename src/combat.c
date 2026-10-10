// Combat: hit rolls, single-way combat lock, player and NPC attacks, drops and deaths.
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

// ticks between swings for the wielded weapon (OSRS speeds)
int weapon_speed(void) {
    switch (pl.equip[EQ_WEAPON]) {
    case I_BRONZE_AXE: case I_IRON_AXE: case I_STEEL_AXE: case I_MITHRIL_AXE: case I_ADAMANT_AXE:
    case I_RUNE_AXE: case I_BRONZE_PICK: case I_IRON_PICK: case I_STEEL_PICK: case I_MITHRIL_PICK:
    case I_ADAMANT_PICK: case I_RUNE_PICK: case I_IRON_MACE: case I_STEEL_LONGSWORD: case I_RUNE_LONGSWORD:
        return 5;
    default:
        return 4;   // unarmed, daggers, swords, scimitars
    }
}

void player_attack(int i) {
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    int eff_a = skill_level(SK_ATTACK) + style_bonus(SK_ATTACK) + 8;
    int eff_s = skill_level(SK_STRENGTH) + style_bonus(SK_STRENGTH) + 8;
    int a = eff_a * (equip_bonus(0) + 64);
    int def = (d->def + 9) * (d->defb + 64);
    int maxhit = (eff_s * (equip_bonus(1) + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = roll(hit_chance256(a, def)) ? rnd_range(maxhit + 1) : 0;
    if (n->kind == NPC_DELRITH && pl.equip[EQ_WEAPON] != I_SILVERLIGHT) {
        if (dmg) msg_col("Only Silverlight can harm the demon!", TC_ORANGE);
        dmg = 0;
    }
    if (dmg > n->hp) dmg = n->hp;
    foe_engage(i);
    pl.lunge = 8;
    n->hpbar_timer = 200;
    n->in_combat = 1;
    if (!n->attack_cd) n->attack_cd = 2;
    splat_spawn(n->px, n->py, dmg);
    sfx(dmg ? SFX_HIT : SFX_MISS);
    if (dmg) {
        int sk = pl.style == STYLE_ACCURATE ? SK_ATTACK : pl.style == STYLE_AGGRESSIVE ? SK_STRENGTH : SK_DEFENCE;
        add_xp(sk, dmg * 40);
        add_xp(SK_HITPOINTS, dmg * 40 / 3);
        n->hp -= dmg;
        if (!n->hp) npc_die(i);
    }
}

void npc_attack(int i) {
    Npc *n = &npcs[i];
    const NpcDef *d = &npc_defs[n->kind];
    int a = (d->att + 9) * (d->atkb + 64);
    int eff_d = skill_level(SK_DEFENCE) + style_bonus(SK_DEFENCE) + 8;
    int def = eff_d * (equip_bonus(2) + 64);
    int maxhit = ((d->str + 9) * (d->strb + 64) + 320) / 640;
    if (maxhit < 1) maxhit = 1;
    int dmg = roll(hit_chance256(a, def)) ? rnd_range(maxhit + 1) : 0;
    if (dmg > pl.hp) dmg = pl.hp;
    foe_engage(i);
    n->lunge = 8;
    pl.hpbar_timer = 200;
    splat_spawn(pl.px, pl.py, dmg);
    sfx(dmg ? SFX_HIT : SFX_MISS);
    pl.hp -= dmg;
    if (pl.action != ACT_COMBAT && pl.autoret && !pl.moving) {
        pl.action = ACT_COMBAT;
        pl.target = i;
        if (!pl.attack_cd) pl.attack_cd = 1;
        ui_set_target(i);
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
    pl.x = START_X;
    pl.y = START_Y;
    pl.px = pl.x * 16;
    pl.py = pl.y * 16;
    pl.moving = 0;
    pl.dir = DIR_DOWN;
    pl.hp = skill_level(SK_HITPOINTS);
    pl.hpbar_timer = 0;
}

static void npc_drops(Npc *n) {
    int x = n->x, y = n->y;
    switch (n->kind) {
    case NPC_COW:
        drop_item(x, y, I_BONES, 1);
        drop_item(x, y, I_RAW_BEEF, 1);
        drop_item(x, y, I_COWHIDE, 1);
        break;
    case NPC_CHICKEN:
        drop_item(x, y, I_BONES, 1);
        drop_item(x, y, I_RAW_CHICKEN, 1);
        drop_item(x, y, I_FEATHER, 5 + rnd_range(11));
        break;
    case NPC_GOBLIN: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 55) drop_item(x, y, I_COINS, 1 + rnd_range(15));
        else if (r < 63) drop_item(x, y, I_BRONZE_HELM, 1);
        else if (r < 70) drop_item(x, y, I_BRONZE_SHIELD, 1);
        else if (r < 75) drop_item(x, y, I_BRONZE_LEGS, 1);
        else if (r < 78) drop_item(x, y, I_LEATHER_BODY, 1);
        else if (r < 81) drop_item(x, y, I_BRONZE_DAGGER, 1);
        else if (r < 84) drop_item(x, y, I_BRONZE_FULL_HELM, 1);
        else if (r < 86) drop_item(x, y, I_BRONZE_CHAINBODY, 1);
        else if (r < 88) drop_item(x, y, I_BRONZE_SKIRT, 1);
        else if (r < 98) drop_item(x, y, I_GOBLIN_MAIL, 1);
        break;
    }
    case NPC_IMP: {
        static const u8 beads[4] = {I_RED_BEAD, I_YELLOW_BEAD, I_BLACK_BEAD, I_WHITE_BEAD};
        if (rnd_range(100) < 60) drop_item(x, y, beads[rnd_range(4)], 1);
        break;
    }
    case NPC_RAT:
        drop_item(x, y, I_BONES, 1);
        // the tail only drops while Hetty needs one (as in OSRS)
        if (quest_stage(Q_WITCH) == 1 && !has_item(I_RAT_TAIL)) drop_item(x, y, I_RAT_TAIL, 1);
        break;
    case NPC_SKELETON:
        drop_item(x, y, I_BONES, 1);
        break;
    case NPC_MAN:
    case NPC_WOMAN:
        drop_item(x, y, I_BONES, 1);
        if (rnd_range(100) < 70) drop_item(x, y, I_COINS, 3 + rnd_range(20));
        break;
    case NPC_WARRIOR:
    case NPC_DARK_WIZARD:
    case NPC_BARBARIAN: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 50) drop_item(x, y, I_COINS, 5 + rnd_range(25));
        else if (r < 58) drop_item(x, y, I_BRONZE_SWORD, 1);
        else if (r < 64) drop_item(x, y, I_IRON_HELM, 1);
        else if (r < 68) drop_item(x, y, I_IRON_SWORD, 1);
        break;
    }
    case NPC_GUARD: {
        drop_item(x, y, I_BONES, 1);
        int r = rnd_range(100);
        if (r < 60) drop_item(x, y, I_COINS, 10 + rnd_range(50));
        else if (r < 70) drop_item(x, y, I_IRON_HELM, 1);
        else if (r < 78) drop_item(x, y, I_IRON_SWORD, 1);
        else if (r < 84) drop_item(x, y, I_IRON_LEGS, 1);
        else if (r < 88) drop_item(x, y, I_STEEL_SWORD, 1);
        else if (r < 91) drop_item(x, y, I_IRON_BODY, 1);
        else if (r < 93) drop_item(x, y, I_IRON_SCIMITAR, 1);
        else if (r < 95) drop_item(x, y, I_IRON_KITE, 1);
        else if (r < 96) drop_item(x, y, I_IRON_FULL_HELM, 1);
        else if (r < 97) drop_item(x, y, I_IRON_CHAINBODY, 1);
        else if (r < 98) drop_item(x, y, I_STEEL_LONGSWORD, 1);
        else if (r < 99) {
            // rare table: a taste of the higher tiers
            static const u8 rare[] = {I_MITHRIL_SCIMITAR, I_MITHRIL_FULL_HELM, I_MITHRIL_KITE,
                                      I_STEEL_BODY, I_STEEL_LEGS, I_ADAMANT_SCIMITAR, I_MITHRIL_BODY,
                                      I_RUNE_SCIMITAR};
            drop_item(x, y, rare[rnd_range(sizeof(rare))], 1);
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
    occ_clear(n->x, n->y);
    n->in_combat = 0;
    foe_release(i);
    n->respawn = npc_defs[n->kind].respawn;
    if (pl.target == i && pl.action == ACT_COMBAT) player_stop();
}
