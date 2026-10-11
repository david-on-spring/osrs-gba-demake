// Player avatar: composites body, armour, shield and held item layers (tools/gen_avatar.py)
// into a 32x32 4bpp sprite whenever the player's look or pose changes. The result is uploaded
// during VBlank to one of two dynamic OBJ tile blocks (world sprite, menu portrait/paper-doll).
// Everything uses the player palette bank, recoloured by metal tier in avatar_palette().
#include "game.h"
#include "avatar_art.h"

#define AV_SZ 32
#define AV_OX 8            // the 16x16 human cell sits at (8,12) inside the 32x32 canvas
#define AV_OY 12
#define AV_TILE(b) (DYN_TILE_BASE + 16 + (b) * 16)

// ---------------------------------------------------------------------------------------
// Item looks: which layer set an equippable item uses (value + 1; 0 = slot default)
#define LK(v) ((v) + 1)
static const u8 look_tab[I_COUNT] = {
    [I_BRONZE_AXE] = LK(AVW_AXE), [I_IRON_AXE] = LK(AVW_AXE), [I_STEEL_AXE] = LK(AVW_AXE),
    [I_MITHRIL_AXE] = LK(AVW_AXE), [I_ADAMANT_AXE] = LK(AVW_AXE), [I_RUNE_AXE] = LK(AVW_AXE),
    [I_BRONZE_PICK] = LK(AVW_PICK), [I_IRON_PICK] = LK(AVW_PICK), [I_STEEL_PICK] = LK(AVW_PICK),
    [I_MITHRIL_PICK] = LK(AVW_PICK), [I_ADAMANT_PICK] = LK(AVW_PICK), [I_RUNE_PICK] = LK(AVW_PICK),
    [I_NET] = LK(AVW_NET), [I_ROD] = LK(AVW_ROD),
    [I_BRONZE_SWORD] = LK(AVW_SWORD), [I_SILVERLIGHT] = LK(AVW_SWORD), [I_IRON_SWORD] = LK(AVW_SWORD), [I_STEEL_SWORD] = LK(AVW_SWORD),
    [I_BRONZE_DAGGER] = LK(AVW_DAGGER), [I_IRON_MACE] = LK(AVW_MACE),
    [I_IRON_SCIMITAR] = LK(AVW_SCIM), [I_MITHRIL_SCIMITAR] = LK(AVW_SCIM),
    [I_ADAMANT_SCIMITAR] = LK(AVW_SCIM), [I_RUNE_SCIMITAR] = LK(AVW_SCIM),
    [I_STEEL_LONGSWORD] = LK(AVW_LONG), [I_RUNE_LONGSWORD] = LK(AVW_LONG),
    [I_BRONZE_HELM] = LK(AVH_MED), [I_IRON_HELM] = LK(AVH_MED), [I_STEEL_HELM] = LK(AVH_MED),
    [I_BRONZE_FULL_HELM] = LK(AVH_FULL), [I_IRON_FULL_HELM] = LK(AVH_FULL),
    [I_MITHRIL_FULL_HELM] = LK(AVH_FULL), [I_ADAMANT_FULL_HELM] = LK(AVH_FULL),
    [I_RUNE_FULL_HELM] = LK(AVH_FULL),
    [I_LEATHER_BODY] = LK(AVT_LEATHER), [I_BRONZE_BODY] = LK(AVT_PLATE), [I_IRON_BODY] = LK(AVT_PLATE),
    [I_STEEL_BODY] = LK(AVT_PLATE), [I_MITHRIL_BODY] = LK(AVT_PLATE), [I_ADAMANT_BODY] = LK(AVT_PLATE),
    [I_RUNE_BODY] = LK(AVT_PLATE), [I_BRONZE_CHAINBODY] = LK(AVT_CHAIN), [I_IRON_CHAINBODY] = LK(AVT_CHAIN),
    [I_LEATHER_CHAPS] = LK(AVG_CHAPS), [I_BRONZE_LEGS] = LK(AVG_PLATE), [I_IRON_LEGS] = LK(AVG_PLATE),
    [I_STEEL_LEGS] = LK(AVG_PLATE), [I_MITHRIL_LEGS] = LK(AVG_PLATE), [I_ADAMANT_LEGS] = LK(AVG_PLATE),
    [I_RUNE_LEGS] = LK(AVG_PLATE), [I_BRONZE_SKIRT] = LK(AVG_SKIRT), [I_IRON_SKIRT] = LK(AVG_SKIRT),
    [I_WOODEN_SHIELD] = LK(AVS_ROUND), [I_BRONZE_SHIELD] = LK(AVS_SQ), [I_IRON_SHIELD] = LK(AVS_SQ),
    [I_STEEL_SHIELD] = LK(AVS_SQ), [I_IRON_KITE] = LK(AVS_KITE), [I_MITHRIL_KITE] = LK(AVS_KITE),
    [I_ADAMANT_KITE] = LK(AVS_KITE), [I_RUNE_KITE] = LK(AVS_KITE),
    [I_SHORTBOW] = LK(AVW_BOW), [I_LONGBOW] = LK(AVW_BOW), [I_OAK_SHORTBOW] = LK(AVW_BOW),
    [I_OAK_LONGBOW] = LK(AVW_BOW), [I_WILLOW_SHORTBOW] = LK(AVW_BOW), [I_WILLOW_LONGBOW] = LK(AVW_BOW),
    [I_MAPLE_SHORTBOW] = LK(AVW_BOW), [I_MAPLE_LONGBOW] = LK(AVW_BOW),
    [I_STAFF] = LK(AVW_STAFF), [I_MAGIC_STAFF] = LK(AVW_STAFF), [I_STAFF_AIR] = LK(AVW_STAFF),
    [I_STAFF_WATER] = LK(AVW_STAFF), [I_STAFF_EARTH] = LK(AVW_STAFF), [I_STAFF_FIRE] = LK(AVW_STAFF),
    [I_HAMMER] = LK(AVW_MACE),   // held while smithing
    [I_BRONZE_KNIFE] = LK(AVW_DAGGER), [I_IRON_KNIFE] = LK(AVW_DAGGER), [I_STEEL_KNIFE] = LK(AVW_DAGGER),
    [I_MITHRIL_KNIFE] = LK(AVW_DAGGER), [I_ADAMANT_KNIFE] = LK(AVW_DAGGER), [I_RUNE_KNIFE] = LK(AVW_DAGGER),
    // smithing: 2h swords show as longswords, battleaxes as axes, warhammers as maces, claws as daggers
    [I_BRONZE_SCIMITAR] = LK(AVW_SCIM), [I_BRONZE_LONGSWORD] = LK(AVW_LONG), [I_BRONZE_2H] = LK(AVW_LONG),
    [I_BRONZE_MACE] = LK(AVW_MACE), [I_BRONZE_WARHAMMER] = LK(AVW_MACE), [I_BRONZE_BATTLEAXE] = LK(AVW_AXE),
    [I_BRONZE_CLAWS] = LK(AVW_DAGGER), [I_BRONZE_KITE] = LK(AVS_KITE), [I_IRON_DAGGER] = LK(AVW_DAGGER),
    [I_IRON_LONGSWORD] = LK(AVW_LONG), [I_IRON_2H] = LK(AVW_LONG), [I_IRON_WARHAMMER] = LK(AVW_MACE),
    [I_IRON_BATTLEAXE] = LK(AVW_AXE), [I_IRON_CLAWS] = LK(AVW_DAGGER), [I_STEEL_DAGGER] = LK(AVW_DAGGER),
    [I_STEEL_SCIMITAR] = LK(AVW_SCIM), [I_STEEL_2H] = LK(AVW_LONG), [I_STEEL_MACE] = LK(AVW_MACE),
    [I_STEEL_WARHAMMER] = LK(AVW_MACE), [I_STEEL_BATTLEAXE] = LK(AVW_AXE), [I_STEEL_CLAWS] = LK(AVW_DAGGER),
    [I_STEEL_CHAINBODY] = LK(AVT_CHAIN), [I_STEEL_SKIRT] = LK(AVG_SKIRT), [I_STEEL_FULL_HELM] = LK(AVH_FULL),
    [I_STEEL_KITE] = LK(AVS_KITE), [I_MITHRIL_DAGGER] = LK(AVW_DAGGER), [I_MITHRIL_SWORD] = LK(AVW_SWORD),
    [I_MITHRIL_LONGSWORD] = LK(AVW_LONG), [I_MITHRIL_2H] = LK(AVW_LONG), [I_MITHRIL_MACE] = LK(AVW_MACE),
    [I_MITHRIL_WARHAMMER] = LK(AVW_MACE), [I_MITHRIL_BATTLEAXE] = LK(AVW_AXE),
    [I_MITHRIL_CLAWS] = LK(AVW_DAGGER), [I_MITHRIL_CHAINBODY] = LK(AVT_CHAIN),
    [I_MITHRIL_SKIRT] = LK(AVG_SKIRT), [I_MITHRIL_HELM] = LK(AVH_MED), [I_MITHRIL_SHIELD] = LK(AVS_SQ),
    [I_ADAMANT_DAGGER] = LK(AVW_DAGGER), [I_ADAMANT_SWORD] = LK(AVW_SWORD),
    [I_ADAMANT_LONGSWORD] = LK(AVW_LONG), [I_ADAMANT_2H] = LK(AVW_LONG), [I_ADAMANT_MACE] = LK(AVW_MACE),
    [I_ADAMANT_WARHAMMER] = LK(AVW_MACE), [I_ADAMANT_BATTLEAXE] = LK(AVW_AXE),
    [I_ADAMANT_CLAWS] = LK(AVW_DAGGER), [I_ADAMANT_CHAINBODY] = LK(AVT_CHAIN),
    [I_ADAMANT_SKIRT] = LK(AVG_SKIRT), [I_ADAMANT_HELM] = LK(AVH_MED), [I_ADAMANT_SHIELD] = LK(AVS_SQ),
    [I_RUNE_DAGGER] = LK(AVW_DAGGER), [I_RUNE_SWORD] = LK(AVW_SWORD), [I_RUNE_2H] = LK(AVW_LONG),
    [I_RUNE_MACE] = LK(AVW_MACE), [I_RUNE_WARHAMMER] = LK(AVW_MACE), [I_RUNE_BATTLEAXE] = LK(AVW_AXE),
    [I_RUNE_CLAWS] = LK(AVW_DAGGER), [I_RUNE_CHAINBODY] = LK(AVT_CHAIN), [I_RUNE_SKIRT] = LK(AVG_SKIRT),
    [I_RUNE_HELM] = LK(AVH_MED), [I_RUNE_SHIELD] = LK(AVS_SQ),
};

