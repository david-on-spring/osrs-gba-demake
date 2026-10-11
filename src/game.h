// Shared game types, globals and module interfaces.
#pragma once
#include "gba.h"
#include "assets.h"

#define SCREEN_W 240
#define SCREEN_H 160
#define TICK_FRAMES 30          // one game tick (RS used 600 ms)

// ---------------------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------------------
enum {
    I_NONE, I_COINS, I_LOGS, I_OAK_LOGS, I_WILLOW_LOGS, I_TINDERBOX, I_BRONZE_AXE, I_IRON_AXE,
    I_STEEL_AXE, I_BRONZE_PICK, I_IRON_PICK, I_NET, I_ROD, I_FEATHER, I_RAW_SHRIMP, I_SHRIMP,
    I_BURNT_SHRIMP, I_RAW_ANCHOVY, I_ANCHOVY, I_RAW_TROUT, I_TROUT, I_RAW_SALMON, I_SALMON,
    I_BURNT_FISH, I_RAW_BEEF, I_COOKED_MEAT, I_BURNT_MEAT, I_RAW_CHICKEN, I_COOKED_CHICKEN,
    I_BONES, I_COWHIDE, I_COPPER_ORE, I_TIN_ORE, I_IRON_ORE, I_BRONZE_SWORD, I_IRON_SWORD,
    I_STEEL_SWORD, I_BRONZE_HELM, I_IRON_HELM, I_STEEL_HELM, I_WOODEN_SHIELD, I_BRONZE_SHIELD,
    I_IRON_SHIELD, I_STEEL_SHIELD, I_LEATHER_BODY, I_BRONZE_BODY, I_IRON_BODY, I_LEATHER_CHAPS,
    I_BRONZE_LEGS, I_IRON_LEGS, I_BOOTS,
    // equipment variety (avatar shows each type distinctly)
    I_MITHRIL_AXE, I_ADAMANT_AXE, I_RUNE_AXE, I_STEEL_PICK, I_MITHRIL_PICK, I_ADAMANT_PICK,
    I_RUNE_PICK, I_BRONZE_DAGGER, I_IRON_MACE, I_IRON_SCIMITAR, I_MITHRIL_SCIMITAR,
    I_ADAMANT_SCIMITAR, I_RUNE_SCIMITAR, I_STEEL_LONGSWORD, I_RUNE_LONGSWORD, I_BRONZE_FULL_HELM,
    I_IRON_FULL_HELM, I_MITHRIL_FULL_HELM, I_ADAMANT_FULL_HELM, I_RUNE_FULL_HELM,
    I_BRONZE_CHAINBODY, I_IRON_CHAINBODY, I_STEEL_BODY, I_MITHRIL_BODY, I_ADAMANT_BODY, I_RUNE_BODY,
    I_BRONZE_SKIRT, I_IRON_SKIRT, I_STEEL_LEGS, I_MITHRIL_LEGS, I_ADAMANT_LEGS, I_RUNE_LEGS,
    I_IRON_KITE, I_MITHRIL_KITE, I_ADAMANT_KITE, I_RUNE_KITE, I_IRON_BOOTS,
    I_YEW_LOGS,
    // quest items
    I_EGG, I_BUCKET, I_BUCKET_MILK, I_BUCKET_WATER, I_POT, I_POT_FLOUR, I_GRAIN, I_SHEARS, I_WOOL,
    I_BALL_WOOL, I_GHOSTSPEAK, I_SKULL, I_MESSAGE, I_CADAVA, I_CADAVA_POTION, I_CLAY, I_AIR_TALISMAN,
    I_PACKAGE, I_NOTES, I_RED_BEAD, I_YELLOW_BEAD, I_BLACK_BEAD, I_WHITE_BEAD, I_ACCURACY, I_EYE_NEWT,
    I_ONION, I_RAT_TAIL, I_GOBLIN_MAIL, I_ORANGE_DYE, I_BLUE_DYE, I_ORANGE_MAIL, I_BLUE_MAIL, I_GOLD_BAR,
    I_FISH_FOOD, I_POISON, I_POISON_FOOD, I_GAUGE, I_TUBE, I_OIL_CAN, I_MANOR_KEY, I_SPADE, I_RUM,
    I_CHEST_KEY, I_PIRATE_MSG, I_GOLD_RING, I_EMERALD, I_CABBAGE, I_SILVERLIGHT, I_KEY_ROVIN,
    I_KEY_TRAIBORN, I_KEY_DRAIN,
    // general store goods (appended: item ids are saved)
    I_JUG, I_KNIFE, I_BOWL, I_CAKE_TIN, I_CHISEL, I_HAMMER, I_NEWCOMER_MAP, I_SECURITY_BOOK,
    // ranged: bows and arrows (appended: item ids are saved)
    I_SHORTBOW, I_LONGBOW, I_OAK_SHORTBOW, I_OAK_LONGBOW, I_WILLOW_SHORTBOW, I_WILLOW_LONGBOW,
    I_MAPLE_SHORTBOW, I_MAPLE_LONGBOW, I_BRONZE_ARROW, I_IRON_ARROW, I_STEEL_ARROW, I_MITHRIL_ARROW,
    I_ADAMANT_ARROW,
    // magic: runes and staves
    I_AIR_RUNE, I_WATER_RUNE, I_EARTH_RUNE, I_FIRE_RUNE, I_MIND_RUNE, I_BODY_RUNE, I_CHAOS_RUNE,
    I_DEATH_RUNE, I_STAFF, I_MAGIC_STAFF, I_STAFF_AIR, I_STAFF_WATER, I_STAFF_EARTH, I_STAFF_FIRE,
    I_COSMIC_RUNE, I_LAW_RUNE, I_NATURE_RUNE, I_BLOOD_RUNE, I_SOUL_RUNE, I_WRATH_RUNE,
    // smithing: bars, ores, and everything the anvils make (appended: item ids are saved)
    I_BRONZE_BAR, I_IRON_BAR, I_STEEL_BAR, I_SILVER_BAR, I_MITHRIL_BAR, I_ADAMANT_BAR, I_RUNE_BAR,
    I_COAL, I_SILVER_ORE, I_GOLD_ORE, I_MITHRIL_ORE, I_ADAMANT_ORE, I_RUNE_ORE, I_BRONZE_SCIMITAR,
    I_BRONZE_LONGSWORD, I_BRONZE_2H, I_BRONZE_MACE, I_BRONZE_WARHAMMER, I_BRONZE_BATTLEAXE,
    I_BRONZE_CLAWS, I_BRONZE_KITE, I_BRONZE_NAILS, I_BRONZE_DART_TIP, I_BRONZE_ARROWTIPS,
    I_BRONZE_KNIFE, I_BRONZE_JAVELIN_TIPS, I_BRONZE_BOLTS, I_BRONZE_LIMBS, I_BRONZE_WIRE,
    I_IRON_DAGGER, I_IRON_LONGSWORD, I_IRON_2H, I_IRON_WARHAMMER, I_IRON_BATTLEAXE, I_IRON_CLAWS,
    I_IRON_NAILS, I_IRON_DART_TIP, I_IRON_ARROWTIPS, I_IRON_KNIFE, I_IRON_JAVELIN_TIPS,
    I_IRON_BOLTS, I_IRON_LIMBS, I_IRON_SPIT, I_OIL_LANTERN_FRAME, I_STEEL_DAGGER, I_STEEL_SCIMITAR,
    I_STEEL_2H, I_STEEL_MACE, I_STEEL_WARHAMMER, I_STEEL_BATTLEAXE, I_STEEL_CLAWS,
    I_STEEL_CHAINBODY, I_STEEL_SKIRT, I_STEEL_FULL_HELM, I_STEEL_KITE, I_STEEL_NAILS,
    I_STEEL_DART_TIP, I_STEEL_ARROWTIPS, I_STEEL_KNIFE, I_STEEL_JAVELIN_TIPS, I_STEEL_BOLTS,
    I_STEEL_LIMBS, I_STEEL_STUDS, I_MITHRIL_DAGGER, I_MITHRIL_SWORD, I_MITHRIL_LONGSWORD,
    I_MITHRIL_2H, I_MITHRIL_MACE, I_MITHRIL_WARHAMMER, I_MITHRIL_BATTLEAXE, I_MITHRIL_CLAWS,
    I_MITHRIL_CHAINBODY, I_MITHRIL_SKIRT, I_MITHRIL_HELM, I_MITHRIL_SHIELD, I_MITHRIL_NAILS,
    I_MITHRIL_DART_TIP, I_MITHRIL_ARROWTIPS, I_MITHRIL_KNIFE, I_MITHRIL_JAVELIN_TIPS,
    I_MITHRIL_BOLTS, I_MITHRIL_LIMBS, I_ADAMANT_DAGGER, I_ADAMANT_SWORD, I_ADAMANT_LONGSWORD,
    I_ADAMANT_2H, I_ADAMANT_MACE, I_ADAMANT_WARHAMMER, I_ADAMANT_BATTLEAXE, I_ADAMANT_CLAWS,
    I_ADAMANT_CHAINBODY, I_ADAMANT_SKIRT, I_ADAMANT_HELM, I_ADAMANT_SHIELD, I_ADAMANT_NAILS,
    I_ADAMANT_DART_TIP, I_ADAMANT_ARROWTIPS, I_ADAMANT_KNIFE, I_ADAMANT_JAVELIN_TIPS,
    I_ADAMANT_BOLTS, I_ADAMANT_LIMBS, I_RUNE_DAGGER, I_RUNE_SWORD, I_RUNE_2H, I_RUNE_MACE,
    I_RUNE_WARHAMMER, I_RUNE_BATTLEAXE, I_RUNE_CLAWS, I_RUNE_CHAINBODY, I_RUNE_SKIRT, I_RUNE_HELM,
    I_RUNE_SHIELD, I_RUNE_NAILS, I_RUNE_DART_TIP, I_RUNE_ARROWTIPS, I_RUNE_KNIFE,
    I_RUNE_JAVELIN_TIPS, I_RUNE_BOLTS, I_RUNE_LIMBS,
    // blurite, the furnace's tools and helpers (appended)
    I_BLURITE_ORE, I_BLURITE_BAR, I_BLURITE_BOLTS, I_BLURITE_LIMBS, I_RING_FORGING, I_GOLDSMITH_GAUNTLETS,
    I_AMMO_MOULD, I_CANNONBALL,
    I_COUNT
};

