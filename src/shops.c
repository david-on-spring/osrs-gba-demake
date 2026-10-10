// Shops: OSRS stock lists, dynamic prices and restocking.
//
// General stores sell at 130% of an item's value minus 3% per item in stock (floor 30%) and
// buy anything tradeable at 40% minus 3% per item above their normal stock (floor 10%).
// Specialty shops sell at 100%, 2% dearer for each item below their normal stock (2%
// cheaper for each above), and buy only what they stock, at 60% minus 2% per item above
// normal stock (floor 10%). Stock below normal comes back one at a time; extra stock
// (including items players sold) drains away at one item a minute, like OSRS.
// Stock lists follow the OSRS wiki, limited to items the demake has.
#include "game.h"

typedef struct { u16 id, qty; } StockDef;
typedef struct {
    const char *name;
    u8 general;
    u8 n;
    const StockDef *stock;
} ShopDef;

#define GENERAL_BASICS                                                                        \
    {I_POT, 5}, {I_JUG, 2}, {I_SHEARS, 2}
#define GENERAL_REST                                                                          \
    {I_BUCKET, 3}, {I_BOWL, 2}, {I_CAKE_TIN, 2}, {I_TINDERBOX, 2}, {I_CHISEL, 2}

static const StockDef st_general[] = {
    GENERAL_BASICS, GENERAL_REST, {I_HAMMER, 5}, {I_NEWCOMER_MAP, 5}, {I_SECURITY_BOOK, 5},
};
static const StockDef st_lumbridge[] = {
    GENERAL_BASICS, {I_KNIFE, 5}, GENERAL_REST, {I_SPADE, 5}, {I_HAMMER, 5}, {I_NEWCOMER_MAP, 5},
    {I_SECURITY_BOOK, 5},
};
static const StockDef st_edgeville[] = {
    GENERAL_BASICS, GENERAL_REST, {I_HAMMER, 5}, {I_KNIFE, 1}, {I_NEWCOMER_MAP, 5}, {I_SECURITY_BOOK, 5},
};
static const StockDef st_bob[] = {
    {I_BRONZE_PICK, 5}, {I_BRONZE_AXE, 10}, {I_IRON_AXE, 5}, {I_STEEL_AXE, 3},
};
static const StockDef st_gerrant[] = {
    {I_NET, 5}, {I_ROD, 5}, {I_FEATHER, 1000}, {I_RAW_SHRIMP, 0}, {I_RAW_ANCHOVY, 0},
    {I_RAW_TROUT, 0}, {I_RAW_SALMON, 0},
};
#define BASIC_RUNES(n)                                                                        \
    {I_FIRE_RUNE, n}, {I_WATER_RUNE, n}, {I_AIR_RUNE, n}, {I_EARTH_RUNE, n}, {I_MIND_RUNE, n},    \
    {I_BODY_RUNE, n}
static const StockDef st_betty[] = {BASIC_RUNES(300), {I_EYE_NEWT, 300}};
static const StockDef st_aubury[] = {BASIC_RUNES(5000), {I_CHAOS_RUNE, 250}, {I_DEATH_RUNE, 250}};
static const StockDef st_lowe[] = {
    {I_BRONZE_ARROW, 2000}, {I_IRON_ARROW, 1500}, {I_STEEL_ARROW, 1000}, {I_MITHRIL_ARROW, 800},
    {I_SHORTBOW, 4}, {I_LONGBOW, 4}, {I_OAK_SHORTBOW, 3}, {I_OAK_LONGBOW, 3}, {I_WILLOW_SHORTBOW, 2},
    {I_WILLOW_LONGBOW, 2}, {I_MAPLE_SHORTBOW, 1}, {I_MAPLE_LONGBOW, 1},
};
static const StockDef st_zaff[] = {
    {I_STAFF, 5}, {I_MAGIC_STAFF, 5}, {I_STAFF_AIR, 2}, {I_STAFF_WATER, 2}, {I_STAFF_EARTH, 2}, {I_STAFF_FIRE, 2},
};
static const StockDef st_swords[] = {
    {I_BRONZE_SWORD, 5}, {I_IRON_SWORD, 4}, {I_STEEL_SWORD, 4}, {I_STEEL_LONGSWORD, 3}, {I_BRONZE_DAGGER, 10},
};
static const StockDef st_horvik[] = {
    {I_BRONZE_CHAINBODY, 5}, {I_IRON_CHAINBODY, 3}, {I_BRONZE_BODY, 3}, {I_IRON_BODY, 1},
    {I_STEEL_BODY, 1}, {I_MITHRIL_BODY, 1}, {I_IRON_LEGS, 1},
};
static const StockDef st_peksa[] = {
    {I_BRONZE_HELM, 5}, {I_IRON_HELM, 3}, {I_STEEL_HELM, 3}, {I_BRONZE_FULL_HELM, 4},
    {I_IRON_FULL_HELM, 3}, {I_MITHRIL_FULL_HELM, 1}, {I_ADAMANT_FULL_HELM, 1},
};
static const StockDef st_cassie[] = {
    {I_WOODEN_SHIELD, 5}, {I_BRONZE_SHIELD, 3}, {I_IRON_SHIELD, 2}, {I_IRON_KITE, 0}, {I_STEEL_SHIELD, 0},
};
static const StockDef st_wayne[] = {{I_BRONZE_CHAINBODY, 3}, {I_IRON_CHAINBODY, 2}};
static const StockDef st_zeke[] = {{I_IRON_SCIMITAR, 3}, {I_MITHRIL_SCIMITAR, 1}};
static const StockDef st_louie[] = {
    {I_BRONZE_LEGS, 5}, {I_IRON_LEGS, 3}, {I_STEEL_LEGS, 2}, {I_MITHRIL_LEGS, 1}, {I_ADAMANT_LEGS, 1},
};
static const StockDef st_nurmof[] = {
    {I_BRONZE_PICK, 6}, {I_IRON_PICK, 5}, {I_STEEL_PICK, 4}, {I_MITHRIL_PICK, 3}, {I_ADAMANT_PICK, 2},
    {I_RUNE_PICK, 1},
};