static int look_of(int id, int def) { return look_tab[id] ? look_tab[id] - 1 : def; }

// ---------------------------------------------------------------------------------------
// Palette: tier colours (light, dark) in BGR555
static inline u16 rgb(int r, int g, int b) { return r | (g << 5) | (b << 10); }
static const u16 tier_light[TIER_COUNT] = {
    [TIER_BRONZE] = 0x2A9C, [TIER_IRON] = 0x5294, [TIER_STEEL] = 0x6B39, [TIER_LEATHER] = 0x1D71,
    [TIER_WOOD] = 0x21D4, [TIER_MITHRIL] = 0x594B, [TIER_ADAMANT] = 0x29E9, [TIER_RUNE] = 0x5E4A,
};
static const u16 tier_dark[TIER_COUNT] = {
    [TIER_BRONZE] = 0x1593, [TIER_IRON] = 0x2D6B, [TIER_STEEL] = 0x41CD, [TIER_LEATHER] = 0x0CEB,
    [TIER_WOOD] = 0x110D, [TIER_MITHRIL] = 0x34A6, [TIER_ADAMANT] = 0x1525, [TIER_RUNE] = 0x3966,
};

static u16 shown_held = 0xFFFF;   // item whose colours are in slots 12/13 (tool or weapon)
static u16 world_held;            // item the world sprite currently shows in hand
#define HELD_BOOK 0xFFFE          // world_held while reading the home teleport's spellbook

