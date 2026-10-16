/*
 * gfx.h - 320x200x256 software renderer (the hand-written asm module of RYSS.EXE)
 */
#ifndef RK_GFX_H
#define RK_GFX_H

#include <stdint.h>

#define SCR_W 320
#define SCR_H 200
#define SCR_SIZE (SCR_W * SCR_H)

/* The 64000-byte buffers of the original program (the fourth is vga_vram) */
extern uint8_t scr_clean[SCR_SIZE];  /* background + corpses (no live sprites)    */
extern uint8_t scr_back[SCR_SIZE];   /* back buffer, sprites are drawn here       */
extern uint8_t scr_bg[SCR_SIZE];     /* pristine background picture               */

/* Dirty tracking used by flip_dirty() */
extern uint32_t dirty_top, dirty_bottom; /* row range [top, bottom)               */
extern uint8_t  dirty_gun;               /* 128x64 gun area at (96,136)           */
extern uint8_t  dirty_hud;               /* 80x50 damage area at (0,150)          */

/* screen x offset for each aim angle (0..0x3FFF), built from the sin table */
extern int16_t angle_to_x[0x4000];

void gfx_init_tables(void);

/* Sprites are drawn into scr_back, centred on (x, y). */
void gfx_sprite(int frame, int x, int y);
/* scale is 8.8 fixed point (0x100 = 1:1). Updates dirty_top/bottom. */
void gfx_sprite_scaled(int frame, int scale, int x, int y);

/* Font: size 32, 16 or 8. Draws into the given buffer at byte offset pos. */
void gfx_char(uint8_t *dst, int pos, int size, int ch, uint8_t color);
void gfx_string(uint8_t *dst, int pos, int size, const char *s, uint8_t color);

void gfx_copy_screen(uint8_t *dst, const uint8_t *src);
/* copy dirty rows plus optional gun / hud rectangles */
void gfx_flip_dirty(uint8_t *dst, const uint8_t *src);
/* copy a w x h rectangle at byte offset pos */
void gfx_copy_rect(uint8_t *dst, const uint8_t *src, int pos, int w, int h);

#endif
