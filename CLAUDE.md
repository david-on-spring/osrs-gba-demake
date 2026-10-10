# OSRS GBA Demake

Bare-metal GBA game in C (gnu11, Thumb, `-O2`) with ARM assembly. Python 3 generates all art, the map, quests and music at build time. No devkitPro or libtonc.

## Build and test

```bash
./build.sh                 # make -j8 with the toolchain in .toolchain/ -> rs07demake.gba
make test                  # or: sh tools/run_quest_tests.sh (exit status = failure count)
./play.sh                  # build and open in mGBA (macOS)
```

- `build/` holds generated C (`assets.c/h`, `quest_data.c/h`, `music_data.c/h`, `avatar_art.c/h`), `spawns.txt` (NPC index, kind, cell) and `rs07demake.map`. Don't edit generated files. Change the generator instead.
- After any gameplay change, run `make test`. It takes about 10 seconds and plays 13 quests, bank/shop/skilling/combat tests and save/reload.
- For refactors that should not change behaviour, run `python3 tools/snap.py build/snap_before` before the change and `python3 tools/snap.py build/snap_after --compare build/snap_before` after it. It runs every scenario and byte-compares about 120 screenshots.
- To rebuild the harness: `tools/build_mgba.sh` (once, needs cmake), then `tools/build_harness.sh`. `make clean` deletes `build/harness`, and the fallback `.toolchain/harness` may be older and silently ignore newer checks such as `expectge32`.
- CI is in `.github/workflows/ci.yml` (build plus `make test` on PRs and `main`) and `release.yml` (a `v*` tag builds, tests and attaches `osrs-gba-demake.gba` to the release). Shared setup is in `.github/actions/setup`.

## Source map

The main loop is in `src/main.c`. It switches on `game_state` (`GS_*` in `game.h`), then runs `ui_draw_sprites`, `world_draw_sprites` and `gfx_world_update`.

| File | Owns |
|---|---|
| `src/game.h` | Public types (items, NPCs, `Player pl`), globals, every module's API |
| `src/world.c` | Map state and override table, NPC occupancy, ground items, fires, movement, NPC AI, `game_tick`, `world_update` |
| `src/combat.c` | Hit rolls, single-way combat lock (`foe_*`), attacks, drops (`npc_drops`), death, respawn |
| `src/skilling.c` | Resource and recipe tables, tools, `skill_tick`, firemaking, `interact()` (the world A button: talk, attack, fish, chop, mine, cook, bank booth, shop counter, pickup) |
| `src/world_draw.c` | Depth-sorted sprites for the player, NPCs, items, fires and hitsplats |
| `src/world_internal.h` | Private state and helpers shared by the four world files |
| `src/ui.c` | UI core: `sbuf`/`sadd` text building, icon/cursor sprite list, popup menu (`pop_*`), item grid, header/footer, `ui_draw_sprites`, `ui_wait` |
| `src/hud.c` | Chat messages, XP drops, area banner, HP orb, target bar, level-up popup, quest complete scroll |
| `src/menu.c` | START menu: inventory actions (`item_action`), equipment, skills, quest list and journal, combat style, settings |
| `src/dialog.c` | Chatbox: NPC page lists and quest VM events (`qvm_step`), choices, portraits |
| `src/store.c` | Bank and shop screens (stock logic lives in `src/shops.c`) |
| `src/title.c` | Login screen, name keyboard, save slots |
| `src/ui_internal.h` | Private helpers shared by the six UI files |
| `src/quests.c` | Quest bytecode VM, conditions, rewards |
| `src/data.c` | `items[]`, `npc_defs[]`, rng, number formatting, skills/XP, inventory helpers |
| `src/gfx.c`, `src/canvas.c` | VRAM/OAM and tile streaming; per-pixel UI canvases, font and panels (hot paths are `IWRAM_CODE`) |
| `src/sound.c`, `src/mixer.s` | Sequencer and SFX; ARM wavetable mixer in IWRAM |
| `src/save.c` | SRAM, 3 slots, migration from older save versions |

## Generators (`tools/`)

- `world_gen.py`: the map in real OSRS coordinates. The overworld is 1 cell = 2×2 OSRS tiles (cells 0..207); the 1:1 towns are zones below it in the same 256×560 grid (`ZONES`, `city()`, portal rings, `build_from_ref`, `migrate_ow_content`). See `docs/remaster.md`. It places buildings, `shop(...)`/`service(...)`, `spawn(kind, X, Y)` and `spawn_area(...)`. It is imported by `gen_assets.py`.
- `hd_art.py`: art for the 1:1 zones (terrain field, walls, roofs, furniture), with per-town themes.
- `zones/<town>.ref` (traced by `ref_extract.py` from the OSRS Wiki map) and `zones/<town>.py` (banks, shops, landmarks, run inside world_gen's namespace).
- `gen_assets.py`: sprites, icons, font and tiles. The NPC kind list is `KINDS`, which becomes the `NPC_*` ids. A new kind also needs an entry in `npc_defs[]` in `src/data.c`.
- `quests.txt` plus `gen_quests.py`: quest definitions and dialogue scripts. The syntax is in the `gen_quests.py` docstring. Dialogue is wrapped and paged at build time, and the build fails on bad fits or labels.
- `midi2gba.py` plus `music/tracks.txt`: music. `gen_avatar.py`: player layers. `gen_title.py`: login art.

## Test scripts

- `shots/q/*.src` (quests) and `shots/t/*.src` (systems) are expanded by `tools/qtest.py`. Its docstring lists the commands: `tp`, `item`, `equip`, `xp`, `stage`, `talk`, `dlg`, `choose`, `world`, `untildead`, and the `expect*` checks. `tools/run_scenario.py` then resolves `@symbol+off` from the map and runs `build/harness`.
- Scripts can only poke globals that appear in the map. Test-visible state must not be `static` (for example `ui_wait`, `occ_bits`, `pl`, `npcs`, `game_state`).
- `ui_wait` is what scripts wait on: 1 page shown, 2 choice box, 3 quest scroll, 4 level-up, 5 free in the world.
- Prefer `face THING DIR` and `tpz ZONE X Y DIR` (OSRS coordinates) over raw `tp` cells in scripts; `build/spawns.txt` lists spawns, objects, items, named places, portals and where to stand to face each thing.
- To find cells, look up NPC positions in `build/spawns.txt`. For terrain, read `map_type[]` in `build/assets.c` (`T_*` in `assets.h`, index `y*208+x`).

## Invariants

- **Item ids are saved.** Append new `I_*` at the end of the enum, before `I_COUNT`, and never reorder.
- **The `Player` struct is saved raw** and `qtest.py` hard-codes its offsets (`OFF_XP`, `OFF_INV`, `OFF_BANK`, ...). Changing its layout needs a save version bump and migration in `save.c`, plus updated offsets.
- Frame budget: `perf_max` counts scanlines of work per frame. 228 or more is a dropped frame. Per-NPC paths (`free_tile`, `npc_at`, `walkable`) stay in `world.c` next to the NPC AI.
- Memory: EWRAM 256 KB (`EWRAM_BSS` for big arrays), IWRAM 32 KB (stack, mixer, `IWRAM_CODE`). Code and const data live in ROM.
- One game tick is `TICK_FRAMES` (30 frames, OSRS's 600 ms). XP is stored in tenths.
- The game has no libc. `mem.c` provides memcpy/memset. Use `fmt_num`/`fmt_int` and `sadd`/`sadd_num` for strings.
