/*
 * gfx.c - the drawing routines of the hand-written 32-bit asm module that
 * RYSS.EXE carries in its second object (0x20000-0x205E0), in C.
 *
 * These routines are C versions of the original asm. They reproduce its behaviour (including the fixed point maths of
 * the scaled sprite routine) but clip against the screen edges, which the
 * original did not do.
 */
#include "gfx.h"
#include "data.h"
#include "platform.h"

#include <string.h>

uint8_t scr_clean[SCR_SIZE];
uint8_t scr_back[SCR_SIZE];
uint8_t scr_bg[SCR_SIZE];

uint32_t dirty_top, dirty_bottom;
uint8_t  dirty_gun, dirty_hud;

int16_t angle_to_x[0x4000];

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 0x11DED: screen x for every aim angle, x = 160 * sin(a). The 8.8 fixed
   point tables the original built for the scaled blitter are computed on the
   fly in gfx_sprite_scaled(). */
void gfx_init_tables(void)
{
    int a;
    for (a = 0; a < 0x4000; a++) {
        int idx = ((a - 0x2000) & 0xFFFF) >> 4;
        int v = (int16_t)gd.tab_sin[idx] * 160;
        angle_to_x[a] = (int16_t)(v / 256); /* truncates towards zero, like the original */
    }
}

/* Plots a sprite pixel into the back buffer, clipped to the screen. */
static void put_back(int x, int y, uint8_t c)
{
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H)
        scr_back[y * SCR_W + x] = c;
}

static const uint8_t *sprite_ptr(int frame)
{
    if (frame < 0 || frame >= NUM_SPRITES) return NULL;
    return gd.sprites + gd.spr_offs[frame];
}

/*
 * Sprite format: u16 w, u16 h, 128 x u32 row offsets, then RLE rows:
 *   0xFF n   : skip n pixels
 *   0xFE     : end of row
 *   n (<FE)  : n literal pixels follow
 */
/* 0x202F0 */
void gfx_sprite(int frame, int x, int y)
{
    const uint8_t *spr = sprite_ptr(frame);
    const uint8_t *src, *end;
    int w, h, row = 0, col = 0;

    if (!spr) return;
    end = gd.sprites + gd.sprites_size;
    w = rd16(spr);
    h = rd16(spr + 2);
    x -= w >> 1;
    y -= h >> 1;
    src = spr + 0x204;

    while (row < h && src < end) {
        uint8_t c = *src++;
        if (c == 0xFF) {
            col += *src++;
        } else if (c == 0xFE) {
            row++;
            col = 0;
        } else {
            int i;
            if (src + c > end) break;
            for (i = 0; i < c; i++)
                put_back(x + col + i, y + row, src[i]);
            src += c;
            col += c;
        }
    }
}

/* 0x20392: scale is 8.8 fixed point; the original patched the integer part
   of the source step into its own code. */