// IF_RANGED: bows (req is a Ranged level) and arrows; IF_MAGIC: staves (cast spells);
// IF_2H: two-handed weapons (2h swords, claws) that leave no hand for a shield
enum { IF_STACK = 1, IF_EAT = 2, IF_BURY = 4, IF_EQUIP = 8, IF_LIGHT = 16, IF_RANGED = 32, IF_MAGIC = 64, IF_2H = 128 };
enum { EQ_HEAD, EQ_WEAPON, EQ_BODY, EQ_SHIELD, EQ_LEGS, EQ_FEET, EQ_NECK, EQ_AMMO, EQ_HANDS, EQ_RING, EQ_COUNT };
enum { TIER_NONE, TIER_BRONZE, TIER_IRON, TIER_STEEL, TIER_LEATHER, TIER_WOOD, TIER_MITHRIL,
       TIER_ADAMANT, TIER_RUNE, TIER_COUNT };
// melee weapon classes (OSRS attack speed and main attack style); W_NONE: unarmed or not a weapon
enum { W_NONE, W_DAGGER, W_SWORD, W_SCIM, W_LONG, W_2H, W_AXE, W_BATTLEAXE, W_MACE, W_WARHAMMER, W_CLAWS,
       W_PICK };

typedef struct {
    const char *name;
    const char *examine;
    u16 icon;
    u8 flags, slot, req;         // req: attack (weapons), defence (armour) or ranged (IF_RANGED) level
    s8 atk, str, def;
    u8 heal;
    u16 value;
    u8 tier;
    s8 ratk, rstr, matk;         // ranged attack, ranged strength, magic attack
    u8 wclass;                   // W_* for melee weapons
} ItemDef;
extern const ItemDef items[I_COUNT];

