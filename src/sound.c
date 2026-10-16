/*
 * sound.c - the Sound Blaster part of RYSS.EXE: sample preparation (0x105ED),
 * effect start (0x1041E) and the two-voice mixer (asm routine at 0x20051) that
 * the SB interrupt handler ran for every 256 byte half of the DMA buffer.
 * The Gravis Ultrasound code of the original is not ported.
 */
#include "sound.h"
#include "data.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

#define GUN_RING 0x800

static uint8_t *proc[MAX_SAMPLES];   /* samples converted to (s ^ 0x80) >> 1 */
static uint32_t proc_dw[MAX_SAMPLES]; /* length in dwords                     */
static uint8_t  gun_ring[GUN_RING];  /* sample 0 repeated to fill 2 KB       */
static uint32_t gun_len_dw;

static const uint8_t *eff_ptr;       /* 0x30804 */
static uint32_t eff_dw;              /* 0x30808 */
static uint32_t gun_pos;             /* 0x30810 */

volatile uint8_t sound_gun_on;       /* 0x30B22 */

static uint8_t  block[SND_BLOCK];
static int      block_pos = SND_BLOCK;

static void audio_cb(uint8_t *out, int len, void *user);

/* 0x10B75 / 0x105ED: converts the samples to the mixer format, builds the
   machine gun ring buffer and starts the output. */
void sound_init(void)
{
    int i;
    uint32_t k;

    sound_shutdown();
    for (i = 0; i < gd.num_samples; i++) {
        uint32_t len = gd.samples[i].len;
        proc[i] = (uint8_t *)malloc(len ? len : 1);
        if (!proc[i]) continue;
        for (k = 0; k < len; k++)
            proc[i][k] = (uint8_t)((gd.samples[i].data[k] ^ 0x80) >> 1);
        proc_dw[i] = len >> 2;
    }

    memset(gun_ring, 0x40, sizeof(gun_ring));
    gun_len_dw = 0;
    if (gd.num_samples > 0 && proc[0] && gd.samples[0].len) {
        uint32_t len = gd.samples[0].len;
        for (k = 0; k < GUN_RING; k++)
            gun_ring[k] = proc[0][k % len];
        gun_len_dw = len >> 2;
        /* keep the read window inside the ring buffer */
        if (gun_len_dw * 4 + SND_BLOCK > GUN_RING) gun_len_dw = (GUN_RING - SND_BLOCK) / 4;
    }
    eff_ptr = NULL;
    eff_dw = 0;
    gun_pos = 0;
    sound_gun_on = 0;
    block_pos = SND_BLOCK;
    plat_audio_open(SND_RATE, audio_cb, NULL);
}

/* Audio device callback. */
static void audio_cb(uint8_t *out, int len, void *user)
{
    (void)user;
    sound_mix(out, len);
}

/* 0x10B98: frees the converted samples. */
void sound_shutdown(void)
{
    int i;
    plat_audio_lock();
    eff_ptr = NULL;
    eff_dw = 0;
    sound_gun_on = 0;
    plat_audio_unlock();
    for (i = 0; i < MAX_SAMPLES; i++) {
        free(proc[i]);
        proc[i] = NULL;
        proc_dw[i] = 0;
    }
    eff_ptr = NULL;
    eff_dw = 0;
}

/* 0x1041E, Sound Blaster branch */
void sound_play(int n)
{
    if (n < 0 || n >= gd.num_samples || !proc[n]) return;
    if (n == 0) {
        gun_pos = 0;
        return;
    }
    /* only one effect voice; the important sounds may interrupt */
    if (eff_dw != 0 && !(n >= SND_RELOAD && n <= SND_DEATH))
        return;
    eff_ptr = proc[n];
    eff_dw = proc_dw[n];
}

/* 0x20051: mixes one 256 byte block. Each voice is stored pre-halved, so the
   gun + effect sum fits a byte; a lone voice gets 0x40 added instead. */
static void mix_block(uint8_t *out)
{
    uint32_t n = SND_BLOCK / 4; /* dwords left in this block */
    uint32_t i, c, p;

    for (;;) {
        if (sound_gun_on && gun_len_dw) {
            const uint8_t *g;
            p = gun_pos % gun_len_dw;
            gun_pos = n + p;
            g = gun_ring + p * 4;
            if (eff_dw) {
                c = eff_dw < n ? eff_dw : n;
                for (i = 0; i < c * 4; i++)
                    *out++ = (uint8_t)(g[i] + eff_ptr[i]);
                eff_ptr += c * 4;
                eff_dw -= c;
                n -= c;
                if (!n) return;
            } else {
                for (i = 0; i < n * 4; i++)
                    *out++ = (uint8_t)(g[i] + 0x40);
                return;
            }
        } else {
            gun_pos = 0;
            if (eff_dw) {
                c = eff_dw < n ? eff_dw : n;
                for (i = 0; i < c * 4; i++)
                    *out++ = (uint8_t)(eff_ptr[i] + 0x40);
                eff_ptr += c * 4;
                eff_dw -= c;
                n -= c;
                if (!n) return;
            } else {
                memset(out, 0x80, n * 4);
                return;
            }
        }
    }
}

/* Hands out the mixer output in whatever buffer size the device asks for. */
void sound_mix(uint8_t *out, int len)
{
    while (len > 0) {
        int c;
        if (block_pos >= SND_BLOCK) {
            mix_block(block);
            block_pos = 0;
        }
        c = SND_BLOCK - block_pos;
        if (c > len) c = len;
        memcpy(out, block + block_pos, (size_t)c);
        block_pos += c;
        out += c;
        len -= c;
    }
}
