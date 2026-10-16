/*
 * main.c - entry point of the portable RYSSÄN KAUHU.
 *
 * Usage: ryssankauhu-sdl [--fullscreen] [--sens N] [path-to-original-game-directory]
 *
 * The original data file GRAPH.DAT is needed at runtime. Without an argument
 * the game looks in the current directory, then next to the executable, then
 * in a "data" folder next to the executable. In the web version the file is
 * packed into the page at /game.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "files.h"
#include "game.h"
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#define TITLE "Ryss\xC3\xA4n Kauhu"

/* Uses d as the game directory if it contains GRAPH.DAT; returns 1 on success. */
static int try_dir(const char *d)
{
    if (game_file_exists(d, "GRAPH.DAT")) {
        files_set_dir(d);
        return 1;
    }
    return 0;
}

/* Prints the command line options. */
static void usage(void)
{
    printf("RYSSAN KAUHU - portable C / SDL2 port\n"
           "usage: ryssankauhu-sdl [options] [path-to-original-game-directory]\n"
           "  -f, --fullscreen   start in fullscreen\n"
           "  -s, --sens N       mouse sensitivity multiplier (default 1.0)\n"
           "  -h, --help         this text\n");
}

/* Parses the command line, locates the original game files, loads them and
   runs the game. */
int main(int argc, char **argv)
{
    int found = 0, fullscreen = 0, i;
    const char *dir = NULL;
    char err[256];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if ((!strcmp(argv[i], "-s") || !strcmp(argv[i], "--sens")) && i + 1 < argc) {
            plat_mouse_sens = (float)atof(argv[++i]);
            if (plat_mouse_sens <= 0.0f) plat_mouse_sens = 1.0f;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (!strncmp(argv[i], "-psn_", 5)) {
            /* macOS Finder process serial number */
        } else dir = argv[i];
    }
    if (dir) found = try_dir(dir);
#ifdef __EMSCRIPTEN__
    if (!found) found = try_dir("/game");     /* packed into the web page */
#endif
    if (!found) found = try_dir("");
    if (!found) {
        char *base = SDL_GetBasePath();
        if (base) {
            char buf[1200];
            found = try_dir(base);
            if (!found) { snprintf(buf, sizeof buf, "%sdata", base); found = try_dir(buf); }
            if (!found) { snprintf(buf, sizeof buf, "%s../Resources", base); found = try_dir(buf); }
            SDL_free(base);
        }
    }
    if (!found) {
        const char *msg =
            "Original RYSS\xC3\x84N KAUHU game file not found.\n\n"
            "Copy GRAPH.DAT into the \"data\" folder next to this program,\n"
            "or pass the game directory as the first argument.";
        fprintf(stderr, "%s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, msg, NULL);
        return 1;
    }
    if (!game_load_assets(err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, err, NULL);
        return 1;
    }
    if (!plat_init(TITLE, fullscreen)) return 1;
    game_main();
    plat_shutdown();
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.onGameExit) Module.onGameExit(); });   /* web page: "play again" */
#endif
    return 0;
}