typedef struct { u16 id, qty; } Slot;

// ---------------------------------------------------------------------------------------
// Skills
// ---------------------------------------------------------------------------------------
enum { SK_ATTACK, SK_STRENGTH, SK_DEFENCE, SK_HITPOINTS, SK_PRAYER, SK_WOODCUTTING,
       SK_FIREMAKING, SK_FISHING, SK_COOKING, SK_MINING, SK_RANGED, SK_MAGIC, SK_SMITHING, SK_COUNT };
extern const char *const skill_names[SK_COUNT];
extern const u16 skill_icons[SK_COUNT];

// ---------------------------------------------------------------------------------------
// NPCs
// ---------------------------------------------------------------------------------------
enum { NF_ATTACK = 1, NF_AGGRO = 2, NF_TALK = 4, NF_SHOP = 8, NF_BANK = 16 };
enum { SPR_KIND_HUMAN, SPR_KIND_COW, SPR_KIND_CHICKEN, SPR_KIND_WIZARD, SPR_KIND_WITCH, SPR_KIND_PRIEST,
       SPR_KIND_DRESS, SPR_KIND_SKELETON, SPR_KIND_GHOST, SPR_KIND_SHEEP, SPR_KIND_IMP, SPR_KIND_RAT,
       SPR_KIND_DEMON };
