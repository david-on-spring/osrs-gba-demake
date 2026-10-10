# Overworld remaster

The towns are 1:1 zones with new art. The countryside stays at 1:2. This page records
the plan, what is done, and what it costs on the hardware.

## Prioritized plan

1. **Zone engine** (done). 1:1 towns live in the same cell grid as the overworld, below it
   (`ZONES` in `tools/world_gen.py`). Portal rings join each town to the overworld, and towns
   that share a border join each other directly. Each zone has its own palette set and camera
   bounds. Roofs hide while you are under them. Saves made before the zones existed are moved
   into them on load.
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
8. **Next: the 1:2 countryside**: the same art for fields, woods and swamp. This needs a 1:2
   variant of the terrain field. The overworld copies of the towns could also be repainted with
   the new materials.
9. **Next: tracing fixes**: a hand pass over each `.ref` where the map's furniture lines confused
   the tracer. Bank booths and counters are already handled; some houses are still split oddly.

## Hardware budget

| | Before | After | Limit |
|---|---|---|---|
| ROM | 828 KB | 2.3 MB | 32 MB |
| World tiles in ROM | 1,777 | 14,502 | (streamed) |
| Worst camera window (unique tiles) | 332 | 455 | 575 cache slots |
| Cache misses in the town tours | 0 | 0 | |
| World palette sets | 1 | 7 (one per town) | |
| EWRAM | 74 KB | 125 KB | 256 KB |
| Peak frame work while running through the towns | — | 147–160 scanlines | 228 |
| NPC slots | 224 | 256 | |

VRAM layout and the streaming algorithm are unchanged. A warp or a roof change redraws the 31×21
tile window once, and the measurements above include that redraw. Each town's tiles are packed
into its own 14 banks of 15 colours. About 3% of each town's tiles are quantized to the nearest
colour in their bank.

## How to check it

```bash
./build.sh && make test                                  # 13 quests, bank/shop/skills/combat, castle, save
python3 tools/qtest.py shots/map/towns.src build/t.txt && python3 tools/run_scenario.py build/t.txt
python3 tools/sheet.py out.png 3 shot1.png shot2.png ... # contact sheet of harness screenshots
```

`shots/t/castle.src` climbs every staircase, walks out of Lumbridge and back, and leaves the
graveyard through its south gate into the swamp.