void avatar_palette(u16 *p) {
    for (int i = 0; i < 16; i++) p[i] = obj_pal[i];
    int t;
    if ((t = items[pl.equip[EQ_HEAD]].tier)) p[4] = tier_light[t], p[5] = tier_dark[t];
    if ((t = items[pl.equip[EQ_BODY]].tier)) p[6] = tier_light[t], p[7] = tier_dark[t];
    if ((t = items[pl.equip[EQ_LEGS]].tier)) p[8] = tier_light[t], p[9] = tier_dark[t];
    if ((t = items[pl.equip[EQ_FEET]].tier)) p[10] = t == TIER_LEATHER ? 0x1531 : tier_dark[t];
    if ((t = items[pl.equip[EQ_SHIELD]].tier)) p[14] = tier_light[t], p[15] = tier_dark[t];
    int held = world_held;
    if (held == HELD_BOOK) p[12] = rgb(10, 18, 30), p[13] = rgb(4, 7, 18);
    else if (held == I_NET) p[12] = rgb(27, 26, 22), p[13] = rgb(16, 13, 8);
    else if (held == I_ROD) p[12] = rgb(28, 28, 28), p[13] = rgb(14, 14, 15);
    else if (held == I_STAFF_AIR) p[12] = rgb(28, 28, 30), p[13] = tier_dark[TIER_WOOD];   // orb colours
    else if (held == I_STAFF_WATER) p[12] = rgb(8, 14, 30), p[13] = tier_dark[TIER_WOOD];
    else if (held == I_STAFF_EARTH) p[12] = rgb(10, 22, 8), p[13] = tier_dark[TIER_WOOD];
    else if (held == I_STAFF_FIRE) p[12] = rgb(31, 12, 4), p[13] = tier_dark[TIER_WOOD];
    else if (held == I_MAGIC_STAFF) p[12] = rgb(20, 10, 28), p[13] = tier_dark[TIER_WOOD];
    else if (held == I_HAMMER) p[12] = tier_light[TIER_IRON], p[13] = tier_dark[TIER_WOOD];   // iron head, wooden haft
    else if (held < I_COUNT && (t = items[held].tier)) p[12] = tier_light[t], p[13] = tier_dark[t];
    shown_held = held;
}