enum { ELEM_NONE, ELEM_AIR, ELEM_WATER, ELEM_EARTH, ELEM_FIRE };
typedef struct {
    const char *name;
    u8 sprite, pal, level, hp;
    u8 att, str, def;
    s8 atkb, strb, defb;          // defb: stab defence (dslash, dcrush below)
    u8 flags, wander, respawn;
    const char *const *dialog;
    // OSRS Wiki stats for ranged and magic (monsters only; 0 elsewhere)
    u8 mag;                       // Magic level: defends against spells (and powers magic attacks)
    s8 mdef, rdef;                // magic defence, ranged (standard: arrows) defence bonuses
    u8 weak, weakpct;             // elemental weakness: ELEM_*, % more accuracy and damage
    u8 magic_max;                 // > 0: attacks with magic (Water Strike) up to this, instead of melee
    s8 dslash, dcrush;            // slash and crush defence bonuses
    u8 speed;                     // ticks between attacks (0 = 4)
} NpcDef;
extern const NpcDef npc_defs[NPC_KIND_COUNT];
const char *npc_examine(int kind);

#define MAX_NPCS 256
typedef struct {
    u8 active, kind, dir, moving;
    s16 x, y;            // OSRS tile (x east, y north) on floor 'plane'
    s16 px, py;          // pixel position (top-left; TILE_PX / TILE_PY)
    s16 hx, hy;          // home tile
    u8 hp, step, anim, attack_cd;
    u8 in_combat, lunge, hpbar_timer, state;   // state: quest use (e.g. sheared sheep timer)
    u16 timer, respawn;
    u8 curse, bound;     // curse: CU_* bits from Confuse..Stun; bound: ticks held by Bind/Snare/Entangle
    u8 plane;
} Npc;
enum { CU_ATK5 = 1, CU_STR5 = 2, CU_DEF5 = 4, CU_DEF10 = 8, CU_STR10 = 16, CU_ATK10 = 32 };

// ---------------------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------------------
enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum { ACT_NONE, ACT_CHOP, ACT_MINE, ACT_FISH, ACT_LIGHT, ACT_COOK, ACT_COMBAT, ACT_TALK, ACT_SMELT, ACT_SMITH };
// melee: accurate / aggressive / defensive; with a bow the same slots are accurate / rapid / longrange
enum { STYLE_ACCURATE, STYLE_AGGRESSIVE, STYLE_DEFENSIVE };
enum { STYLE_RAPID = 1, STYLE_LONGRANGE = 2 };
enum { CB_MELEE, CB_RANGED, CB_MAGIC };

typedef struct {
    s16 x, y, px, py;            // OSRS tile (on floor 'plane') and pixel position
    u8 dir, moving, step, anim;
    u8 action, action_timer, lunge, turn_delay;
    s16 tx, ty;                  // action target tile
    s16 target;                  // npc index for combat, fishing spot index for fishing
    u8 hp, style, autoret, attack_cd;
    u8 cook_item, hpbar_timer, run, music;
    u32 xp[SK_COUNT];            // tenths of XP
    Slot inv[28];
    u16 equip[EQ_COUNT];
    u16 ammo_qty;                // arrows in the ammo slot (equip[EQ_AMMO])
    Slot bank[96];
    u32 playtime;                // frames
    u8 qstage[16];               // quest progress (0 = not started, QuestDef.done = complete)
    u16 qvar[16];                // per-quest flag bits / counters
    u8 spell;                    // autocast spell (spells[] index, 0 = none), used while a staff is wielded
    u8 plane;                    // floor: 0 ground .. 3
    u16 home_cd;                 // ticks until Lumbridge Home Teleport can be cast again (counts while playing)
    u16 weapon_qty;              // thrown weapons (knives) in the weapon slot
    u8 forging;                  // charges the worn ring of forging has used (it crumbles at 140)
    u8 pad_;
} Player;

