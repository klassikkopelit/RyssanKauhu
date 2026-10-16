/*
 * sound.h - the Sound Blaster driver and mixer of RYSS.EXE
 *
 * The original streams 8-bit unsigned mono at 8000 Hz to the SB in 256 byte
 * blocks. There are two voices: a looping machine gun voice that plays while
 * the trigger is held, and a single sound effect voice.
 */
#ifndef RK_SOUND_H
#define RK_SOUND_H

#include <stdint.h>

#define SND_RATE  8000
#define SND_BLOCK 256

enum {
    SND_GUN = 0,          /* looping machine gun */
    SND_SCREAM1 = 1,      /* 1..8 enemy death screams */
    SND_HIT1 = 9,         /* 9..10 player hit */
    SND_AMBIENT1 = 11,    /* 11..14 random battlefield noise */
    SND_RELOAD = 15,
    SND_LEVELSTART = 16,
    SND_MEDIC = 17,
    SND_DEATH = 18
};

/* 0x10B75: prepare the samples (after data_load) and start the output */
void sound_init(void);
/* 0x10B98: stop the output */
void sound_shutdown(void);

/* 0x1041E: start a sound effect (the caller locks the audio device) */
void sound_play(int n);
/* Machine gun voice on/off (0x30B22 in the original). */
extern volatile uint8_t sound_gun_on;

/* Produces len bytes of 8-bit unsigned audio (the SB interrupt handler at
   0x1039F mixing into the DMA buffer). Called from the audio thread. */
void sound_mix(uint8_t *out, int len);

#endif