// ---------------------------------------------------------------------------------------
// Compositing
typedef struct {
    u8 view, left, legf, legflip;
    u8 head, torso, legs, boots, shield, wtype;   // 0xFF = none for shield/wtype
    s8 hx, hy;                                     // held-item hand
    u8 ang, wflags, line, pad;
} Look;
enum { WF_BACK = 1, WF_FLIP = 2, WF_HIDE = 4 };
enum { V_DOWN, V_UP, V_SIDE };

static u8 cv[AV_SZ * AV_SZ];
static u32 tilebuf[2][16 * 8];
static u8 pending[2];
static Look last[2];
static u8 valid[2];

static void blit(int id, int ox, int oy, int flip) {
    const AvLayer *L = &av_layers[id];
    const u8 *src = &av_px[L->off];
    for (int y = 0; y < L->h; y++) {
        int cy = oy + y;
        if (cy < 0 || cy >= AV_SZ) {
            src += L->w;
            continue;
        }
        u8 *row = &cv[cy * AV_SZ];
        for (int x = 0; x < L->w; x++) {
            u8 v = *src++;
            if (!v) continue;
            int cx = ox + (flip ? L->w - 1 - x : x);
            if ((unsigned)cx < AV_SZ) row[cx] = v;
        }
    }
}

// body-part layer at its authored place in the human cell (optionally mirrored about the cell)
static void part(int id, int flip) {
    const AvLayer *L = &av_layers[id];
    int x = flip ? 16 - L->x - L->w : L->x;
    blit(id, AV_OX + x, AV_OY + L->y, flip);
}

