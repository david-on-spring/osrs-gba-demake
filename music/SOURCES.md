# Music sources

Active manifest: `music/tracks.txt` (18 tracks, one per region group). The MIDIs are the Old
School RuneScape soundtrack arrangements supplied by the project owner. The compositions are
copyrighted by Jagex Ltd. and are used here for this fan demake only; remove them if a rights
holder asks.

| Slot (`MUS_*`) | File | Track |
|---|---|---|
| `TITLE` | `scape_main.mid` | Scape Main |
| `HARMONY` | `harmony.mid` | Harmony |
| `NEWBIE` | `newbie_melody.mid` | Newbie Melody |
| `AUTUMN` | `autumn_voyage.mid` | Autumn Voyage |
| `DREAM` | `dream.mid` | Dream |
| `FLUTE_SALAD` | `flute_salad.mid` | Flute Salad |
| `UNKNOWN_LAND` | `unknown_land.mid` | Unknown Land |
| `SPOOKY` | `spooky.mid` | Spooky |
| `ADVENTURE` | `adventure.mid` | Adventure |
| `MEDIEVAL` | `medieval.mid` | Medieval |
| `GARDEN` | `garden.mid` | Garden |
| `FANFARE` | `fanfare.mid` | Fanfare |
| `ARRIVAL` | `arrival.mid` | Arrival |
| `EXPANSE` | `expanse.mid` | Expanse |
| `AL_KHARID` | `al_kharid.mid` | Al Kharid |
| `ARABIAN` | `arabian.mid` | Arabian |
| `FOREVER` | `forever.mid` | Forever |
| `BARBARIANISM` | `barbarianism.mid` | Barbarianism |
| `WILDERNESS` | `wilderness.mid` | Wilderness |

## Original placeholders

`title`, `lumbridge`, `varrock`, `falador`, `draynor`, `alkharid` and `edgeville` `.mid` are
unused original compositions from `tools/make_demo_midis.py` (seeded chord progressions, not
transcriptions). `wilderness.mid` was overwritten by the real track, so regenerating with that
tool replaces it with the placeholder again.

## Using your own MIDI files

1. Put a Standard MIDI File (`.mid`, format 0 or 1) that you have the right to use into `music/`.
2. In `music/tracks.txt`, point a slot at it, or add a line to create a new slot. A new slot
   gets an `MUS_<ID>` constant automatically.
3. Run `./build.sh`. The converter prints note count, polyphony, length and tempo for each track.
   Add a row to the table above that says where the file came from.

The converter maps General MIDI programs to the synth's instruments and channel 10 to the drum
kit. It honours `loopStart`/`loopEnd` marker events and bakes the sustain pedal, volume and
expression into the notes. Pitch bends are ignored and reported. Tracks that need more than 10
simultaneous notes still play, but the quietest voices get stolen.
