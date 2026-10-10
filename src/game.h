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
    I_COUNT
};

enum { IF_STACK = 1, IF_EAT = 2, IF_BURY = 4, IF_EQUIP = 8, IF_LIGHT = 16 };
enum { EQ_HEAD, EQ_WEAPON, EQ_BODY, EQ_SHIELD, EQ_LEGS, EQ_FEET, EQ_NECK, EQ_COUNT };
enum { TIER_NONE, TIER_BRONZE, TIER_IRON, TIER_STEEL, TIER_LEATHER, TIER_WOOD, TIER_MITHRIL,
       TIER_ADAMANT, TIER_RUNE, TIER_COUNT };

typedef struct {
    const char *name;
    const char *examine;
    u8 icon, flags, slot, req;   // req: attack (weapons) or defence (armour) level
    s8 atk, str, def;
    u8 heal;
    u16 value;
    u8 tier;
} ItemDef;
extern const ItemDef items[I_COUNT];

typedef struct { u16 id, qty; } Slot;

// ---------------------------------------------------------------------------------------
// Skills
// ---------------------------------------------------------------------------------------
enum { SK_ATTACK, SK_STRENGTH, SK_DEFENCE, SK_HITPOINTS, SK_PRAYER, SK_WOODCUTTING,
       SK_FIREMAKING, SK_FISHING, SK_COOKING, SK_MINING, SK_COUNT };
extern const char *const skill_names[SK_COUNT];
extern const u8 skill_icons[SK_COUNT];

// ---------------------------------------------------------------------------------------
// NPCs
// ---------------------------------------------------------------------------------------
enum { NF_ATTACK = 1, NF_AGGRO = 2, NF_TALK = 4, NF_SHOP = 8, NF_BANK = 16 };
enum { SPR_KIND_HUMAN, SPR_KIND_COW, SPR_KIND_CHICKEN, SPR_KIND_WIZARD, SPR_KIND_WITCH, SPR_KIND_PRIEST,
       SPR_KIND_DRESS, SPR_KIND_SKELETON, SPR_KIND_GHOST, SPR_KIND_SHEEP, SPR_KIND_IMP, SPR_KIND_RAT,
       SPR_KIND_DEMON };
typedef struct {
    const char *name;
    u8 sprite, pal, level, hp;
    u8 att, str, def;
    s8 atkb, strb, defb;
    u8 flags, wander, respawn;
    const char *const *dialog;
} NpcDef;
extern const NpcDef npc_defs[NPC_KIND_COUNT];

#define MAX_NPCS 224
typedef struct {
    u8 active, kind, dir, moving;
    s16 x, y;            // tile
    s16 px, py;          // pixel position (top-left)
    s16 hx, hy;          // home tile
    u8 hp, step, anim, attack_cd;
    u8 in_combat, lunge, hpbar_timer, state;   // state: quest use (e.g. sheared sheep timer)
    u16 timer, respawn;
} Npc;

// ---------------------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------------------
enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT };
enum { ACT_NONE, ACT_CHOP, ACT_MINE, ACT_FISH, ACT_LIGHT, ACT_COOK, ACT_COMBAT };
enum { STYLE_ACCURATE, STYLE_AGGRESSIVE, STYLE_DEFENSIVE };

typedef struct {
    s16 x, y, px, py;
    u8 dir, moving, step, anim;
    u8 action, action_timer, lunge, turn_delay;
    s16 tx, ty;                  // action target tile
    s16 target;                  // npc index for combat, fishing spot index for fishing
    u8 hp, style, autoret, attack_cd;
    u8 cook_item, hpbar_timer, run, music;
    u32 xp[SK_COUNT];            // tenths of XP
    Slot inv[28];
    u16 equip[EQ_COUNT];
    Slot bank[96];
    u32 playtime;                // frames
    u8 qstage[16];               // quest progress (0 = not started, QuestDef.done = complete)
    u16 qvar[16];                // per-quest flag bits / counters
} Player;

extern Player pl;

// ---------------------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------------------
enum { GS_TITLE, GS_WORLD, GS_MENU, GS_DIALOG, GS_BANK, GS_SHOP, GS_LEVELUP, GS_QUESTDONE };
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
int equip_bonus(int which);              // 0 atk, 1 str, 2 def
int bank_add(int id, int qty);

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
void gfx_mark_cell_dirty(int x, int y);
void gfx_world_update(void);
void obj_sprite(int x, int y, int tile, int pal, int prio, int hflip, int size16, int flags);
void obj_icon(int x, int y, int icon, int prio);
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
int world_type(int x, int y);           // cell type (T_*), including runtime changes
int world_meta(int x, int y);           // ground metatile
int world_over(int x, int y);           // overlay metatile (tree canopies)
void world_init(void);
void world_new_game(void);
void world_update(void);
void world_draw_sprites(int prio_mosaic);
void world_after_load(void);
int world_region(void);
void player_start_firemaking(int slot);
void player_stop(void);
void player_respawn(void);
void player_tp(int x, int y, int dir);
void player_warp(int x, int y, int dir);  // fade out, move, fade in (staircases)
int world_warping(void);
int world_zone(int x, int y);           // zones[] index of a cell
void world_camera(void);
int player_walk_through(void);          // step two cells ahead (through a door / past a guard)
int player_can_walk_through(void);      // the cell two ahead is free
void world_refresh_npcs(void);          // apply quest NPC visibility
void npc_set_active(int i, int on);
int npc_portrait_tile(int kind);        // first tile of a 16x16 frame for dialogue portraits
int item_on_ground_count(void);
void drop_item(int x, int y, int id, int qty);

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
void ui_draw_sprites(void);
void dialog_run_script(void);           // show a quest script started by quest_run()
void questdone_update(void);
void quest_complete_popup(int q);

// avatar.c (composited player sprite)
enum { AVA_IDLE, AVA_CHOP, AVA_MINE, AVA_NET, AVA_ROD, AVA_COMBAT };
void avatar_palette(u16 *pal16);
void avatar_vblank(void);
void avatar_draw_world(int sx, int sy, int dir, int walk_phase, int act, int t, int tool);
void avatar_draw_portrait(int x, int y, int prio);

// sound.c
enum { SFX_MOVE, SFX_OK, SFX_BACK, SFX_CHOP, SFX_MINE, SFX_SPLASH, SFX_HIT, SFX_MISS, SFX_EAT,
       SFX_PICKUP, SFX_FIRE, SFX_LEVEL, SFX_DEATH, SFX_ERR, SFX_COINS, SFX_QUEST };
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
int quest_object(int type, int x, int y);
int quest_item(int slot);               // "Use" an inventory item with a script
int quest_item_usable(int id);
int quest_npc_dying(int npc);           // 1 if a quest takes over the NPC's death (Delrith)
int quest_npc_visible(int kind);        // -1 = not quest controlled
int qvm_step(DlgEvent *ev);             // run until something to show; 0 when finished
void qvm_choose(int i);
int qvm_after(void);                    // 1 = open the shop, 2 = the bank, after a script
int quest_pending(void);                // quest id awaiting its completion popup, or -1
void quest_new_game(void);
