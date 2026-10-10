// Map storage: per-cell runtime state (see map.h).
#include "map.h"

EWRAM_BSS u32 occ_bits[(MAP_W * MAP_H + 31) / 32];   // (not static: test scripts poke it)
EWRAM_BSS u32 ovr_bits[(MAP_W * MAP_H + 31) / 32];

void map_reset(void) {
    for (int i = 0; i < (MAP_W * MAP_H + 31) / 32; i++) occ_bits[i] = ovr_bits[i] = 0;
}
