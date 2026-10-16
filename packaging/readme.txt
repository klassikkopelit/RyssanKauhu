RYSSÄN KAUHU - portable C / SDL2 port
Version @VERSION@
https://github.com/klassikkopelit/ryssankauhu

A reverse-engineered port of RYSSÄN KAUHU, the Finnish DOS shooter from the
1990s set in the Winter War: you man a machine gun and stop the waves of
Soviet soldiers walking towards you across the field.


YOU NEED THE ORIGINAL GAME FILES
--------------------------------
This package contains only the game program. It reads the original data
file at runtime, so you need your own copy of the original game:

    GRAPH.DAT

Copy that file into the "data" folder of this package, then start
ryssankauhu-sdl. That's all.

Other ways that work too:
  - copy the files of this package into the original game folder and start
    ryssankauhu-sdl there, or
  - give the game folder on the command line:

        ryssankauhu-sdl /path/to/original/game
        ryssankauhu-sdl --fullscreen --sens 1.5 /path/to/original/game

--sens changes the mouse sensitivity (1.0 = sweeping the mouse across the
window covers the whole field of fire).

@PLATFORM_NOTES@

CONTROLS
--------
Aim                 mouse, or the left/right arrow keys
Fire                left mouse button or Ctrl
Start               Enter
Next level          Space
New game?           K = yes (kyllä), E = no (ei)
Quit                Esc
Fullscreen          Alt+Enter or F11

Eight hits and you are dead. Once per level a medic patches you up, and an
empty gun gets 50 more rounds after a few seconds. Clear six levels to win.


CREDITS
-------
RYSSÄN KAUHU: graphics Tuomas Aikioniemi, sound Aleksi Ala-Mononen.
"this product has been finded by sheepart's products."
SDL2 (https://libsdl.org) - zlib license, see LICENSE-SDL2.txt.
