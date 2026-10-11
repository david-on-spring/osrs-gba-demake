// Battery-backed SRAM save. The "SRAM_V" marker lets emulators pick the right save type.
//
// Legacy layout at offset 0: magic (4) | version (2) | pad (2) | size (2) | pad (2) | Player |
// checksum (4). Version 2 added the neck slot and quest progress over version 1. Current saves
// use three character slots (versions 3 to 7, below); version 4 added Ranged, Magic, the ammo
// slot and the autocast spell, 5 the seamless 1:1 world, 6 stores OSRS tiles (x, y, plane)
// instead of cells of the generator's grid, and 7 added Smithing, thrown weapons and the hands and
// ring slots.
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
    u32 xp[10];
    Slot inv[28];
    u16 equip[6];
    Slot bank[96];
    u32 playtime;
} PlayerV1;

// The Player of legacy version 2 and slot version 3: ten skills, seven equipment slots.
typedef struct {
    s16 x, y, px, py;
    u8 dir, moving, step, anim;
    u8 action, action_timer, lunge, turn_delay;
    s16 tx, ty;
    s16 target;
    u8 hp, style, autoret, attack_cd;
    u8 cook_item, hpbar_timer, run, music;
    u32 xp[10];
    Slot inv[28];
    u16 equip[7];
    Slot bank[96];
    u32 playtime;
    u8 qstage[16];
    u16 qvar[16];
} PlayerV3;

// The Player of slot versions 4 to 6: twelve skills (before Smithing), otherwise as now.
typedef struct {
    s16 x, y, px, py;
    u8 dir, moving, step, anim;
    u8 action, action_timer, lunge, turn_delay;
    s16 tx, ty;
    s16 target;
    u8 hp, style, autoret, attack_cd;
    u8 cook_item, hpbar_timer, run, music;
    u32 xp[12];
    Slot inv[28];
    u16 equip[8];
    u16 ammo_qty;
    Slot bank[96];
    u32 playtime;
    u8 qstage[16];
    u16 qvar[16];
    u8 spell, plane;
    u16 home_cd;
} PlayerV6;

// tools/qtest.py pokes these offsets directly
_Static_assert(__builtin_offsetof(Player, x) == 0 && __builtin_offsetof(Player, px) == 4, "qtest.py tp_lines");
_Static_assert(__builtin_offsetof(Player, plane) == 0x291, "qtest.py OFF_PLANE");
_Static_assert(__builtin_offsetof(Player, xp) == 0x20, "qtest.py OFF_XP");
_Static_assert(__builtin_offsetof(Player, inv) == 0x54, "qtest.py OFF_INV");
_Static_assert(__builtin_offsetof(Player, equip) == 0xC4, "qtest.py OFF_EQUIP");
_Static_assert(__builtin_offsetof(Player, ammo_qty) == 0xD8, "qtest.py OFF_AMMO");
_Static_assert(__builtin_offsetof(Player, bank) == 0xDA, "qtest.py OFF_BANK");
_Static_assert(__builtin_offsetof(Player, qstage) == 0x260, "qtest.py OFF_QSTAGE");
_Static_assert(__builtin_offsetof(Player, qvar) == 0x270, "qtest.py OFF_QVAR");
_Static_assert(__builtin_offsetof(Player, spell) == 0x290, "qtest.py OFF_SPELL");
_Static_assert(__builtin_offsetof(Player, home_cd) == 0x292, "qtest.py OFF_HOMECD");
_Static_assert(__builtin_offsetof(Player, weapon_qty) == 0x294, "qtest.py OFF_WQTY");

