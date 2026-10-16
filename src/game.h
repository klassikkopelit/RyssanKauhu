#ifndef GAME_H
#define GAME_H
/* Loads graph.dat from the game directory; returns 1 on success, otherwise
   0 with a message in err. */
int  game_load_assets(char *err, int errlen);
/* main() of RYSS.EXE: title screen, the game, back to "DOS". */
void game_main(void);
#endif
