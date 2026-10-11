# OSRS GBA Demake

Bare-metal GBA game in C (gnu11, Thumb, `-O2`) with ARM assembly. Python 3 generates all art, the map, quests and music at build time. No devkitPro or libtonc.

## Build and test

```bash
./build.sh                 # make -j8 with the toolchain in .toolchain/ -> rs07demake.gba
make test                  # or: sh tools/run_quest_tests.sh (exit status = failure count)
./play.sh                  # build and open in mGBA (macOS)
```

- `build/` holds generated C (`assets.c/h`, `quest_data.c/h`, `music_data.c/h`, `avatar_art.c/h`, `version.h` from `git describe`), `spawns.txt` (NPC index, kind, OSRS tile and plane) and `rs07demake.map`. Don't edit generated files. Change the generator instead.
- After any gameplay change, run `make test`. It takes about 10 seconds and plays 13 quests, bank/shop/skilling/smithing/combat/ranged/magic/target/settings tests and save/reload.
- For refactors that should not change behaviour, run `python3 tools/snap.py build/snap_before` before the change and `python3 tools/snap.py build/snap_after --compare build/snap_before` after it. It runs every scenario and byte-compares about 220 screenshots, waiting in game frames (the harness's `gameframe` mode), so frames that drop one VBlank more or less do not shift later shots.
- To rebuild the harness: `tools/build_mgba.sh` (once, needs cmake), then `tools/build_harness.sh`. Newer commands (`expectge32`, `expectlt8`, `expectlt16`, `gameframe`) need a harness built from the current `tools/harness.c`. `make clean` deletes `build/harness`, and the fallback `.toolchain/harness` may be older and silently ignore newer checks such as `expectge32`.
- CI is in `.github/workflows/ci.yml` (build plus `make test` on PRs and `main`) and `release.yml` (a `v*` tag builds, tests and attaches `osrs-gba-demake.gba` to the release). Shared setup is in `.github/actions/setup`.

## Source map

The main loop is in `src/main.c`. It switches on `game_state` (`GS_*` in `game.h`), then runs `ui_draw_sprites`, `world_draw_sprites` and `gfx_world_update`.

| File | Owns |
|---|---|
| `src/game.h` | Public types (items, NPCs, `Player pl`), globals, every module's API |
| `src/world.c` | Override table, ground items, fires, movement, NPC AI, `game_tick`, `world_update` |
| `src/map.h`, `src/map.c` | Map storage: OSRS 64×64 regions of LZ77 blocks, the EWRAM block cache and its prefetch, walkability bitmaps, NPC occupancy hash (see `docs/regions.md`) |
| `src/combat.c` | Hit rolls, single-way combat lock (`foe_*`), melee/ranged/magic attacks (`combat_type`, `attack_range`), projectiles (`projs`), curses, drops (`npc_drops`), death, respawn |
| `src/spells.c` | The OSRS standard spellbook table (`spells[]`, book order), rune checks, teleports, alchemy, autocast and manual casts (`cast_once`) |
| `src/skilling.c` | Resource and recipe tables, tools, `skill_tick`, firemaking, `interact()` (the world A button: talk, attack, fish, chop, mine, cook, furnace, anvil, bank booth, shop counter, pickup) |
| `src/smithing.c` | Smithing tables (`smelt_defs`, `anvil_tiers`), `smith_start`/`smith_tick` (`ACT_SMELT`/`ACT_SMITH`), Doric's anvils, Superheat Item |
| `src/smithy.c` | The furnace / anvil screen (`GS_SMITH`): item grid, L/R metal, 1/5/10/All |
| `src/world_draw.c` | Depth-sorted sprites for the player, NPCs, items, fires and hitsplats |
| `src/world_internal.h` | Private state and helpers shared by the four world files |
| `src/ui.c` | UI core: `sbuf`/`sadd` text building, icon/cursor sprite list, popup menu (`pop_*`), item grid, header/footer, `ui_draw_sprites`, `ui_wait` |
| `src/hud.c` | Chat messages, XP drops, area banner, HP orb, target bar, level-up popup, quest complete scroll |
| `src/target.c` | L targeting (`lock_target`, `target_cycle`) and the world target menu (`GS_TARGET`) |
| `src/menu.c` | START menu: inventory actions (`item_action`), equipment, skills, spellbook, quest list and journal, combat style, settings |
| `src/dialog.c` | Chatbox: NPC page lists and quest VM events (`qvm_step`), choices, portraits |
| `src/store.c` | Bank and shop screens (stock logic lives in `src/shops.c`) |
| `src/title.c` | Login screen, name keyboard, save slots |
| `src/ui_internal.h` | Private helpers shared by the UI files |
| `src/quests.c` | Quest bytecode VM, conditions, rewards |
| `src/data.c` | `items[]`, `npc_defs[]`, rng, number formatting, skills/XP, inventory helpers |
| `src/gfx.c`, `src/canvas.c` | VRAM/OAM and tile streaming; per-pixel UI canvases, font and panels (hot paths are `IWRAM_CODE`) |
| `src/sound.c`, `src/mixer.s` | Sequencer and SFX; ARM wavetable mixer in IWRAM |
| `src/save.c` | SRAM, 3 slots, migration from older save versions |

## Generators (`tools/`)

- `world_gen.py`: the map in real OSRS coordinates, 1 cell = 1 OSRS tile. It lays the world out on a 416×460 cell grid (overworld cells 0..415, upper floors (`ZONES`) below it) and emits everything in absolute OSRS tiles (`osrs_of()`), the map as regions of LZ77 blocks (`build_regions()`, `tools/lz77.py`). Towns are rebuilt in place over a countryside sketch (`city()`, `build_from_ref`, `migrate_ow_content`), and palette sets follow the camera (`PAL_REGIONS`, shared banks). See `docs/remaster.md`. Walls stand on tile edges, as in OSRS (`EDGES`, emitted as per-region edge bitmaps): buildings are still authored with wall cells, and `convert_walls()` moves them onto edges once every building is laid (furnishing and bank/shop rows are deferred until then); `zplan()` lays a floor plan with edge walls directly (the castle). It places buildings, `shop(...)`/`service(...)`, `spawn(kind, X, Y)`, `spawn_area(...)`, the ores in `mine(...)`, and furnaces and anvils on their OSRS tiles with `forge(...)` (named in `build/spawns.txt` for `face`, e.g. `FURNACE_LUMBRIDGE`, `ANVIL_VARROCK_W1`). New fields that `gen_assets.py` reads from the world must also be listed in `world_cache.FIELDS`. Underground areas are zones on planes of their own at OSRS's surface coordinates (OSRS keeps them 6400 tiles north): the Mining Guild mine is plane 3 (`MINING_GUILD`), the Asgarnian Ice Dungeon plane 4 (`ICE_DUNGEON`), the Lumbridge Castle cellar plane 5 (`CELLAR`); their ladders are `obj LADDER` in `quests.txt`. Varrock Palace's floors above the ground are planes 6..8 (`PALACE_1..3`, laid by `tools/zones/varrock.py` from `varrock_palace<N>.ref`; stairs `VP_*`/`VPA_*` in `obj STAIRS`/`obj LADDER`). It is imported by `gen_assets.py` through `world_cache.py`, which keeps its results in `build/cache/world-<hash>.pickle` keyed by the contents of `world_gen.py`, `hd_art.py`, `lz77.py`, `zones/*` and the `WG_*` env knobs, so a quests, sprite or emission edit reruns the generator in about 2 s instead of rebuilding the world (about 3 min). World C emission lives in `world_emit.py`, outside the key. `WG_NOCACHE=1` bypasses the cache.
- `hd_art.py`: all world art (terrain field, walls, roofs, furniture), with per-town themes and an `OVERWORLD` theme.
- `zones/<town>.ref` (traced by `ref_extract.py` from the OSRS Wiki map) and `zones/<town>.py` (banks, shops, landmarks, run inside world_gen's namespace).
- `gen_assets.py`: sprites, icons, font and tiles. The NPC kind list is `KINDS`, which becomes the `NPC_*` ids. A new kind also needs an entry in `npc_defs[]` in `src/data.c`; a monster also takes its Magic level, magic and ranged (standard) defence and elemental weakness from the OSRS Wiki infobox (`MR`/`MRW`).
- `quests.txt` plus `gen_quests.py`: quest definitions and dialogue scripts. The syntax is in the `gen_quests.py` docstring. Dialogue is wrapped and paged at build time, and the build fails on bad fits or labels.
- `midi2gba.py` plus `music/tracks.txt`: music. `gen_avatar.py`: player layers. `gen_title.py`: login art.

## Test scripts

- `shots/q/*.src` (quests) and `shots/t/*.src` (systems) are expanded by `tools/qtest.py`. Its docstring lists the commands: `tp`, `item`, `equip`, `xp`, `stage`, `talk`, `dlg`, `choose`, `world`, `untildead`, and the `expect*` checks. `tools/run_scenario.py` then resolves `@symbol+off` from the map and runs `build/harness`.
- Scripts can only poke globals that appear in the map. Test-visible state must not be `static` (for example `ui_wait`, `pl`, `npcs`, `game_state`, `map_loads`).
- `ui_wait` is what scripts wait on: 1 page shown, 2 choice box, 3 quest scroll, 4 level-up, 5 free in the world, 6 target menu, 7 furnace / anvil screen.
- Menu positions matter to scripts: the START menu is Inventory, Worn Equipment, Skills, Spellbook, Quest List, Combat Options, Settings, and it (like the spellbook) remembers its last selection.
- Walls are on tile edges, so a tile beside a wall is walkable and the wall only blocks the step across it; `stand` lines in `spawns.txt` never face a thing through a wall.
- Positions in scripts are OSRS tiles. Prefer `face THING DIR` and `tpz ZONE X Y DIR` (the zone names the plane); `build/spawns.txt` lists spawns, objects, items, named places, zones and where to stand to face each thing, all as `X Y PLANE`.
- To find tiles, look up NPC positions in `build/spawns.txt`. For terrain, use `world_gen.TY` (`cell_of(X, Y, plane)` turns a tile into its grid cell), since the ROM map is compressed.

## Invariants

- **Item ids are saved.** Append new `I_*` at the end of the enum, before `I_COUNT`, and never reorder.
- **The `Player` struct is saved raw** and `qtest.py` hard-codes its offsets (`OFF_XP`, `OFF_INV`, `OFF_BANK`, ...). Changing its layout needs a save version bump and migration in `save.c`, plus updated offsets (`save.c` asserts them, and `world.c` asserts `sizeof(Npc)` against `NPC_SIZE`).
- **Skill and equipment ids are saved too:** append new `SK_*` / `EQ_*` before the `_COUNT`, as with items.
- Frame budget: `perf_max` counts scanlines of work per frame. 228 or more is a dropped frame. Per-NPC paths (`free_tile`, `npc_at`, `walkable`) stay in `world.c` next to the NPC AI.
- Memory: EWRAM 256 KB (`EWRAM_BSS` for big arrays), IWRAM 32 KB (stack, mixer, `IWRAM_CODE`). Code and const data live in ROM. Nothing may be sized by the map (no whole-map bitmaps): the map is decompressed in 16×16 blocks around the camera (`src/map.c`).
- One game tick is `TICK_FRAMES` (30 frames, OSRS's 600 ms). XP is stored in tenths.
- The game has no libc. `mem.c` provides memcpy/memset. Use `fmt_num`/`fmt_int` and `sadd`/`sadd_num` for strings.