// a version 4-6 character in the current layout: Smithing starts at level 1; no thrown weapons, and
// nothing on the hands or ring fingers
static void from_v6(const PlayerV6 *o, Player *p) {
    const u8 *src = (const u8 *)o;
    u8 *dst = (u8 *)p;
    for (unsigned i = 0; i < __builtin_offsetof(PlayerV6, xp); i++) dst[i] = src[i];   // position, flags
    for (int i = 0; i < SK_COUNT; i++) p->xp[i] = i < 12 ? o->xp[i] : 0;
    for (int i = 0; i < 28; i++) p->inv[i] = o->inv[i];
    for (int i = 0; i < EQ_COUNT; i++) p->equip[i] = i < 8 ? o->equip[i] : 0;
    p->ammo_qty = o->ammo_qty;
    for (int i = 0; i < 96; i++) p->bank[i] = o->bank[i];
    p->playtime = o->playtime;
    for (int i = 0; i < 16; i++) p->qstage[i] = o->qstage[i], p->qvar[i] = o->qvar[i];
    p->spell = o->spell;
    p->plane = o->plane;
    p->home_cd = o->home_cd;
    p->weapon_qty = 0;
    p->forging = 0;
}

// a version 3 character in the current layout: new skills at level 1, nothing in the ammo slot
static void from_v3(const PlayerV3 *o) {
    Player *p = &pl;
    const u8 *src = (const u8 *)o;
    u8 *dst = (u8 *)p;
    for (unsigned i = 0; i < __builtin_offsetof(PlayerV3, xp); i++) dst[i] = src[i];   // position, flags
    for (int i = 0; i < SK_COUNT; i++) p->xp[i] = i < 10 ? o->xp[i] : 0;
    for (int i = 0; i < 28; i++) p->inv[i] = o->inv[i];
    for (int i = 0; i < EQ_COUNT; i++) p->equip[i] = i < 7 ? o->equip[i] : 0;
    p->ammo_qty = 0;
    for (int i = 0; i < 96; i++) p->bank[i] = o->bank[i];
    p->playtime = o->playtime;
    for (int i = 0; i < 16; i++) p->qstage[i] = o->qstage[i], p->qvar[i] = o->qvar[i];
    p->spell = 0;
    p->plane = 0;
    p->home_cd = 0;
    p->weapon_qty = 0;
    p->forging = 0;
}

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
#define SLOT_VERSION 7             // 7: Smithing, thrown weapons, hands and ring; 6: positions are OSRS tiles (x, y, plane); 5 stored cells of the 1:1 grid
#define SLOT_BASE(k) (0x1000 * ((k) + 1))
#define SLOT_NAME 12
#define SLOT_PLAYER 28

char player_name[NAME_MAX + 1] = "Adventurer";
static int cur_slot;

void save_set_slot(int k) { cur_slot = k; }
int save_slot(void) { return cur_slot; }