extern Player pl;

// ---------------------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------------------
enum { GS_TITLE, GS_WORLD, GS_MENU, GS_DIALOG, GS_BANK, GS_SHOP, GS_LEVELUP, GS_QUESTDONE, GS_TARGET, GS_SMITH };
extern u8 game_state;
extern u32 frame;
extern u16 keys, keys_pressed, keys_repeat;
extern s16 cam_x, cam_y;
extern Npc npcs[MAX_NPCS];

// rng.c / util
u32 rnd(void);
int rnd_range(int n);
void seed_rng(u32 s);
char *fmt_num(char *buf, u32 v);         // with thousands separators
char *fmt_int(char *buf, s32 v);

// skills
int level_for_xp(u32 xp_tenths);
int skill_level(int sk);
int combat_level(void);
int total_level(void);
void add_xp(int sk, u32 tenths);

// inventory
int inv_count(int id);
int inv_free(void);
int inv_add(int id, int qty);            // returns amount added
int inv_remove(int id, int qty);
int has_item(int id);                    // inventory or equipped
int equip_bonus(int which);              // 0 atk, 1 str, 2 def, 3 ranged atk, 4 ranged str, 5 magic atk
int equip_item(int slot);                // wear inventory slot's item; 0 (with a message) if not allowed
int unequip(int eq);                     // back to the inventory; 0 if there is no room

// spells.c: the OSRS standard spellbook. Spells whose systems the demake lacks (enchanting,
// orbs, most teleports...) are listed with their real data but cast as "not available yet".
enum { SPK_COMBAT, SPK_CURSE, SPK_BIND, SPK_INSPECT, SPK_TELE, SPK_ALCH, SPK_HEAT, SPK_NA };
enum { SPG_NONE, SPG_STRIKE, SPG_BOLT, SPG_BLAST, SPG_WAVE, SPG_SURGE };
typedef struct {
    const char *name;
    u8 level, kind, maxhit, group;       // group: max hits rise to the tier's best known spell
    u8 icon, param;                      // icon: projectile; param: curse bits, bind ticks, alch %,
                                         // teleport place (1 Lumbridge, 2 Varrock, 3 Falador), 1 = undead only
    u16 xp;                              // base experience in tenths
    u8 rune[4], qty[4];                  // I_* rune ids (0 = unused)
} SpellDef;
extern const SpellDef spells[];
extern const u8 spell_count;
int spell_maxhit(int s);                 // with the tier rule (Wind Strike hits 8 once Fire Strike is known)
int spell_runes_ok(int s);               // the runes are there (an elemental staff counts as its rune)
int spell_castable(int s, int quiet);    // level and runes, with a message unless quiet
int spell_targeted(int s);               // cast on an NPC (combat spells, curses, binds, Monster Inspect)
void spell_pay(int s);                   // use up the runes
int spell_cast_self(int s);              // teleports; 1 if cast (the home teleport starts its 24-tick cast)
extern u16 home_cast;                    // frames into the home teleport's cast, 0 = not casting
#define HOME_CAST_FRAMES (24 * TICK_FRAMES)
#define HOME_COOLDOWN_TICKS 3000         // 30 minutes
int spell_alch(int s, int slot);         // High / Low Level Alchemy on an inventory slot; 1 if cast
int spell_superheat(int s, int slot);    // Superheat Item on an ore; 1 if cast
int spell_on_item(int s);                // cast on an inventory item (alchemy, Superheat Item)
int spell_autocast(int s);               // set the autocast spell; 0 (with a message) if not possible
void spell_manual(int s);                // cast once at the target (from the spellbook)
int combat_type(void);                   // CB_* for the wielded weapon, autocast or manual spell
int attack_range(void);                  // cells (1 = melee)
int ammo_ok(int arrow, int bow);         // the bow can fire these arrows
int thrown_weapon(int id);               // knives: a stack wielded and thrown, no bow or arrows