#define N(a) (u8)(sizeof(a) / sizeof(a[0]))
#define GEN(name, st) {name, 1, N(st), st}
#define SPEC(name, st) {name, 0, N(st), st}
static const ShopDef shop_defs[SHOP_COUNT] = {
    [SHOP_LUMBRIDGE] = GEN("Lumbridge General Store", st_lumbridge),
    [SHOP_VARROCK] = GEN("Varrock General Store", st_general),
    [SHOP_FALADOR] = GEN("Falador General Store", st_general),
    [SHOP_EDGEVILLE] = GEN("Edgeville General Store", st_edgeville),
    [SHOP_ALKHARID] = GEN("Al Kharid General Store", st_general),
    [SHOP_RIMMINGTON] = GEN("Rimmington General Store", st_general),
    [SHOP_BOB] = SPEC("Bob's Brilliant Axes", st_bob),
    [SHOP_GERRANT] = SPEC("Gerrant's Fishy Business", st_gerrant),
    [SHOP_BETTY] = SPEC("Betty's Magic Emporium", st_betty),
    [SHOP_SWORDSHOP] = SPEC("Varrock Swordshop", st_swords),
    [SHOP_HORVIK] = SPEC("Horvik's Armour Shop", st_horvik),
    [SHOP_PEKSA] = SPEC("Peksa's Helmet Shop", st_peksa),
    [SHOP_CASSIE] = SPEC("Cassie's Shield Shop", st_cassie),
    [SHOP_WAYNE] = SPEC("Wayne's Chains", st_wayne),
    [SHOP_ZEKE] = SPEC("Zeke's Superior Scimitars", st_zeke),
    [SHOP_LOUIE] = SPEC("Louie's Armoured Legs Bazaar", st_louie),
    [SHOP_NURMOF] = SPEC("Nurmof's Pickaxe Shop", st_nurmof),
    [SHOP_LOWE] = SPEC("Lowe's Archery Emporium", st_lowe),
    [SHOP_ZAFF] = SPEC("Zaff's Superior Staffs!", st_zaff),
    [SHOP_AUBURY] = SPEC("Aubury's Rune Shop", st_aubury),
};

// live stock: base = normal stock (0 for items players sold), fixed = always listed
typedef struct { u16 id, qty, base; u8 fixed; } ShopSlot;
static EWRAM_BSS ShopSlot stock[SHOP_COUNT][SHOP_SLOTS];
static u8 restock_t, drain_t;

#define RESTOCK_TICKS 10    // one missing item back every 5 s
#define DRAIN_TICKS 120     // one surplus item gone every minute

// an empty slot that is not part of the normal stock leaves the list; later items move up
static void drop_if_gone(int s, int i) {
    ShopSlot *k = &stock[s][i];
    if (k->qty || k->base || k->fixed) return;
    for (; i + 1 < SHOP_SLOTS; i++) stock[s][i] = stock[s][i + 1];
    stock[s][SHOP_SLOTS - 1].id = stock[s][SHOP_SLOTS - 1].qty = 0;
    stock[s][SHOP_SLOTS - 1].base = stock[s][SHOP_SLOTS - 1].fixed = 0;
}

void shops_reset(void) {
    for (int s = 0; s < SHOP_COUNT; s++)
        for (int i = 0; i < SHOP_SLOTS; i++) {
            ShopSlot *k = &stock[s][i];
            if (i < shop_defs[s].n) {
                k->id = shop_defs[s].stock[i].id;
                k->qty = k->base = shop_defs[s].stock[i].qty;
                k->fixed = 1;
            } else {
                k->id = k->qty = k->base = k->fixed = 0;
            }
        }
    restock_t = drain_t = 0;
}