void gfx_sprite_scaled(int frame, int s, int x, int y)
{
    const uint8_t *spr = sprite_ptr(frame);
    const uint8_t *src, *end;
    uint16_t inv;
    uint8_t step_frac;
    int step_int, w, h, sh, k;

    if (s == 0x100) {
        gfx_sprite(frame, x, y);
        return;
    }
    if (!spr) return;
    if (s < 2) s = 2;
    if (s > 0xFF) s = 0xFF;

    end = gd.sprites + gd.sprites_size;
    inv = (uint16_t)(0x10000 / s);           /* table at 0x80B30 */
    step_int = (uint8_t)((inv >> 8) - 1);    /* self-modified immediate */
    step_frac = (uint8_t)(inv & 0xFF);

    w = rd16(spr);
    h = rd16(spr + 2);
    x -= (uint8_t)(((w >> 1) * s) >> 8);     /* table at 0x40B30 */
    sh = (uint8_t)((h * s) >> 8);
    y -= sh >> 1;
    if (sh <= 0) return;

    if (y >= 0 && (uint32_t)y < dirty_top) dirty_top = (uint32_t)y;

    src = spr + 0x204;
    for (k = 0; k < sh; k++) {
        uint16_t bx = 0;
        int col = 0;

        if (k > 0) {
            /* the row table was reversed at load time; this picks source row
               128 - (((sh-k) << 8) / s) exactly like the original */
            int e = (uint8_t)(((sh - k) << 8) / s);  /* table at 0x50B30 */
            if (e < 1) e = 1;
            if (e > 128) e = 128;
            src = spr + rd32(spr + 4 + 4 * (128 - e));
        }

        while (src < end) {
            uint8_t c = *src++;
            if (c == 0xFF) {
                bx = (uint16_t)(bx + *src++ * s);
                col += bx >> 8;
                bx &= 0xFF;
            } else if (c == 0xFE) {
                break;
            } else {
                const uint8_t *next = src + c;
                int nout;
                uint8_t acc = 0;
                bx = (uint16_t)(bx + c * s);     /* table at 0x60B30 */
                nout = bx >> 8;
                bx &= 0xFF;
                while (nout-- > 0) {
                    unsigned t = (unsigned)acc + step_frac;
                    acc = (uint8_t)t;
                    if (src < end) put_back(x + col, y + k, *src);
                    col++;
                    src += 1 + step_int + (t >> 8);
                }
                src = next;
            }
        }
    }

    if ((uint32_t)(y + sh) > dirty_bottom) dirty_bottom = (uint32_t)(y + sh);
}

/* 0x201D6: glyph bytes: odd = draw b>>1 pixels, even = skip b>>1, 0 = next row */
void gfx_char(uint8_t *dst, int pos, int size, int ch, uint8_t color)
{
    const uint8_t *src, *end;
    int set, x0, y0, row = 0, col = 0;

    switch (size) {
    case 8:  set = 2; break;
    case 16: set = 1; break;
    default: set = 0; size = 32; break;
    }
    if (!gd.font) return;
    src = gd.font + gd.font_offs[set * 256 + (ch & 0xFF)];
    end = gd.font + gd.font_size + 0xC00;
    if (src < gd.font || src >= end) return;

    x0 = pos % SCR_W;
    y0 = pos / SCR_W;

    while (row < size && src < end) {
        uint8_t b = *src++;
        int n = b >> 1;
        if (b & 1) {
            while (n-- > 0) {
                int x = x0 + col, y = y0 + row;
                if ((unsigned)x < SCR_W && (unsigned)y < SCR_H)
                    dst[y * SCR_W + x] = color;
                col++;
            }
        } else if (n) {
            col += n;
        } else {
            row++;
            col = 0;
        }
    }
}

/* 0x20243 */
void gfx_string(uint8_t *dst, int pos, int size, const char *s, uint8_t color)
{
    while (*s) {
        gfx_char(dst, pos, size, (uint8_t)*s, color);
        pos += size;
        s++;
    }
}

/* 0x201C8 */
void gfx_copy_screen(uint8_t *dst, const uint8_t *src)
{
    memcpy(dst, src, SCR_SIZE);
}

/* Copies a w x h rectangle at byte offset pos (inlined loops in the original). */
void gfx_copy_rect(uint8_t *dst, const uint8_t *src, int pos, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        int o = pos + y * SCR_W;
        if (o < 0 || o + w > SCR_SIZE) continue;
        memcpy(dst + o, src + o, (size_t)w);
    }
}

/* 0x2025D: copies the dirty rows, plus the gun and damage areas if flagged */
void gfx_flip_dirty(uint8_t *dst, const uint8_t *src)
{
    uint32_t top = dirty_top, bot = dirty_bottom;
    if (bot > SCR_H) bot = SCR_H;
    if (top < bot)
        memcpy(dst + top * SCR_W, src + top * SCR_W, (bot - top) * SCR_W);
    if (dirty_gun)
        gfx_copy_rect(dst, src, 136 * SCR_W + 96, 128, 64);
    if (dirty_hud)
        gfx_copy_rect(dst, src, 150 * SCR_W + 0, 80, 50);
}