int bank_add(int id, int qty);

// smithing.c: furnaces, anvils and Superheat Item (levels, XP, ticks: OSRS Wiki)
typedef struct { u16 bar, ore, ore2; u8 n2, level, ticks; u16 xp; u8 out; u16 tool; } SmeltDef;   // ore2 x n2: tin or coal
typedef struct { u16 item; u8 level, bars, qty; } SmithDef;
typedef struct { u16 bar, xp; const char *metal; const SmithDef *list; u8 n; } AnvilTier;   // xp per bar
#define SMELT_N 10
#define ANVIL_TIERS 7
extern const SmeltDef smelt_defs[SMELT_N];
extern const AnvilTier anvil_tiers[ANVIL_TIERS];
int smelt_max(int k);                    // bars of smelt_defs[k] the inventory has ore for
int smith_max(int t, int i);             // how many of anvil_tiers[t].list[i] the bars make
int anvil_tier_owned(void);              // best tier the player has bars of, -1 = none
int smith_start(int furnace, int k, int i, int count);   // at the facility in front; 0 (with a message) if not possible
void smithy_open(int furnace);           // the "What would you like to make?" screen (smithy.c)
void smithy_update(void);

// gfx.c
typedef struct { u16 a0, a1, a2, a3; } ObjAttr;
extern ObjAttr oam_buf[128];
extern int oam_n;
void gfx_init(void);
void gfx_vblank(void);
void gfx_set_world_fade(int level16);     // 16 = normal, 0 = black
void gfx_set_mosaic(int n);
void gfx_set_ui_alpha(int eva, int text_too);
void gfx_mark_world_dirty(void);
void gfx_mark_cell_dirty(int x, int y, int p);   // redraw a tile changed at runtime
void gfx_world_update(void);
void obj_sprite(int x, int y, int tile, int pal, int prio, int hflip, int size16, int flags);
void obj_icon(int x, int y, int icon, int prio);
void obj_icon_flip(int x, int y, int icon, int prio, int hflip, int vflip);
void obj_hide_rest(void);
void gfx_set_player_palette(void);
void gfx_set_hl_pulse(int phase);
void gfx_dyn_hitsplat(int slot, int dmg);
void gfx_set_ui_scroll(int x);
void gfx_set_title_palette(int on);
void gfx_set_world_palette_set(int set);  // zones[].pal_set (marks the palettes for upload)
void gfx_set_hidden_roof(int group);      // map_roof group not drawn (the one the player is under)
#define OBJF_MOSAIC 1
#define OBJF_SEMI 2
#define OBJF_AFFINE2X 4
#define OBJF_WIDE 8                       // 16x8 instead of 16x16
#define OBJF_8x8 16
#define OBJF_32 32                        // 32x32
#define OBJF_TALL 64                      // 16x32 (vertical)

// canvas.c
typedef struct {
    u32 *tiles;
    u16 vram_tile;        // first tile in VRAM (absolute index from 0x06000000)
    u8 dmin, dmax;        // dirty tile rows
} Canvas;
extern Canvas cv_text, cv_panel;
void cv_init(void);
void cv_clear(Canvas *c);
void cv_clear_rows(Canvas *c, int r0, int r1);
void cv_fill(Canvas *c, int x, int y, int w, int h, int col) __attribute__((long_call));   // IWRAM
void cv_pset(Canvas *c, int x, int y, int col) __attribute__((long_call));             // IWRAM
void cv_flush(Canvas *c, int budget);
int text_width(const char *s);
int text_draw(int x, int y, const char *s, int col);           // with shadow
int text_draw_ns(int x, int y, const char *s, int col);        // no shadow
int text_right(int xr, int y, const char *s, int col);
int text_center(int xc, int y, const char *s, int col);
int text_spaced(int x, int y, const char *s, int col, int sp);
int text_spaced_width(const char *s, int sp);
void text_big(int xc, int y, const char *s);
int text_wrap(const char *s, int width, char lines[][48], int maxlines);
void draw_panel(int x, int y, int w, int h, int chamfer) __attribute__((long_call));      // IWRAM
void draw_panel_plain(int x, int y, int w, int h, int col);
void draw_highlight(int x, int y, int w, int h) __attribute__((long_call));             // IWRAM
void draw_bar(int x, int y, int w, int val, int max, int col, int dark);
void draw_button(int x, int y, char label);
void draw_rule(int x, int y, int w);
void draw_cell(int x, int y, int w, int h, int sel);
void draw_parchment(int x, int y, int w, int h);   // OSRS chatbox / scroll paper
void draw_stone_button(int x, int y, int w, int h, int sel);

