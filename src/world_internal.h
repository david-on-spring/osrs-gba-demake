// Private interface shared by the overworld modules (world.c, combat.c, skilling.c,
// world_draw.c). The rest of the game talks to the world through game.h only.
#pragma once
#include "game.h"
#include "map.h"

#define MAX_FIRES 8
#define MAX_GROUND 40
#define MAX_DEPLETED 32
#define MAX_SPLATS 4
typedef struct { u8 active, plane; s16 x, y; u16 timer; } Fire;
typedef struct { u16 id, qty; s16 x, y; u16 timer; u8 plane; } Ground;
typedef struct { u8 active, type, plane; s16 x, y; u16 timer; } Depleted;
typedef struct { u8 timer; s16 wx, wy; } Splat;

// world.c: entity state
extern Fire fires[MAX_FIRES];
extern Ground ground[MAX_GROUND];
extern Depleted depleted[MAX_DEPLETED];
extern Splat splats[MAX_SPLATS];
extern int tick_frames;                 // frames into the current game tick
extern int npc_hi;                      // npcs[] slots in use (spawn table size)
extern const s8 DX[4], DY[4];           // per DIR_*: tile steps (DY north = +1)
extern const s8 SDY[4];                 // per DIR_*: screen y step (south = +1)

static inline int in_map(int x, int y, int p) { return map_in(x, y, p); }
static inline int roll(int p256) { return rnd_range(256) < p256; }

// world.c: runtime map overrides (stumps, mined-out rocks)
#define MAX_OVR 72
enum { OV_META = 1, OV_OVER = 2, OV_TYPE = 4 };
extern int n_ovr;
int ovr_set(int x, int y, int p, int mask, int meta, int over, int type);
void ovr_clear(int x, int y, int p, int mask);
int res_alt_meta(int x, int y, int p);  // depleted metatile of a resource cell, -1 if none
int world_roof(int x, int y, int p);    // roof group of a cell (0 none)

// world.c: queries and helpers
int npc_at(int x, int y, int p);        // npc index on the cell, -1 if none
int spot_at(int x, int y, int p);       // fishing spot index, -1 if none
int fire_at(int x, int y, int p);       // fires[] index, -1 if none
int walkable(int x, int y, int p);      // terrain only (fires don't block)
int free_tile(int x, int y, int p);     // walkable, no NPC, not the player
int adjacent(int ax, int ay, int bx, int by);
int clear_shot(int ax, int ay, int bx, int by, int p);   // no wall or tree between two cells (projectiles)
int pickup_at(int x, int y, int p);     // pick up the top ground item; 1 if one was there (even if the inventory is full)
void splat_spawn(int wx, int wy, int dmg);
void world_debug(void);                 // apply test-script requests (each world frame)

// combat.c
extern u8 foe_timer;
int foe_free(int i);                    // npc i may fight the player (single-way combat)
void foe_engage(int i);
void foe_release(int i);
int weapon_speed(void);                 // ticks between attacks
int player_attack(int i);               // 0 if no attack could be made (out of ammo or runes)
int npc_in_reach(int i);                // the player can attack npc i from where they stand
void npc_attack(int i);
// Projectiles fly like OSRS's: released a few frames into the attack, arcing to the target and
// following it, landing on the tick OSRS's hit delay gives (1 + (3 + d) / 6 ticks for arrows,
// 1 + (1 + d) / 3 for spells, d the distance in tiles)
#define MAX_PROJ 6
typedef struct {
    u8 active, npc, dmg, cb, spell, icon, hit, at_pl;   // at_pl: an NPC's spell flying at the player
    u8 t, dur, rel, arc;                                // frames: flown, to land, before release; arc peak px
    u16 drop;
    s16 sx, sy;
} Proj;
extern Proj projs[MAX_PROJ];            // arrows and spells in flight
void proj_update(void);                 // every frame: move, land hits on arrival
void proj_clear(void);
void proj_pos(const Proj *p, int *x, int *y, int *vx, int *vy);   // where it is drawn, and its heading
// spell graphics where a projectile lands: an element burst, or the pale blue splash of a miss
#define MAX_SPOTS 4
typedef struct { u8 timer, icon, npc, on_pl; } Spot;   // icon 0 = splash
extern Spot spots[MAX_SPOTS];
extern u8 cast_once;
void home_update(void);                 // spells.c: the home teleport's cast, every frame
void home_cancel(void);                    // spell to cast once at the target (spellbook), 0 = none

// skilling.c
typedef struct { u8 id, power, level; } Tool;
extern const Tool axes[6], picks[6];    // best first
const Tool *best_tool(const Tool *t, int n, int sk, int *owned);
void deplete(int x, int y, int p, int type, int respawn_ticks);
void restore(Depleted *d);
void skill_tick(void);
void smith_tick(void);                  // smithing.c: one game tick at a furnace or anvil
void smith_interact(int t, int x, int y);   // the A button on a furnace or anvil (T_*) at x, y
void interact(void);                    // the A button in the world
void npc_talk(int n);                   // talk to an NPC standing next to the player
void npc_fight(int n);                  // attack an NPC (and target it)