void shops_tick(void) {
    int restock = ++restock_t >= RESTOCK_TICKS, drain = ++drain_t >= DRAIN_TICKS;
    if (restock) restock_t = 0;
    if (drain) drain_t = 0;
    if (!restock && !drain) return;
    // walk backwards: dropping a slot moves only the (already visited) later ones up
    for (int s = 0; s < SHOP_COUNT; s++)
        for (int i = SHOP_SLOTS - 1; i >= 0; i--) {
            ShopSlot *k = &stock[s][i];
            if (!k->id) continue;
            if (restock && k->qty < k->base) {
                int step = k->base / 50 ? k->base / 50 : 1;   // big stacks (feathers) refill faster
                k->qty = k->qty + step > k->base ? k->base : k->qty + step;
            }
            if (drain && k->qty > k->base) {
                k->qty--;
                drop_if_gone(s, i);
            }
        }
}

int shop_nearest(void) {
    int best = 0, bd = 0x7FFF;
    for (int s = 0; s < SHOP_COUNT; s++) {
        int dx = pl.x - shop_pos[s].x, dy = pl.y - shop_pos[s].y;
        int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (shop_pos[s].plane != pl.plane ? 0x1000 : 0);
        if (d < bd) bd = d, best = s;
    }
    return best;
}

const char *shop_name(int s) { return shop_defs[s].name; }
int shop_is_general(int s) { return shop_defs[s].general; }
int shop_count(int s) {
    int n = 0;
    for (int i = 0; i < SHOP_SLOTS; i++)
        if (stock[s][i].id) n = i + 1;
    return n;
}
void shop_view(int s, Slot *out) {
    for (int i = 0; i < SHOP_SLOTS; i++) {
        out[i].id = stock[s][i].id;
        out[i].qty = stock[s][i].qty;
    }
}

static int pct_of(int value, int pct) { return value * pct / 100; }


static int find(int s, int id) {
    for (int i = 0; i < SHOP_SLOTS; i++)
        if (stock[s][i].id == id) return i;
    return -1;
}

// what the shop charges for one of slot i right now (0 if none in stock)
int shop_buy_price(int s, int i) {
    ShopSlot *k = &stock[s][i];
    if (!k->id) return 0;
    int v = items[k->id].value;
    int pct;
    if (shop_defs[s].general) {
        pct = 130 - 3 * k->qty;
        if (pct < 30) pct = 30;
    } else {
        pct = 100 + 2 * ((int)k->base - (int)k->qty);
        if (pct < 10) pct = 10;
    }
    int p = pct_of(v, pct);
    return p < 1 ? 1 : p;
}

static const u16 untradeable[] = {
    I_COINS, I_GHOSTSPEAK, I_SKULL, I_MESSAGE, I_CADAVA_POTION, I_PACKAGE, I_NOTES, I_RAT_TAIL, I_GAUGE,
    I_TUBE, I_OIL_CAN, I_MANOR_KEY, I_CHEST_KEY, I_PIRATE_MSG, I_SILVERLIGHT, I_KEY_ROVIN, I_KEY_TRAIBORN,
    I_KEY_DRAIN, I_ORANGE_MAIL, I_BLUE_MAIL,
};

// what the shop pays for one item id right now; -1 if it won't take it
int shop_sell_price(int s, int id) {
    for (unsigned i = 0; i < sizeof(untradeable) / sizeof(untradeable[0]); i++)
        if (untradeable[i] == id) return -1;
    int i = find(s, id);
    if (!shop_defs[s].general && i < 0) return -1;
    int over = 0;
    if (i >= 0) over = (int)stock[s][i].qty - (int)stock[s][i].base;
    if (over < 0) over = 0;
    int pct = shop_defs[s].general ? 40 - 3 * over : 60 - 2 * over;
    if (pct < 10) pct = 10;
    return pct_of(items[id].value, pct);
}

// take one from the shop / add one to it; return 0 when the shop has none / no room
int shop_take(int s, int i) {
    ShopSlot *k = &stock[s][i];
    if (!k->id || !k->qty) return 0;
    k->qty--;
    drop_if_gone(s, i);
    return 1;
}

int shop_put(int s, int id) {
    int i = find(s, id);
    if (i < 0) {
        for (i = 0; i < SHOP_SLOTS && stock[s][i].id; i++) {}
        if (i >= SHOP_SLOTS) return 0;
        stock[s][i].id = id;
        stock[s][i].qty = stock[s][i].base = stock[s][i].fixed = 0;
    }
    if (stock[s][i].qty == 0xFFFF) return 0;
    stock[s][i].qty++;
    return 1;
}