static void held(const Look *k) {
    int id = av_wpn[k->wtype][k->ang];
    const AvLayer *L = &av_layers[id];
    int flip = k->wflags & WF_FLIP;
    int x = flip ? k->hx - (L->x + L->w - 1) : k->hx + L->x;
    blit(id, AV_OX + x, AV_OY + k->hy + L->y, flip);
}

static void pset(int x, int y, int v) {
    x += AV_OX;
    y += AV_OY;
    if ((unsigned)x < AV_SZ && (unsigned)y < AV_SZ) cv[y * AV_SZ + x] = v;
}
static int pget(int x, int y) {
    x += AV_OX;
    y += AV_OY;
    return ((unsigned)x < AV_SZ && (unsigned)y < AV_SZ) ? cv[y * AV_SZ + x] : 0;
}

// procedural arm: shoulder -> hand line, sleeve then skin, outlined into empty pixels only
static void arm(int sx, int sy, int hx, int hy, int sleeve) {
    s8 px[12][2];
    int n = 0;
    int dx = hx - sx, dy = hy - sy;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    for (int i = 0; i <= steps && n < 12; i++) {
        int x = sx + (steps ? (dx * i * 2 + (dx < 0 ? -steps : steps)) / (2 * steps) : 0);
        int y = sy + (steps ? (dy * i * 2 + (dy < 0 ? -steps : steps)) / (2 * steps) : 0);
        px[n][0] = x;
        px[n][1] = y;
        n++;
    }
    for (int i = 0; i < n; i++) {
        int x = px[i][0], y = px[i][1], under = pget(x, y), c;
        if (i == n - 1) c = 2;
        else if (i < sleeve) c = under == 6 ? 7 : 6;
        else c = under == 2 ? 3 : 2;
        pset(x, y, c);
    }
    for (int i = 0; i < n; i++)
        for (int d = 0; d < 4; d++) {
            int x = px[i][0] + (d == 0) - (d == 1), y = px[i][1] + (d == 2) - (d == 3);
            if (!pget(x, y)) pset(x, y, 1);
        }
}

static void compose(const Look *k) {
    for (int i = 0; i < AV_SZ * AV_SZ / 4; i++) ((u32 *)cv)[i] = 0;
    int v = k->view;
    int has_w = k->wtype != 0xFF && !(k->wflags & WF_HIDE);
    int wback = has_w && (k->wflags & WF_BACK);
    int sleeve = k->torso == AVT_SHIRT ? 1 : k->torso == AVT_LEATHER ? 2 : 6;
    // arms: weapon arm shoulder and the off arm (shield) rest hand
    int wsx, wsy = 9, osx, osy = 9, ohx, ohy = 11;
    if (v == V_DOWN) wsx = 4, osx = 11, ohx = 11;
    else if (v == V_UP) wsx = 11, osx = 4, ohx = 4;
    else wsx = 7, osx = 7, ohx = 8;
    int far_w = v == V_SIDE && k->left;            // weapon in the far hand (facing left)

    // behind the body
    if (k->shield != 0xFF) {
        if (v == V_UP) part(av_shield[k->shield][1], 0);
        if (v == V_SIDE && !k->left) part(av_shield[k->shield][3], 0);
    }
    if (far_w) {
        arm(wsx + 1, wsy, k->hx, k->hy, sleeve);
        if (has_w) held(k);
    } else if (wback) {
        held(k);
    }
    // body
    int lf = (v == V_SIDE ? 2 : 0) + k->legf;
    part(av_legs[k->legs][k->boots][lf], k->legflip);
    part(av_torso[k->torso][v], 0);
    if (k->head == AVH_FULL) {
        part(av_head[AVH_FULL][v], 0);
    } else {
        part(av_head[AVH_HAIR][v], 0);
        if (k->head == AVH_MED) part(av_head[AVH_MED][v], 0);
    }
    // arms and front items
    if (v == V_SIDE) {
        if (k->left) {
            arm(osx, osy, ohx, ohy, sleeve);
            if (k->shield != 0xFF) part(av_shield[k->shield][2], 0);
        } else {
            arm(wsx, wsy, k->hx, k->hy, sleeve);
            if (has_w && !wback) held(k);
            if (has_w) pset(k->hx, k->hy, 2);
        }
    } else {
        arm(osx, osy, ohx, ohy, sleeve);
        if (k->shield != 0xFF && v == V_DOWN) part(av_shield[k->shield][0], 0);
        arm(wsx, wsy, k->hx, k->hy, sleeve);
        if (has_w && !wback) held(k);
        if (has_w) pset(k->hx, k->hy, 2);
    }
    // fishing line from the rod tip
    if (k->line && has_w) {
        int tx = k->hx + av_tip[k->wtype][k->ang][0], ty = k->hy + av_tip[k->wtype][k->ang][1];
        int len = k->line;
        for (int i = 1; i <= len; i++) pset(tx, ty + i, 12);
    }
}

