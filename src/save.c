// Battery-backed SRAM save. The "SRAM_V" marker lets emulators pick the right save type.
//
// Layout: magic (4) | version (2) | pad (2) | size (2) | pad (2) | Player | checksum (4).
// Version 2 added the neck slot and quest progress; version 1 saves are migrated on load.
#include "game.h"

__attribute__((used, aligned(4))) static const char sram_tag[] = "SRAM_V113";

#define SAVE_MAGIC 0x37305352u   // "RS07"
#define SAVE_VERSION 2

// The version 1 Player (before quests): six equipment slots, no quest state.
typedef struct {
    s16 x, y, px, py;
    u8 dir, moving, step, anim;
    u8 action, action_timer, lunge, turn_delay;
    s16 tx, ty;
    s16 target;
    u8 hp, style, autoret, attack_cd;
    u8 cook_item, hpbar_timer, run, music;
    u32 xp[SK_COUNT];
    Slot inv[28];
    u16 equip[6];
    Slot bank[96];
    u32 playtime;
} PlayerV1;

static void sram_write(int off, const void *src, int n) {
    const u8 *s = src;
    for (int i = 0; i < n; i++) SRAM[off + i] = s[i];
}
static void sram_read(int off, void *dst, int n) {
    u8 *d = dst;
    for (int i = 0; i < n; i++) d[i] = SRAM[off + i];
}

static u32 checksum(const u8 *p, int n) {
    u32 h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int save_version(void) {
    u32 magic;
    u16 ver;
    sram_read(0, &magic, 4);
    sram_read(4, &ver, 2);
    (void)sram_tag;
    return magic == SAVE_MAGIC ? ver : 0;
}

int save_exists(void) {
    int v = save_version();
    return v == 1 || v == SAVE_VERSION;
}

void save_game(void) {
    u32 magic = SAVE_MAGIC, sum = checksum((const u8 *)&pl, sizeof(pl));
    u16 ver = SAVE_VERSION, size = sizeof(pl);
    sram_write(8, &size, 2);
    sram_write(12, &pl, sizeof(pl));
    sram_write(12 + sizeof(pl), &sum, 4);
    sram_write(4, &ver, 2);
    sram_write(0, &magic, 4);
}

static EWRAM_BSS union { Player v2; PlayerV1 v1; } tmp;

static int read_checked(int size) {
    u16 stored;
    u32 sum;
    sram_read(8, &stored, 2);
    if (stored != size) return 0;
    sram_read(12, &tmp, size);
    sram_read(12 + size, &sum, 4);
    return sum == checksum((const u8 *)&tmp, size);
}

int load_game(void) {
    int v = save_version();
    if (v == SAVE_VERSION) {
        if (!read_checked(sizeof(Player))) return 0;
        pl = tmp.v2;
        return 1;
    }
    if (v == 1) {
        // migrate: same fields, one more equipment slot, quests not started
        if (!read_checked(sizeof(PlayerV1))) return 0;
        PlayerV1 o = tmp.v1;
        Player *p = &pl;
        p->x = o.x, p->y = o.y, p->px = o.px, p->py = o.py;
        p->dir = o.dir, p->moving = 0, p->step = o.step, p->anim = 0;
        p->action = ACT_NONE, p->action_timer = 0, p->lunge = 0, p->turn_delay = 0;
        p->tx = o.tx, p->ty = o.ty, p->target = o.target;
        p->hp = o.hp, p->style = o.style, p->autoret = o.autoret, p->attack_cd = 0;
        p->cook_item = o.cook_item, p->hpbar_timer = 0, p->run = o.run, p->music = o.music;
        for (int i = 0; i < SK_COUNT; i++) p->xp[i] = o.xp[i];
        for (int i = 0; i < 28; i++) p->inv[i] = o.inv[i];
        for (int i = 0; i < EQ_COUNT; i++) p->equip[i] = i < 6 ? o.equip[i] : 0;
        for (int i = 0; i < 96; i++) p->bank[i] = o.bank[i];
        p->playtime = o.playtime;
        quest_new_game();
        return 1;
    }
    return 0;
}
