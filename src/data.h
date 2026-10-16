/*
 * data.h - GRAPH.DAT loader
 *
 * GRAPH.DAT layout (reverse engineered from RYSS.EXE, all values little endian):
 *
 *   0x300 bytes     greyscale VGA palette (only used by an unused intro)
 *   u32             sprite data size  ^ 0xF5D43DD7
 *   4 x 4096 u16    lookup tables, each word ^ (0x37EA - 2*i)
 *                     t0: unused, t1: cos*256, t2: sin*256, t3: hit width by distance
 *   256 x u32       sprite offsets, each ^ (i + 0xCC96C330)
 *   u32             number of sound samples
 *   n x { u32 len; len bytes signed 8-bit PCM @ 8000 Hz }
 *   u32             font data size
 *   768 x u32       font glyph offsets (3 sizes x 256 chars), each ^ 0x0041FBE3
 *   ...             font data (offsets are relative to the start of the offset table)
 *   0x300 bytes     game VGA palette (6-bit), byte i ^ (4*i + 0x84)
 *   ...             320x200 background, PCX-style RLE, byte ^ (3*pos + 0x9D)
 *   ...             sprite data, byte i ^ (6*i + 0x9A)
 */
#ifndef RK_DATA_H
#define RK_DATA_H

#include <stdint.h>

#define NUM_SPRITES   29
#define MAX_SAMPLES   32

typedef struct {
    uint8_t  *data;   /* raw signed 8-bit PCM */
    uint32_t  len;
} Sample;

typedef struct {
    uint8_t  palette[768];          /* 6-bit VGA palette */
    uint8_t  background[64000];
    uint16_t tab_cos[4096];         /* 0x32B30 in the original */
    uint16_t tab_sin[4096];         /* 0x34B30 */
    uint16_t tab_hitw[4096];        /* 0x36B30 */
    uint32_t spr_offs[256];
    uint8_t *sprites;               /* decrypted sprite blob */
    uint32_t sprites_size;
    uint32_t font_offs[768];
    uint8_t *font;                  /* font blob, offsets relative to this pointer */
    uint32_t font_size;
    Sample   samples[MAX_SAMPLES];
    int      num_samples;
} GameData;

extern GameData gd;

/* Loads graph.dat from the game directory (files.c). Returns 0 on success,
   otherwise writes a message into err. */
int data_load(char *err, int errlen);
void data_free(void);

#endif