static void pack(u32 *dst) {
    for (int ty = 0; ty < 4; ty++)
        for (int tx = 0; tx < 4; tx++) {
            u32 *t = &dst[(ty * 4 + tx) * 8];
            for (int y = 0; y < 8; y++) {
                const u8 *s = &cv[(ty * 8 + y) * AV_SZ + tx * 8];
                t[y] = s[0] | (s[1] << 4) | (s[2] << 8) | (s[3] << 12) | (s[4] << 16) | (s[5] << 20) |
                       ((u32)s[6] << 24) | ((u32)s[7] << 28);
            }
        }
}

static int same(const Look *a, const Look *b) {
    const u8 *p = (const u8 *)a, *q = (const u8 *)b;
    for (unsigned i = 0; i < sizeof(Look); i++)
        if (p[i] != q[i]) return 0;
    return 1;
}

static void update(int buf, const Look *k) {
    if (valid[buf] && same(&last[buf], k)) return;
    last[buf] = *k;
    valid[buf] = 1;
    compose(k);
    pack(tilebuf[buf]);
    pending[buf] = 1;
}

void avatar_vblank(void) {
    for (int b = 0; b < 2; b++)
        if (pending[b]) {
            dma3_copy32((void *)(VRAM_OBJ + AV_TILE(b) * 16), tilebuf[b], 16 * 8);
            pending[b] = 0;
        }
}

// ---------------------------------------------------------------------------------------
// Poses: hand position (human-cell coords), item angle (0 = N, clockwise in 45 degree steps)
// and flags, per view (down, up, side facing right)
typedef struct { s8 hx, hy; u8 ang, flags; } Grip;
enum {
    PZ_REST, PZ_RAISE, PZ_HIGH, PZ_STRIKE, PZ_MINE, PZ_SLASH, PZ_PULL, PZ_THRUST,
    PZ_SCOOP_UP, PZ_SCOOP_DN, PZ_CAST, PZ_HOLD, PZ_READY, PZ_FOLLOW, PZ_AIM, PZ_DRAW1, PZ_DRAW2, PZ_READ,
    PZ_SKYWARD, PZ_COUNT
};
static const Grip poses[PZ_COUNT][3] = {
    [PZ_REST] = {{3, 11, 7, 0}, {12, 11, 1, 0}, {10, 11, 2, 0}},
    [PZ_RAISE] = {{3, 5, 0, WF_BACK}, {13, 6, 1, 0}, {6, 4, 7, WF_BACK}},
    [PZ_HIGH] = {{5, 3, 0, WF_BACK}, {12, 3, 0, 0}, {10, 3, 0, 0}},
    [PZ_STRIKE] = {{5, 12, 4, 0}, {10, 4, 0, WF_BACK}, {12, 9, 2, 0}},
    [PZ_MINE] = {{5, 12, 4, 0}, {10, 4, 0, WF_BACK}, {12, 10, 3, 0}},
    [PZ_SLASH] = {{6, 12, 3, 0}, {10, 4, 1, WF_BACK}, {12, 10, 3, 0}},
    [PZ_PULL] = {{3, 9, 4, 0}, {12, 10, 0, 0}, {6, 10, 2, 0}},
    [PZ_THRUST] = {{5, 13, 4, 0}, {11, 5, 0, WF_BACK}, {13, 9, 2, 0}},
    [PZ_SCOOP_UP] = {{4, 10, 5, 0}, {12, 8, 0, 0}, {11, 9, 2, 0}},
    [PZ_SCOOP_DN] = {{5, 13, 4, 0}, {10, 4, 0, WF_BACK}, {12, 11, 3, 0}},
    [PZ_CAST] = {{3, 6, 0, WF_BACK}, {12, 8, 4, 0}, {7, 5, 7, WF_BACK}},
    [PZ_HOLD] = {{4, 11, 5, 0}, {11, 6, 0, WF_BACK}, {11, 10, 1, 0}},
    [PZ_READY] = {{3, 10, 0, 0}, {12, 10, 0, 0}, {9, 10, 1, 0}},
    [PZ_FOLLOW] = {{4, 13, 5, 0}, {11, 6, 0, WF_BACK}, {11, 12, 3, 0}},
    [PZ_AIM] = {{5, 11, 0, 0}, {11, 8, 0, WF_BACK}, {13, 9, 0, 0}},   // bow held out, upright
    // home teleport: a hand tracing the circle on the ground, then the book held up to read
    [PZ_DRAW1] = {{3, 15, 4, WF_HIDE}, {12, 15, 4, WF_HIDE}, {12, 15, 3, WF_HIDE}},
    [PZ_DRAW2] = {{6, 15, 4, WF_HIDE}, {10, 15, 4, WF_HIDE}, {9, 15, 3, WF_HIDE}},
    [PZ_READ] = {{7, 10, 0, 0}, {8, 9, 0, WF_BACK}, {11, 9, 1, 0}},
    [PZ_SKYWARD] = {{0, 3, 0, 0}, {15, 3, 0, 0}, {10, 1, 0, 0}},   // arm flung up, clear of the head
};

