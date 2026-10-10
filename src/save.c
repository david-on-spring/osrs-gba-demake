// Battery-backed SRAM save. The "SRAM_V" marker lets emulators pick the right save type.
//
// Legacy layout at offset 0: magic (4) | version (2) | pad (2) | size (2) | pad (2) | Player |
// checksum (4). Version 2 added the neck slot and quest progress over version 1. Current saves
// use three character slots (version 3, below).
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

// ---------------------------------------------------------------------------------------
// Version 3: three character slots, each with the player's display name.
// Slot k at SLOT_BASE(k): magic (4) | version (2) | pad (2) | size (2) | pad (2) | name (16) |
// Player | checksum (4, over name + Player). The old single save at offset 0 (versions 1-2)
// is copied into slot 1 the first time the login screen sees it (and left in place).
#define SLOT_VERSION 3
#define SLOT_BASE(k) (0x1000 * ((k) + 1))
#define SLOT_NAME 12
#define SLOT_PLAYER 28

char player_name[NAME_MAX + 1] = "Adventurer";
static int cur_slot;

void save_set_slot(int k) { cur_slot = k; }
int save_slot(void) { return cur_slot; }

static u32 slot_sum(const char *name, const Player *p) {
    u32 h = checksum((const u8 *)name, 16);
    const u8 *b = (const u8 *)p;
    for (unsigned i = 0; i < sizeof(Player); i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static EWRAM_BSS Player slot_tmp;
static char slot_name_tmp[16];

// reads slot k into slot_tmp / slot_name_tmp; 1 if it holds a valid character
static int slot_read(int k) {
    int b = SLOT_BASE(k);
    u32 magic, sum;
    u16 ver, size;
    sram_read(b, &magic, 4);
    sram_read(b + 4, &ver, 2);
    sram_read(b + 8, &size, 2);
    if (magic != SAVE_MAGIC || ver != SLOT_VERSION || size != sizeof(Player)) return 0;
    sram_read(b + SLOT_NAME, slot_name_tmp, 16);
    sram_read(b + SLOT_PLAYER, &slot_tmp, sizeof(Player));
    sram_read(b + SLOT_PLAYER + sizeof(Player), &sum, 4);
    slot_name_tmp[15] = 0;
    return sum == slot_sum(slot_name_tmp, &slot_tmp);
}

static void slot_write(int k) {
    int b = SLOT_BASE(k);
    char name[16] = {0};
    for (int i = 0; i < NAME_MAX && player_name[i]; i++) name[i] = player_name[i];
    u32 magic = SAVE_MAGIC, sum = slot_sum(name, &pl);
    u16 ver = SLOT_VERSION, size = sizeof(Player);
    u32 zero = 0;
    sram_write(b, &zero, 4);                 // invalid while half written
    sram_write(b + 8, &size, 2);
    sram_write(b + SLOT_NAME, name, 16);
    sram_write(b + SLOT_PLAYER, &pl, sizeof(Player));
    sram_write(b + SLOT_PLAYER + sizeof(Player), &sum, 4);
    sram_write(b + 4, &ver, 2);
    sram_write(b, &magic, 4);
}

int save_slot_used(int k) { return slot_read(k); }

int save_slot_summary(int k, char *name, int *total, u32 *playtime) {
    if (!slot_read(k)) return 0;
    for (int i = 0; i <= NAME_MAX; i++) name[i] = slot_name_tmp[i];
    name[NAME_MAX] = 0;
    int t = 0;
    for (int i = 0; i < SK_COUNT; i++) t += level_for_xp(slot_tmp.xp[i]);
    *total = t;
    *playtime = slot_tmp.playtime;
    return 1;
}

static int legacy_load(void);

void save_migrate(void) {
    int any = 0;
    for (int k = 0; k < SAVE_SLOTS; k++) any |= slot_read(k);
    if (any || !legacy_load()) return;
    const char *d = "Adventurer";
    int i = 0;
    for (; d[i]; i++) player_name[i] = d[i];
    player_name[i] = 0;
    slot_write(0);
}

int save_exists(void) {
    for (int k = 0; k < SAVE_SLOTS; k++)
        if (slot_read(k)) return 1;
    return 0;
}

void save_game(void) { slot_write(cur_slot); }

int load_game(void) {
    if (!slot_read(cur_slot)) return 0;
    pl = slot_tmp;
    for (int i = 0; i <= NAME_MAX; i++) player_name[i] = slot_name_tmp[i];
    player_name[NAME_MAX] = 0;
    return 1;
}

// ---------------------------------------------------------------------------------------
// Legacy single save (versions 1 and 2), read only for migration
static int save_version(void) {
    u32 magic;
    u16 ver;
    sram_read(0, &magic, 4);
    sram_read(4, &ver, 2);
    (void)sram_tag;
    return magic == SAVE_MAGIC ? ver : 0;
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

static int legacy_load(void) {
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