static u32 slot_sum(const char *name, const void *p, int size) {
    u32 h = checksum((const u8 *)name, 16);
    const u8 *b = (const u8 *)p;
    for (int i = 0; i < size; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

// Version 5 stored the position as a cell of the 416x460 grid of the seamless world: the
// overworld at cells (0..415, 0..415) from OSRS (2928, 3551), and the castle's first and second
// floors at rows 420.. (columns 0..31 and 36..67) from OSRS (3195, 3236). Fixed here, whatever
// the map later becomes.
static void from_v5_cell(s16 *x, s16 *y, u8 *plane) {
    int cx = *x, cy = *y;
    if (cx >= 0 && cx < 416 && cy >= 0 && cy < 416) {
        *x = 2928 + cx, *y = 3551 - cy, *plane = 0;
    } else if (cy >= 420 && cy <= 456 && cx >= 0 && cx <= 67 && (cx <= 31 || cx >= 36)) {
        *plane = cx <= 31 ? 1 : 2;
        *x = 3195 + (cx <= 31 ? cx : cx - 36), *y = 3236 - (cy - 420);
    } else {
        *x = START_X, *y = START_Y, *plane = START_P;
    }
}

static EWRAM_BSS Player slot_tmp;
static EWRAM_BSS PlayerV3 slot_v3;
static EWRAM_BSS PlayerV6 slot_v6;
static char slot_name_tmp[16];

// reads slot k into slot_tmp / slot_name_tmp (a version 3 slot is converted on the way);
// 1 if it holds a valid character
static int slot_read(int k) {
    int b = SLOT_BASE(k);
    u32 magic, sum;
    u16 ver, size;
    sram_read(b, &magic, 4);
    sram_read(b + 4, &ver, 2);
    sram_read(b + 8, &size, 2);
    if (magic != SAVE_MAGIC) return 0;
    int v3 = ver == 3 && size == sizeof(PlayerV3);
    int v6 = ver >= 4 && ver <= 6 && size == sizeof(PlayerV6);
    if (!v3 && !v6 && (ver != SLOT_VERSION || size != sizeof(Player))) return 0;
    void *dst = v3 ? (void *)&slot_v3 : v6 ? (void *)&slot_v6 : (void *)&slot_tmp;
    sram_read(b + SLOT_NAME, slot_name_tmp, 16);
    sram_read(b + SLOT_PLAYER, dst, size);
    sram_read(b + SLOT_PLAYER + size, &sum, 4);
    slot_name_tmp[15] = 0;
    if (sum != slot_sum(slot_name_tmp, dst, size)) return 0;
    if (v3) {
        Player keep = pl;
        from_v3(&slot_v3);
        slot_tmp = pl;
        pl = keep;
    }
    if (v6) from_v6(&slot_v6, &slot_tmp);
    if (ver == 5) {                      // same layout, positions in cells of the version 5 grid
        from_v5_cell(&slot_tmp.x, &slot_tmp.y, &slot_tmp.plane);
        u8 tp;
        from_v5_cell(&slot_tmp.tx, &slot_tmp.ty, &tp);
    } else if (ver < 5) {                // saved on an older map layout
        slot_tmp.x = START_X;
        slot_tmp.y = START_Y;
        slot_tmp.plane = START_P;
    }
    return 1;
}

static void slot_write(int k) {
    int b = SLOT_BASE(k);
    char name[16] = {0};
    for (int i = 0; i < NAME_MAX && player_name[i]; i++) name[i] = player_name[i];
    u32 magic = SAVE_MAGIC, sum = slot_sum(name, &pl, sizeof(Player));
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
    pl.x = START_X;                      // the old map layout
    pl.y = START_Y;
    pl.plane = START_P;
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


static EWRAM_BSS union { PlayerV3 v2; PlayerV1 v1; } tmp;

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
        if (!read_checked(sizeof(PlayerV3))) return 0;
        from_v3(&tmp.v2);
        return 1;
    }
    if (v == 1) {
        // migrate: same fields, one more equipment slot, quests not started
        if (!read_checked(sizeof(PlayerV1))) return 0;
        PlayerV1 o = tmp.v1;
        PlayerV3 *p = &slot_v3;
        p->x = o.x, p->y = o.y, p->px = o.px, p->py = o.py;
        p->dir = o.dir, p->moving = 0, p->step = o.step, p->anim = 0;
        p->action = ACT_NONE, p->action_timer = 0, p->lunge = 0, p->turn_delay = 0;
        p->tx = o.tx, p->ty = o.ty, p->target = o.target;
        p->hp = o.hp, p->style = o.style, p->autoret = o.autoret, p->attack_cd = 0;
        p->cook_item = o.cook_item, p->hpbar_timer = 0, p->run = o.run, p->music = o.music;
        for (int i = 0; i < 10; i++) p->xp[i] = o.xp[i];
        for (int i = 0; i < 28; i++) p->inv[i] = o.inv[i];
        for (int i = 0; i < 7; i++) p->equip[i] = i < 6 ? o.equip[i] : 0;
        for (int i = 0; i < 96; i++) p->bank[i] = o.bank[i];
        p->playtime = o.playtime;
        from_v3(p);
        quest_new_game();
        return 1;
    }
    return 0;
}