static void base_look(Look *k, int weapon) {
    k->head = pl.equip[EQ_HEAD] ? look_of(pl.equip[EQ_HEAD], AVH_MED) : AVH_HAIR;
    k->torso = pl.equip[EQ_BODY] ? look_of(pl.equip[EQ_BODY], AVT_PLATE) : AVT_SHIRT;
    k->legs = pl.equip[EQ_LEGS] ? look_of(pl.equip[EQ_LEGS], AVG_PLATE) : AVG_PANTS;
    k->boots = pl.equip[EQ_FEET] ? 1 : 0;
    k->shield = pl.equip[EQ_SHIELD] ? look_of(pl.equip[EQ_SHIELD], AVS_SQ) : 0xFF;
    k->wtype = weapon ? look_of(weapon, AVW_SWORD) : 0xFF;
    k->line = 0;
    k->pad = 0;
}

static void set_grip(Look *k, int pose) {
    const Grip *g = &poses[pose][k->view];
    k->hx = g->hx;
    k->hy = g->hy;
    k->ang = g->ang;
    k->wflags = g->flags;
}

// overhead-swing timeline for a 60-frame skilling cycle that strikes at t = 0
static int swing_pose(int t, int strike) {
    if (t < 10) return strike;
    if (t < 20) return PZ_FOLLOW;
    if (t < 38) return PZ_READY;
    if (t < 49) return PZ_RAISE;
    return PZ_HIGH;
}