// Canvas colours (panel canvas, BG palette 0)
// (OSRS look: FILL stone, WELL dark inset, EDGE light bevel, EDGE_DIM outline, HL* hover shades)
enum { PC_FILL = 1, PC_WELL, PC_EDGE, PC_EDGE_DIM, PC_HL1, PC_HL2, PC_HL3, PC_GOLD, PC_GREEN,
       PC_GREEN_D, PC_PARCH, PC_PARCH_D, PC_BAR_BG, PC_WHITE, PC_RED };
// Text colours (text canvas, BG palette 1)
// TC_GREY/TC_CYAN/TC_ORANGE are OSRS interface orange, TC_GOLD/TC_YELLOW its value yellow
enum { TC_WHITE = 1, TC_GREY, TC_DIM, TC_SHADOW, TC_GOLD, TC_CYAN, TC_GREEN, TC_RED, TC_YELLOW,
       TC_ORANGE, TC_BLUE, TC_BLACK, TC_DKRED, TC_DKBLUE };

// world.c, combat.c, skilling.c, world_draw.c (their shared internals: world_internal.h)
// Positions are absolute OSRS tiles: x grows east, y north, plane is the floor (0 ground .. 3).
// Pixels (px, py, cam_x, cam_y) grow east and south from the tile WORLD_X0, WORLD_Y1 on every floor.
#define TILE_PX(x) (((x) - WORLD_X0) * 16)
#define TILE_PY(y) ((WORLD_Y1 - (y)) * 16)
int world_type(int x, int y, int p);    // cell type (T_*), including runtime changes
int world_meta(int x, int y, int p);    // ground metatile
int world_over(int x, int y, int p);    // overlay metatile (tree canopies)
void world_cell_gfx(int x, int y, int p, int *meta, int *over, int *roof);   // the three at once, for drawing
void world_focus(int *x, int *y, int *p);  // the tile the map should be loaded around
void world_init(void);
void world_new_game(void);
void world_update(void);
void world_draw_sprites(int prio_mosaic);
void world_after_load(void);
int world_region(void);
void player_start_firemaking(int slot);
void player_stop(void);
void player_respawn(void);
void player_tp(int x, int y, int p, int dir);
void player_warp(int x, int y, int p, int dir);  // fade out, move, fade in (staircases)
int world_warping(void);
int world_zone(int x, int y, int p);    // zones[] index of a tile
int npc_talkable(int n);                // has dialogue or a quest script
void player_walk_to(int npc, int act);  // pursue an NPC to fight it (ACT_COMBAT) or talk to it (ACT_TALK)
void world_camera(void);
int player_walk_through(void);          // step two cells ahead (through a door / past a guard)
int player_can_walk_through(void);      // the cell two ahead is free
void world_refresh_npcs(void);          // apply quest NPC visibility
void npc_set_active(int i, int on);
int npc_portrait_tile(int kind);        // first tile of a 16x16 frame for dialogue portraits
int item_on_ground_count(void);
void drop_item(int x, int y, int p, int id, int qty);

// ui.c, hud.c, menu.c, dialog.c, store.c, title.c (their shared internals: ui_internal.h)
void ui_init(void);
void ui_hud_reset(void);
void ui_hud_update(void);
void msg(const char *s);
void msg_col(const char *s, int col);
void xp_drop(int sk, u32 tenths);
void level_up(int sk, int lvl);
void menu_open(void);
void menu_update(void);
void dialog_open(int npc_kind, int npc_index);
void dialog_update(void);
void bank_open(void);
void bank_update(void);
void shop_open(void);
void shop_update(void);
void levelup_update(void);
void title_open(void);
void title_update(void);
void ui_set_target(int npc);
// targeting (L cycles through NPCs on screen, B clears, A opens the target menu)
extern s16 lock_target;                  // npc index, -1 = none
void target_set(int npc);
void target_cycle(void);
void target_menu_open(void);
void target_menu_update(void);
void ui_draw_sprites(void);
void dialog_run_script(void);           // show a quest script started by quest_run()
void questdone_update(void);
void quest_complete_popup(int q);

