# RYSSÄN KAUHU – reverse-engineering notes

These notes describe what the original DOS release contains and how the portable
C version maps onto it. Addresses are in the flat image produced by
`tools/le_unpack.py` (`python3 tools/le_unpack.py assets/RYSS.EXE image.bin`, then
e.g. `objdump -D -b binary -m i386 --adjust-vma=0x10000`): the code object is
linked at `0x10000`, a small asm module (stored as a data object) at `0x20000` and
the data object at `0x30000`.

## Files

| File | What it is |
|---|---|
| `RYSS.EXE` | The game. 32-bit Watcom C/C++ program in LE format with a DOS/4GW stub. |
| `DOS4GW.EXE` | Rational Systems' DOS extender, 1990s version. Not needed by the port. |
| `GRAPH.DAT` | Everything else: palette, background, sprites, fonts, sounds, lookup tables. |
| `GRAPH.BAK` | Identical copy of `GRAPH.DAT`. |
| `readme.txt` | Finnish, roughly: "This game is set in the Winter War, when the Soviet Union attacked Finland. The hero is placed behind a minigun to shoot the miserable Soviet soldiers. Requirements: 486/66, 16 MB memory, SB16, needs a mouse. May run on lesser hardware too. This product has been finded by sheepart's products. Enjoy this game" |

## Port mapping

| Original | Port |
|---|---|
| `main` 0x127E4, game 0x11AC3, tick 0x10F7A, render 0x117A5 and helpers | `src/game.c` (addresses in comments) |
| loader 0x11ECD | `src/data.c` |
| asm module 0x20000 (blitters, font) | `src/gfx.c` |
| SB/GUS driver 0x10010–0x10B97, mixer 0x20051 | `src/sound.c` (SB only) |
| PIT multiplexer 0x12B5D–0x1305F, INT 9 handler, INT 33h, mode 13h | `src/platform.c` |

## The executable

* 32-bit Watcom C/C++ (1994 runtime), LE format, run under DOS/4GW.
* 3 objects: code `0x10000` (0x8066 bytes, game code up to ~`0x146E6`, then the Watcom
  runtime), an asm module `0x20000` (0x5E1 bytes) and data/BSS `0x30000` (0x54DC0 bytes).
* 987 internal fixups, all 32-bit offsets or 32-bit relative calls into the asm object.
* Watcom register calling convention (`eax, edx, ebx, ecx`), stack-check prologue
  `push N; call __CHK` on every C function. That prologue makes function boundaries easy
  to find.

## Program flow (`main` at `0x127E4`)

1. `0x12F07` reprograms PIT channel 0 to 100 Hz (divisor `0x2E9C`) and installs a timer
   multiplexer. `0x12FAA(100, 0x10D9A)` registers the game clock: every 1/100 s it adds
   the speed factor (always 1) to the tick counter `0x30B14`.
2. `0x10B75` detects a Gravis Ultrasound (`ULTRASND` variable) or a Sound Blaster
   (`BLASTER` variable).
3. `0x11ECD` loads `graph.dat` (format below).
4. `0x11DED` builds the lookup tables: `angle_to_x[0x4000]` from the sin table, and the
   four 8.8 fixed point tables used by the scaled sprite blitter.
5. Mode 13h, seeds `rand()` by calling it (sum of date and time bytes) times.
6. Installs a keyboard ISR (`0x10DBE`): `keys[scancode] = pressed`, plus the cheat check.
7. Sets the palette, draws the credits straight into VGA memory and waits for Enter.
8. `0x11AC3` runs the game, then everything is restored.

An unused intro (letters flying in, greyscale palette, `0x126FA`) is still in the binary,
as are DPMI helpers and a file/streaming library (`0x12C17`–`0x146E5`, it searches for
`*.STR` files) of which only the file-length helper `0x13EE8` is called.

## The game loop (`0x11AC3`)

```
reset mouse, range 0..1280, centre it, double sensitivity
level = 1, fire_delay = 12
start_level(level); play(16)
while !Esc:
    while ticks: tick(); ticks--
    render()
    if damage >= 8 or remaining == 0: break
```

then "KUOLIT / Uusi peli (k/e)?" (you died, new game y/n), "KENTTÄ LÄPI! / Paina
välilyöntiä" (level cleared, press space) or after level 6 "VOITIT!!!" (you won).

### `tick()` - `0x10F7A`, 100 times per second

* random ambient sound every 2-4 s (samples 11-14)
* once per level, at 4 damage a medic shouts (17) and 5 s later damage drops by 2
* when ammo runs out: reload sound (15), 50 rounds 5 s later
* aim from the mouse: `aim = (mouse_x/2) * 14000 / 640 + 1200` (range 1200..15184,
  0x2000 is straight ahead). Left/right arrows move it by 100.
* fire = Ctrl, or left mouse button
* spawn a soldier every `spawn_min..spawn_max` ticks until the level quota is used up
* one bullet every `fire_delay` ticks (12, or 2 with the cheat). The bullet direction is
  `aim ± 10`. It kills the nearest soldier with `|angle - aim| < hitw[distance]`.
