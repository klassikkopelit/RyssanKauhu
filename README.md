# RYSSÄN KAUHU – portable C / SDL2 port

A reverse-engineered, portable re-implementation of **RYSSÄN KAUHU**, a Finnish
DOS shooter from the 1990s. The game is set in the Winter War: you sit behind
a machine gun and stop the waves of Soviet soldiers walking towards you across an
open field. Graphics by Tuomas Aikioniemi, sound by Aleksi Ala-Mononen
("this product has been finded by sheepart's products").

The port is a function-by-function translation of the original 32-bit Watcom C
program (disassembled from `RYSS.EXE`, a DOS/4GW LE executable) to C99, with SDL2
standing in for VGA mode 13h, the 100 Hz timer interrupt, the keyboard interrupt,
the INT 33h mouse driver and the Sound Blaster. The hand-written asm module of the
original (sprite scaler, font, Sound Blaster mixer) is re-implemented in
`src/gfx.c` and `src/sound.c`. See `docs/REVERSE_ENGINEERING.md` for the details.

**You need the original game files.** The port reads `GRAPH.DAT` at runtime
(graphics, fonts, sounds and lookup tables are all in it).

## Building

Requirements: a C99 compiler, CMake ≥ 3.10 and SDL2 (2.0.10 or newer).

### Linux
```sh
sudo apt install build-essential cmake libsdl2-dev     # Debian/Ubuntu
# sudo dnf install gcc cmake SDL2-devel                # Fedora
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS
```sh
brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows – Visual Studio
Install SDL2 with vcpkg (`vcpkg install sdl2:x64-windows`) or download
`SDL2-devel-2.x.x-VC.zip` from <https://github.com/libsdl-org/SDL/releases>, then
```bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
rem   or: -DSDL2_DIR=C:/SDL2-2.x.x/cmake
cmake --build build --config Release
```
`SDL2.dll` is copied next to `ryssankauhu-sdl.exe` automatically.

### Windows – MinGW / MSYS2
```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-SDL2
cmake -S . -B build -G "MinGW Makefiles" && cmake --build build
```

There is also a plain makefile for systems with `sdl2-config`: `make -f Makefile.unix`.

### Web version (WebAssembly)

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
(version 6.0.12 is what the GitHub workflow uses), then:
```sh
source ~/emsdk/emsdk_env.sh            # once per terminal window
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web 8000
```
and open <http://localhost:8000>. The page has to come from a web server;
opening `index.html` straight from disk does not work. `assets/GRAPH.DAT` is
packed into the page (`index.data`). On touch screens a finger aims and fires,
and while the game waits for a key (Enter, Space, K) a tap presses it
(`plat_waiting_for_key()` in `src/platform.c`); the page adds an Esc button.
The web-only code is inside `#ifdef __EMSCRIPTEN__` blocks and in
`web/shell.html`, so the desktop builds are unchanged.

### Prebuilt releases (GitHub Actions)

`.github/workflows/build-release.yml` builds Windows (x64), macOS (universal)
and Linux (x64) zips on every push to `main` and on pull requests; the zips are
attached to the workflow run as artifacts. Pushing a version tag also publishes
them as a GitHub release:
```sh
git tag v1.0.0
git push origin v1.0.0
```
Each zip holds the executable, `readme.txt` (from `packaging/`), the
libraries it needs (`SDL2.dll` on Windows; on macOS and Linux SDL2 is linked
statically) and an empty `data/` folder for the original game file, which is
never included.

`.github/workflows/web.yml` builds the web version and publishes it on GitHub
Pages on every push to `main` (one-time setup: Settings → Pages → Source:
"GitHub Actions").

## Running

The program is called `ryssankauhu-sdl`. Run it inside the folder with
`GRAPH.DAT`, or give the folder as an argument:
```sh
./build/ryssankauhu-sdl /path/to/ryssankauhu
./build/ryssankauhu-sdl --fullscreen --sens 1.5 /path/to/ryssankauhu
```
If no argument is given the game looks in the current directory, then next to
the executable, then in a `data` folder next to the executable. So you can
either put `GRAPH.DAT` into a `data` folder beside `ryssankauhu-sdl.exe` (the
layout of the release zips), or drop the program into the original game folder,
and double-click it.

Controls are those of the original:

| | |
|---|---|
| Mouse | aim |
| Left button or Ctrl | fire |
| Left / Right arrow | aim with the keyboard |
| Enter | start (title screen) |
| Space | next level |
| K / E | new game? yes (*kyllä*) / no (*ei*) |
| Esc | quit |
| Alt+Enter, F11 | toggle fullscreen |

Eight hits and you are dead (the red crosses at the bottom left). Once per level
a medic patches you up, and an empty gun gets 50 more rounds five seconds later.
Clear six levels to win. *KUOLIT* = you died, *KENTTÄ LÄPI!* = level cleared,
*Paina välilyöntiä* = press space, *VOITIT!!!* = you won, *Uusi peli (k/e)?* =
new game (y/n)?

## How faithful is it?

* Same screens, texts (in Finnish), colours, layout and timings: the game logic
  runs on the original's 100 Hz timer, with the original level table, spawn
  logic, enemy accuracy table and hit detection.
* The renderer keeps the original's four-buffer, dirty-row scheme, and the
  scaled sprite routine reproduces the original's 8.8 fixed point stepping, so
  the soldiers look pixel for pixel the same.
* Sound goes through a C copy of the original two-voice Sound Blaster mixer
  (8000 Hz, 8-bit), including the machine gun loop and the rule that only the
  important sounds may interrupt one already playing.
* The "SIVARI" cheat still works.

Small, deliberate differences:
* Sprites are clipped at the screen edges (the original let them wrap around).
* The mouse is read in relative mode and scaled so that sweeping it across the
  window covers the whole field of fire, like the original's doubled mouse
  speed did on a 640-wide mouse range. `--sens` adjusts it.
* The Gravis Ultrasound code is not ported; sound always uses the SB path.
* The "Loading: ......" text the original printed while reading `GRAPH.DAT` in
  text mode is not shown.

## Source layout

| File | Purpose |
|---|---|
| `src/game.c` | game logic, translated function by function (original addresses in comments) |
| `src/gfx.c` | the asm module's sprite, scaled sprite, font and screen copy routines |
| `src/sound.c` | Sound Blaster sample setup and the two-voice mixer |
| `src/data.c` | `GRAPH.DAT` loader and decryption |
| `src/platform.c` | SDL2: VGA mode 13h, PIT timer, keyboard interrupt, INT 33h mouse, audio |
| `src/files.c` | case-insensitive access to the DOS file names |
| `tools/le_unpack.py` | loads `RYSS.EXE`, applies the LE fixups and writes a flat image for disassembly |
| `tools/extract_assets.py` | dumps sprites, background, fonts and sounds from `GRAPH.DAT` to PNG/WAV |
| `web/shell.html` | the web page around the WebAssembly version |
| `assets/` | the original game files: `GRAPH.DAT` (packed into the web version) and `RYSS.EXE` |