// avatar.c (composited player sprite)
enum { AVA_IDLE, AVA_CHOP, AVA_MINE, AVA_NET, AVA_ROD, AVA_COMBAT, AVA_SHOOT, AVA_CAST, AVA_HOME };
void avatar_palette(u16 *pal16);
void avatar_vblank(void);
void avatar_draw_world(int sx, int sy, int dir, int walk_phase, int act, int t, int tool);
void avatar_draw_portrait(int x, int y, int prio);

// sound.c
enum { SFX_MOVE, SFX_OK, SFX_BACK, SFX_CHOP, SFX_MINE, SFX_SPLASH, SFX_HIT, SFX_MISS, SFX_EAT,
       SFX_PICKUP, SFX_FIRE, SFX_LEVEL, SFX_DEATH, SFX_ERR, SFX_COINS, SFX_QUEST, SFX_BOW, SFX_CAST };
void sound_init(void);
void sound_update(void);
void sfx(int id);
void music_enable(int on);
// Music tracks come from music/tracks.txt (ids MUS_TITLE, MUS_LUMBRIDGE, ... in music_data.h).
#include "music_data.h"
void music_play(int track);                    // loops; fades out the previous track first
void music_for_region(const char *region);     // switch to the region's track, if it has one
int music_track_for_region(const char *region); // MUS_NONE if the name has no track

// shops.c (positions and SHOP_* ids come from the world generator)
#define SHOP_SLOTS 28
void shops_reset(void);
void shops_tick(void);                  // one game tick: restock / drain surplus
int shop_nearest(void);                 // the shop whose building is closest to the player
const char *shop_name(int s);
int shop_is_general(int s);
int shop_count(int s);                  // listed slots
void shop_view(int s, Slot *out);       // copy of the stock (SHOP_SLOTS entries)
int shop_buy_price(int s, int slot);    // price of one, at the current stock
int shop_sell_price(int s, int id);     // what the shop pays for one; -1 = won't buy
int shop_take(int s, int slot);         // remove one bought item; 0 if out of stock
int shop_put(int s, int id);            // add one sold item; 0 if no room

// save.c: three character slots
#define SAVE_SLOTS 3
#define NAME_MAX 12
extern char player_name[NAME_MAX + 1];
void save_migrate(void);                // copy a pre-slot save into slot 1
int save_exists(void);                  // any slot used
int save_slot_used(int k);
int save_slot_summary(int k, char *name, int *total_level, u32 *playtime);
void save_set_slot(int k);
int save_slot(void);
void save_game(void);                   // to the current slot
int load_game(void);                    // from the current slot

// quests.c (data in build/quest_data.c, generated by tools/gen_quests.py from tools/quests.txt)
#include "quest_data.h"
enum { DLG_SAY = 1, DLG_CHOICE };
#define SPK_PLAYER 0xFE
#define SPK_NONE 0xFD
typedef struct {
    u8 type, speaker, nopt;
    const char *text;
    const char *opt[5];
} DlgEvent;
int quest_stage(int q);
int quest_done(int q);
int quest_points(void);
int quest_talk(int npc);                // run the talk script for npcs[npc], 1 if one exists
int quest_object(int type);            // the A button on a cell of this type
int quest_item(int slot);               // "Use" an inventory item with a script
int quest_item_usable(int id);
int quest_npc_dying(int npc);           // 1 if a quest takes over the NPC's death (Delrith)
int quest_npc_visible(int kind);        // -1 = not quest controlled
int qvm_step(DlgEvent *ev);             // run until something to show; 0 when finished
void qvm_choose(int i);
int qvm_after(void);                    // 1 = open the shop, 2 = the bank, after a script
int quest_pending(void);                // quest id awaiting its completion popup, or -1
void quest_new_game(void);
