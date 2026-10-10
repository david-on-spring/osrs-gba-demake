// Private interface shared by the overworld modules (world.c, combat.c, skilling.c,
// world_draw.c). The rest of the game talks to the world through game.h only.
#pragma once
#include "game.h"

#define MAX_FIRES 8
#define MAX_GROUND 40
#define MAX_DEPLETED 32
#define MAX_SPLATS 4
typedef struct { u8 active; s16 x, y; u16 timer; } Fire;
typedef struct { u16 id, qty; s16 x, y; u16 timer; } Ground;
typedef struct { u8 active, type; s16 x, y; u16 timer; } Depleted;
typedef struct { u8 timer; s16 wx, wy; } Splat;

// world.c: entity state
extern Fire fires[MAX_FIRES];
extern Ground ground[MAX_GROUND];
extern Depleted depleted[MAX_DEPLETED];
extern Splat splats[MAX_SPLATS];
extern int tick_frames;                 // frames into the current game tick
extern int npc_hi;                      // npcs[] slots in use (spawn table size)
extern const s8 DX[4], DY[4];           // per DIR_*
extern u32 occ_bits[];                  // NPC occupancy bitmap, one bit per cell

static inline int idx(int x, int y) { return y * MAP_W + x; }
static inline int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static inline void occ_set(int x, int y) { int c = y * MAP_W + x; occ_bits[c >> 5] |= 1u << (c & 31); }
static inline void occ_clear(int x, int y) { int c = y * MAP_W + x; occ_bits[c >> 5] &= ~(1u << (c & 31)); }
static inline int roll(int p256) { return rnd_range(256) < p256; }

// world.c: runtime map overrides (stumps, mined-out rocks)
#define MAX_OVR 72
enum { OV_META = 1, OV_OVER = 2, OV_TYPE = 4 };
extern int n_ovr;
int ovr_set(int cell, int mask, int meta, int over, int type);
void ovr_clear(int cell, int mask);
int res_alt_meta(int cell);             // depleted metatile of a resource cell, -1 if none

// world.c: queries and helpers
int npc_at(int x, int y);               // npc index on the cell, -1 if none
int spot_at(int x, int y);              // fishing spot index, -1 if none
int fire_at(int x, int y);              // fires[] index, -1 if none
int walkable(int x, int y);             // terrain only (fires don't block)
int free_tile(int x, int y);            // walkable, no NPC, not the player
int adjacent(int ax, int ay, int bx, int by);
int clear_shot(int ax, int ay, int bx, int by);   // no wall or tree between two cells (projectiles)
int pickup_at(int x, int y);            // pick up the top ground item; 1 if one was there (even if the inventory is full)
void splat_spawn(int wx, int wy, int dmg);

// combat.c
extern u8 foe_timer;
int foe_free(int i);                    // npc i may fight the player (single-way combat)
void foe_engage(int i);
void foe_release(int i);
int weapon_speed(void);                 // ticks between attacks
int player_attack(int i);               // 0 if no attack could be made (out of ammo or runes)
int npc_in_reach(int i);                // the player can attack npc i from where they stand
void npc_attack(int i);
#define MAX_PROJ 4
typedef struct { u8 active, npc, dmg, cb, spell, icon, t, dur, hit; u16 drop; s16 sx, sy; } Proj;
extern Proj projs[MAX_PROJ];            // arrows and spells in flight
void proj_update(void);                 // every frame: move, land hits on arrival
void proj_clear(void);
extern u8 cast_once;
void home_update(void);                 // spells.c: the home teleport's cast, every frame
void home_cancel(void);                    // spell to cast once at the target (spellbook), 0 = none

// skilling.c
typedef struct { u8 id, power, level; } Tool;
extern const Tool axes[6], picks[6];    // best first
const Tool *best_tool(const Tool *t, int n, int sk, int *owned);
void deplete(int x, int y, int type, int respawn_ticks);
void restore(Depleted *d);
void skill_tick(void);
void interact(void);                    // the A button in the world
void npc_talk(int n);                   // talk to an NPC standing next to the player
void npc_fight(int n);                  // attack an NPC (and target it)