void avatar_draw_world(int sx, int sy, int dir, int phase, int act, int t, int tool) {
    Look k;
    int home = act == AVA_HOME;
    int reading = home && t >= 6 * TICK_FRAMES && t < 14 * TICK_FRAMES;
    int weapon = reading ? HELD_BOOK : tool ? tool : pl.equip[EQ_WEAPON];
    static u16 seen_equip[EQ_COUNT];
    int changed = weapon != world_held;
    for (int i = 0; i < EQ_COUNT; i++)
        if (seen_equip[i] != pl.equip[i]) seen_equip[i] = pl.equip[i], changed = 1;
    world_held = weapon;
    if (changed || shown_held != world_held) gfx_set_player_palette();
    base_look(&k, weapon == HELD_BOOK ? 0 : weapon);
    if (reading) k.wtype = AVW_BOOK;
    k.view = dir == DIR_DOWN ? V_DOWN : dir == DIR_UP ? V_UP : V_SIDE;
    k.left = dir == DIR_LEFT;
    k.legf = phase & 1;
    k.legflip = k.view != V_SIDE && phase == 3;
    int pose = PZ_REST;
    switch (act) {
    case AVA_CHOP: pose = swing_pose(t, PZ_STRIKE); break;
    case AVA_MINE: pose = swing_pose(t, PZ_MINE); break;
    case AVA_NET: pose = (t / 15) & 1 ? PZ_SCOOP_DN : PZ_SCOOP_UP; break;
    case AVA_ROD:
        pose = t >= 48 ? PZ_CAST : PZ_HOLD;
        break;
    case AVA_SHOOT: pose = t < 8 ? PZ_READY : PZ_AIM; break;   // t: frames since the last shot
    case AVA_CAST: pose = t < 14 ? PZ_CAST : PZ_REST; break;
    case AVA_HOME:
        // ticks 0-5 trace the circle, 6-13 read, 14-19 raise the arms, 20-23 vanish
        if (t < 6 * TICK_FRAMES) pose = (t / 15) & 1 ? PZ_DRAW2 : PZ_DRAW1;
        else if (t < 14 * TICK_FRAMES) pose = PZ_READ;
        else if (t < 20 * TICK_FRAMES) pose = PZ_CAST;
        else pose = PZ_SKYWARD;
        break;
    case AVA_COMBAT: {
        // t: frames since the last swing (0..119); wind up over the final 20 frames
        int stab = k.wtype == 0xFF || k.wtype == AVW_SWORD || k.wtype == AVW_DAGGER;
        int slash = k.wtype == AVW_SCIM || k.wtype == AVW_LONG;
        if (t < 12) pose = stab ? PZ_THRUST : slash ? PZ_SLASH : PZ_STRIKE;
        else if (stab) pose = t >= 104 ? PZ_PULL : PZ_REST;
        else if (t >= 112 && !slash) pose = PZ_HIGH;
        else if (t >= 96) pose = PZ_RAISE;
        break;
    }
    }
    set_grip(&k, pose);
    if (home && !reading) k.wflags |= WF_HIDE;   // the weapon is put away
    if (pose == PZ_HOLD) k.line = k.view == V_SIDE ? 5 + ((t >> 4) & 1) : 3;
    // swing the arm a little while walking side-on
    if (pose == PZ_REST && k.view == V_SIDE && phase) k.hx += phase == 1 ? 1 : phase == 3 ? -1 : 0;
    update(0, &k);
    // 32x32 sprite, human cell at (8,12)
    if (oam_n >= 128) return;
    int x = sx - AV_OX, y = sy - AV_OY + (home && t < 14 * TICK_FRAMES ? 2 : 0);   // kneeling
    if (x <= -32 || x >= SCREEN_W || y <= -32 || y >= SCREEN_H) return;
    ObjAttr *o = &oam_buf[oam_n++];
    o->a0 = (y & 0xFF) | (1 << 12);                            // square, mosaic
    o->a1 = (x & 0x1FF) | (2 << 14) | (k.left ? 1 << 12 : 0);  // 32x32
    o->a2 = AV_TILE(0) | (2 << 10);
}

// The menu portrait / paper-doll: standing pose facing down, 2x affine (matrix 0).
// (x, y) is the top-left of the doubled 16x16 human cell on screen.
void avatar_draw_portrait(int x, int y, int prio) {
    Look k;
    base_look(&k, pl.equip[EQ_WEAPON]);
    k.view = V_DOWN;
    k.left = k.legf = k.legflip = 0;
    set_grip(&k, PZ_REST);
    update(1, &k);
    if (oam_n >= 128) return;
    int bx = x - AV_OX * 2, by = y - AV_OY * 2;
    ObjAttr *o = &oam_buf[oam_n++];
    o->a0 = (by & 0xFF) | (3 << 8);                            // affine, double size, square
    o->a1 = (bx & 0x1FF) | (2 << 14);                          // 32x32, matrix 0
    o->a2 = AV_TILE(1) | ((prio & 3) << 10);
}
