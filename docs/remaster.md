# Overworld remaster

The whole mainland is one seamless 1:1 map with the new art. This page records the plan, what
is done, and what it costs on the hardware.

## Prioritized plan

1. **Zone engine** (done, then replaced by item 10). 1:1 towns lived below a 1:2 overworld in
   the same cell grid, joined to it by portal rings with a short fade. Roofs hide while you are
   under them.
2. **Lumbridge vertical slice** (done). Lumbridge was laid out by hand from the OSRS map. The
   castle has three floors with spiral staircases: the kitchen and great hall, Duke Horacio's room
   and the spinning wheel, and the bank. The graveyard's south gate opens onto the swamp, and the
   generator checks it.
3. **Remastered terrain art** (done, `tools/hd_art.py`).
   - Terrain blends through a bilinear field, with rims and soft shadows.
   - Water has shorelines and banks.
   - Walls show a cap and a face, with battlements on free-standing walls.
   - Roofs are hipped, flat or tent-shaped and sit on the overlay layer.
   - Trees, palms and cacti are shaded.
4. **All towns at 1:1** (done). `tools/ref_extract.py` traces the OSRS Wiki world map into
   `tools/zones/*.ref`. `build_from_ref()` turns a trace into walls, doors, rooms, roofs, trees and
   crops. `tools/zones/<town>.py` adds the banks, shops, landmarks and furniture. Overworld NPCs,
   items, quest objects and fishing spots inside a town move into its zone automatically.
5. **Town themes** (done). Each town has its own palette and materials, matched to in-game
   screenshots on the OSRS Wiki:
   - Lumbridge: grey stone, red roofs, a stone bridge and blue banners.
   - Varrock: pale paving, brick, gilded slate and green glass.
   - Falador: white stone, red roofs, lattice fences and the park.
   - Draynor: rough stone, slate and cream market stalls.
   - Al Kharid: whitewash and palms.
   - Edgeville: thatch and dry-stone walls.
6. **Next: interiors and landmarks, one object at a time**, replacing the generic furnishing:
   - Varrock Palace throne room and library.
   - The White Knights' Castle halls.
   - Al Kharid palace courtyard and fountain.
   - The Edgeville mansion ruins.
   - Shop counters and shelves that match OSRS.
7. **Next: upper floors elsewhere**: Varrock Palace, the White Knights' Castle, the Wizards'
   Tower and Draynor Manor.
8. **The countryside at 1:1** (done, item 10): fields, woods, swamp, desert, the Wilderness and
   Ice Mountain use the same terrain field, with an `OVERWORLD` theme.
9. **Next: tracing fixes**: a hand pass over each `.ref` where the map's furniture lines confused
   the tracer. Bank booths and counters are already handled; some houses are still split oddly.
10. **One seamless world** (done). The overworld is 416×416 cells, one per OSRS tile; the castle's
    upper floors sit in rows below it and are the only zones left. The countryside is still
    authored as a sketch in OSRS coordinates; `city()` clears each town's rectangle plus a
    9-tile margin (never another town's rectangle, nor `keep` areas such as the Grand Exchange)
    and the town lays itself out again in place. `migrate_ow_content()` re-lands the sketch's
    NPCs, items and quest objects that fall inside a rebuilt town, and `join_roads()` paves the
    gap where a countryside road meets a town's edge off its streets.
    - Palettes: banks 0–8 are shared by every set and hold the countryside; the other five hold
      a town group's materials. A set is loaded while the camera is inside its region (the town's
      area grown by a screen plus two cells), so its own banks are off screen when it swaps. Towns
      whose regions overlap share a set: Lumbridge with Al Kharid, Draynor with Falador.
    - Doors on a roofed building's north, east and west walls cut a notch in the roof and get a
      doorway, so every entrance shows from outside.
    - Saves from before keep the character and start in Lumbridge (slot version 5).
11. **Regions** (done). The map is stored as OSRS map squares of LZ77 blocks and decompressed
    around the camera; positions are absolute OSRS tiles and the castle floors are planes 1 and 2
    (slot version 6 converts version 5 saves). See `docs/regions.md`.

## Hardware budget

| | 1:2 overworld | 1:1 towns | One 1:1 world | Limit |
|---|---|---|---|---|
| ROM | 828 KB | 2.3 MB | 2.8 MB | 32 MB |
| Map cells | 43,264 | 143,360 | 191,360 | |
| World tiles in ROM | 1,777 | 14,502 | 12,037 | (streamed) |
| Metatiles | — | 29,997 | 45,750 | 65,535 (`meta` is u16) |
| Worst camera window (unique tiles) | 332 | 455 | 417 | 575 cache slots |
| World palette sets | 1 | 7 (one per town) | 5 (countryside + 4 town groups) | |
| EWRAM | 74 KB | 125 KB | 135 KB (147 KB with regions) | 256 KB |
| Peak frame work running across town edges | — | 147–160 scanlines | 156–169 scanlines | 228 |
| NPC slots | 224 | 256 | 256 | |

With regions (item 11) the ROM is 2.0 MB and walking frames peak at 193–208 scanlines; see
`docs/regions.md`. VRAM layout and the streaming algorithm are unchanged. A roof change or a staircase redraws the
31×21 tile window once, and the measurements above include that redraw. About 3.4% of tile uses
are quantized to the nearest colour in their bank (most of them countryside tiles in the shared
banks). `WG_SHARED` sets the number of shared banks.

## How to check it

```bash
./build.sh && make test                                  # 13 quests, bank/shop/skills/combat, castle, save
python3 tools/qtest.py shots/map/towns.src build/t.txt && python3 tools/run_scenario.py build/t.txt
python3 tools/sheet.py out.png 3 shot1.png shot2.png ... # contact sheet of harness screenshots
```

`shots/t/castle.src` climbs every staircase, walks across Lumbridge's west edge and back, and leaves the
graveyard through its south gate into the swamp.
