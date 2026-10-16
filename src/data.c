/*
 * data.c - GRAPH.DAT loader (see data.h for the file format), 0x11ECD
 */
#include "data.h"
#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GameData gd;

typedef struct {
    const uint8_t *p;
    size_t pos, len;
    int bad;
} Reader;

static void rd_bytes(Reader *r, void *dst, size_t n)
{
    if (r->pos + n > r->len) {
        r->bad = 1;
        memset(dst, 0, n);
        return;
    }
    memcpy(dst, r->p + r->pos, n);
    r->pos += n;
}

static uint8_t rd_u8(Reader *r)
{
    uint8_t b = 0;
    rd_bytes(r, &b, 1);
    return b;
}

static uint16_t rd_u16(Reader *r)
{
    uint8_t b[2];
    rd_bytes(r, b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t rd_u32(Reader *r)
{
    uint8_t b[4];
    rd_bytes(r, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void fail(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0) {
        strncpy(err, msg, (size_t)errlen - 1);
        err[errlen - 1] = 0;
    }
}

/* 0x11ECD: loads and decrypts graph.dat from the game directory. */
int data_load(char *err, int errlen)
{
    size_t flen = 0;
    uint8_t *file;
    Reader r;
    uint16_t tab_unused[4096];
    uint32_t i, n;

    memset(&gd, 0, sizeof(gd));

    file = game_load("graph.dat", &flen);
    if (!file) {
        fail(err, errlen, "Unable to open file 'graph.dat'");
        return -1;
    }

    r.p = file;
    r.pos = 0;
    r.len = (size_t)flen;
    r.bad = 0;

    /* greyscale intro palette - not used by the game */
    r.pos += 0x300;

    gd.sprites_size = rd_u32(&r) ^ 0xF5D43DD7u;

    for (i = 0; i < 4096; i++) tab_unused[i] = rd_u16(&r);
    for (i = 0; i < 4096; i++) gd.tab_cos[i] = rd_u16(&r);
    for (i = 0; i < 4096; i++) gd.tab_sin[i] = rd_u16(&r);
    for (i = 0; i < 4096; i++) gd.tab_hitw[i] = rd_u16(&r);
    for (i = 0; i < 4096; i++) {
        uint16_t k = (uint16_t)(0x37EA - 2 * i);
        tab_unused[i] ^= k;
        gd.tab_cos[i] ^= k;
        gd.tab_sin[i] ^= k;
        gd.tab_hitw[i] ^= k;
    }
    (void)tab_unused;

    for (i = 0; i < 256; i++)
        gd.spr_offs[i] = rd_u32(&r) ^ (i + 0xCC96C330u);

    n = rd_u32(&r);
    if (n > MAX_SAMPLES || r.bad) {
        free(file);
        fail(err, errlen, "graph.dat: bad sample table");
        return -1;
    }
    gd.num_samples = (int)n;
    for (i = 0; i < n; i++) {
        uint32_t len = rd_u32(&r);
        if (r.bad || r.pos + len > r.len) {
            free(file);
            data_free();
            fail(err, errlen, "graph.dat: truncated sample data");
            return -1;
        }
        gd.samples[i].len = len;
        gd.samples[i].data = (uint8_t *)malloc(len ? len : 1);
        rd_bytes(&r, gd.samples[i].data, len);
    }

    gd.font_size = rd_u32(&r);
    if (r.bad || r.pos + 0xC00 + gd.font_size > r.len) {
        free(file);
        data_free();
        fail(err, errlen, "graph.dat: truncated font data");
        return -1;
    }
    for (i = 0; i < 768; i++)
        gd.font_offs[i] = rd_u32(&r) ^ 0x0041FBE3u;
    gd.font = (uint8_t *)calloc(1, gd.font_size + 0xC00);
    rd_bytes(&r, gd.font + 0xC00, gd.font_size);

    rd_bytes(&r, gd.palette, 768);
    for (i = 0; i < 768; i++)
        gd.palette[i] ^= (uint8_t)(i * 3 + i + 0x84);

    /* background: RLE, key depends on the output position at the start of a run */
    {
        uint32_t o = 0;
        while (o < 64000 && !r.bad) {
            uint8_t key = (uint8_t)(o * 3 + 0x9D);
            uint8_t b = rd_u8(&r) ^ key;
            if ((b & 0xC0) == 0xC0) {
                uint32_t cnt = b & 0x3F;
                uint8_t v = rd_u8(&r) ^ key;
                while (cnt-- && o < 64000) gd.background[o++] = v;
            } else {
                gd.background[o++] = b;
            }
        }
    }

    if (r.bad || r.pos + gd.sprites_size > r.len) {
        free(file);
        data_free();
        fail(err, errlen, "graph.dat: truncated sprite data");
        return -1;
    }
    gd.sprites = (uint8_t *)malloc(gd.sprites_size);
    rd_bytes(&r, gd.sprites, gd.sprites_size);
    for (i = 0; i < gd.sprites_size; i++)
        gd.sprites[i] ^= (uint8_t)(i * 6 + 0x9A);

    /* sanity check sprite headers */
    for (i = 0; i < NUM_SPRITES; i++) {
        if (gd.spr_offs[i] + 0x204 > gd.sprites_size) {
            free(file);
            data_free();
            fail(err, errlen, "graph.dat: bad sprite table");
            return -1;
        }
    }

    free(file);
    return 0;
}

void data_free(void)
{
    int i;
    for (i = 0; i < MAX_SAMPLES; i++) {
        free(gd.samples[i].data);
        gd.samples[i].data = NULL;
    }
    free(gd.font);
    gd.font = NULL;
    free(gd.sprites);
    gd.sprites = NULL;
}
