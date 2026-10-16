/*
 * platform.h - SDL2 replacement for the DOS / DOS4GW services that RYSSÄN
 * KAUHU used: VGA mode 13h and the DAC, the PIT timer interrupt (reprogrammed
 * to 100 Hz), the keyboard interrupt (INT 9), the INT 33h mouse driver and the
 * Sound Blaster.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/* ---- lifecycle ---- */
int  plat_init(const char *title, int fullscreen);
void plat_shutdown(void);

/* ---- video (VGA) ---- */
extern uint8_t vga_vram[64000];         /* linear frame buffer at 0xA0000 in mode 13h */
void vga_set_mode(int mode);            /* 0x03 = text (shown black), 0x13 = 320x200x256 */
void vga_set_palette(const uint8_t *pal6); /* 768 bytes, 6-bit DAC values */
/* Handles events, shows vga_vram and waits for the next 70 Hz frame. The game
   calls this where the original looped on its own (it never waited for retrace,
   so the original frame rate was simply "as fast as the PC could"). */
void vga_present(void);

/* ---- PIT timer interrupt ---- */
/* Calls isr() hz times per second (from the main thread, whenever the
   platform gets control). */
void timer_install(int hz, void (*isr)(void));
void timer_remove(void);

/* ---- keyboard interrupt (INT 9) ---- */
/* isr() gets every PC/XT set 1 scancode: make codes, and break codes with bit 7 set. */
void keyboard_install(void (*isr)(uint8_t scancode));
void keyboard_remove(void);

/* ---- INT 33h mouse ---- */
int  mouse_reset(void);                       /* AX=00h, returns nonzero if a mouse is present */
void mouse_set_hrange(int min, int max);      /* AX=07h */
void mouse_setpos(int x, int y);              /* AX=04h */
void mouse_get(int *x, int *y, int *buttons); /* AX=03h, bit 0 = left button */
/* Desktop: the game has finished with the mouse, give the pointer back. */
void mouse_release(void);

/* ---- touch screens (web version) ---- */
/* Tells the platform which key the game is waiting for, so a finger tap can
   press it (Enter, Space, K...). 0 = the game is playing and taps are mouse
   input. On the desktop this also lets go of the mouse while the game waits. */
void plat_waiting_for_key(uint8_t scancode);

/* ---- audio device (Sound Blaster stand-in) ---- */
typedef void (*plat_audio_cb)(uint8_t *out, int len, void *user);
/* Opens an 8-bit unsigned mono device that calls cb to fill buffers. Returns
   the sample rate (SDL converts if the hardware wants something else), or 0. */
int  plat_audio_open(int rate, plat_audio_cb cb, void *user);
void plat_audio_lock(void);
void plat_audio_unlock(void);

/* ---- misc ---- */
uint32_t plat_ticks_ms(void);
extern float plat_mouse_sens;   /* --sens */

#endif