* each soldier: walks (6 frames, 15 ticks each) towards the player, stops after a random
  time and shoots (frames 6-8). At the third frame he fires: `rand()%5000-2500` within
  `±hitw[distance]` hits the player. At his final distance (200-260) he stays and keeps
  shooting. A hit soldier plays the 13 frame death animation (8 ticks each) and becomes a
  corpse that is drawn into the background layer.

### Soldier node (26 bytes, doubly linked list kept sorted by distance)

| offset | size | meaning |
|---|---|---|
| 0x00 | 4 | next |
| 0x04 | 4 | prev |
| 0x08 | 4 | distance, 16.16 (600-800 at spawn) |
| 0x0C | 4 | speed per tick, 16.16 |
| 0x10 | 2 | angle, 1200..15199 |
| 0x12 | 2 | final distance, 200..259 |
| 0x14 | 2 | ticks until next stop |
| 0x16 | 1 | state: 0 walk, 1 shoot, 2 die |
| 0x17 | 1 | animation frame |
| 0x18 | 1 | tick counter |
| 0x19 | 1 | corpse flag |

### Levels (table at `0x3036C`, 32 bytes each, level 0 unused)

| level | soldiers | speed | walk ticks | spawn ticks | ammo |
|---|---|---|---|---|---|
| 1 | 45 | 15000-35000 | 100-800 | 50-100 | 250 |
| 2 | 60 | 18000-37000 | 50-600 | 40-70 | 250 |
| 3 | 80 | 22000-40000 | 40-400 | 30-50 | 200 |
| 4 | 100 | 24000-42000 | 30-200 | 25-40 | 200 |
| 5 | 130 | 27000-53000 | 20-100 | 20-35 | 200 |
| 6 | 160 | 32000-60000 | 10-50 | 10-25 | 200 |

### Cheat

Typing `SIVARI` sets the fire delay to 2 ticks and gives unlimited ammo (999).

### Rendering (`0x117A5`)

Four 64000 byte buffers: VGA, *clean* (background + corpses), *back* (sprites drawn here)
and the pristine background. Only changed rows are copied:

1. if a soldier has just died: back = background, draw all corpses, clean = back
   otherwise restore last frame's dirty rows (and gun / HUD rectangles) from clean
2. draw the live soldiers far to near with the scaled blitter, extending the dirty row range
3. cross-hair (sprite 28) at `angle_to_x[aim] + 160, 100`
4. gun (24, firing 25/26) at the bottom centre - only when it changes
5. damage crosses (27) at the bottom left, ammo digits at (224,160), level digit at (288,168)
6. copy the dirty rows/rectangles from back to VGA

Soldier projection: `x = 160 + 160*sin(a)`, `scale = 0x8000 / (dist*cos(a) >> 8)` with
`a = (angle - 0x2000) >> 4` as an index into the 4096-entry tables.

## The asm module (`0x20000`)

| address | function |
|---|---|
| 0x20051 | SB mixer, fills a 256 byte DMA half buffer |
| 0x2015C | intro effect (unused) |
| 0x201C8 | copy 64000 bytes |
| 0x201D6 | draw a font character (RLE, 3 sizes) |
| 0x20243 | draw a string |
| 0x2025D | copy dirty rows + gun/HUD rectangles |
| 0x202F0 | draw a sprite (RLE) centred on x,y |
| 0x20392 | draw a sprite scaled (8.8 fixed point, self-modifying step) |
| 0x204B7.. | an 8-bit mantissa software float library (unused) |

## GRAPH.DAT

All values little endian. Every block is lightly "encrypted" with XOR:

| block | size | key |
|---|---|---|
| greyscale palette (intro only) | 0x300 | none |
| sprite data size | u32 | `^ 0xF5D43DD7` |
| 4 tables (unused, cos, sin, hit width) | 4 x 4096 u16 | word i `^ (0x37EA - 2i)` |
| sprite offsets | 256 x u32 | `^ (i + 0xCC96C330)` |
| sample count, then samples `{u32 len; signed 8-bit PCM}` | | none |
| font size, 768 glyph offsets, font data | | offsets `^ 0x41FBE3` |
| game palette | 0x300 | byte i `^ (4i + 0x84)` |
| background 320x200, PCX-like RLE | | `^ (3*pos + 0x9D)` |
| sprite data | | byte i `^ (6i + 0x9A)` |

### Sprites

29 sprites: 0-5 walking, 6-10 shooting (only 6-8 are used), 11-23 dying, 24-26 gun, 27
damage cross, 28 cross-hair. Each: `u16 w, u16 h, 128 x u32 row offsets` followed by RLE
rows: `FF n` skip n, `FE` end of row, `n < FE` n literal pixels.

### Font

Three sizes (32, 16 and 8 pixels), 256 CP437 characters each. A glyph is a byte stream:
odd byte = draw `b>>1` pixels, even non-zero = skip `b>>1`, zero = next row.

### Sound

19 samples, signed 8-bit mono at 8000 Hz. The SB mixer has two voices: the machine gun
loop (sample 0, repeated while firing) and one effect voice. Each voice is pre-halved and
the sum (or voice + 0x40) is written as unsigned 8-bit.

Names below are inferred from where the game plays them.

| # | sound |
|---|---|
| 0 | machine gun loop |
| 1-8 | death screams |
| 9-10 | player hit |
| 11-14 | ambient |
| 15 | reload |
| 16 | level start |
| 17 | medic |
| 18 | player death |
