/*
 * platform.c - SDL2 implementation of the DOS-era services used by RYSSÄN KAUHU.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#ifdef __EMSCRIPTEN__
/* Web version: the game's blocking loops hand control back to the browser
   whenever they wait (needs -sASYNCIFY, see CMakeLists.txt). */
#  include <emscripten.h>
#  define plat_sleep(ms) emscripten_sleep(ms)
#else
#  define plat_sleep(ms) SDL_Delay(ms)
#endif

#define FRAME_HZ      70.086   /* VGA 320x200 vertical refresh */
#define ASPECT_W      4        /* mode 13h fills a 4:3 CRT screen */
#define ASPECT_H      3

uint8_t vga_vram[64000];
float   plat_mouse_sens = 1.0f;

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex_gfx;   /* 320x200 */
static SDL_Texture  *tex_big;   /* tex_gfx blown up by whole numbers */
static int           big_w, big_h;
static SDL_AudioDeviceID audio_dev;

static int      cur_mode = 0x03;
static uint32_t pal_rgb[256];
static uint32_t pix_gfx[320 * 200];

/* timing */
static uint64_t perf_freq, next_frame;
static uint32_t frame_count;

/* PIT */
static void   (*timer_isr)(void);
static uint64_t timer_step, timer_next;

/* keyboard */
static void   (*kb_isr)(uint8_t);
static uint8_t  tap_key;           /* key a touch tap presses (0 = none)  */
static uint8_t  pending_break;     /* key to release a few frames later  */
static uint32_t pending_break_frame;

/* mouse */
static int    mouse_present_flag;  /* game has reset the driver          */
static int    mouse_x, mouse_y;    /* driver coordinates                 */
static int    mouse_hmin, mouse_hmax = 639;
static int    mouse_btn;
static int    mouse_click_latch;   /* a press released before the game polled */
static int    mouse_captured;      /* desktop: relative mouse mode       */
static double mouse_accum;

/* Returns milliseconds since SDL was initialised. */
uint32_t plat_ticks_ms(void) { return SDL_GetTicks(); }

/* Switches the window between windowed and desktop fullscreen. */
static void toggle_fullscreen(void)
{
    uint32_t f = SDL_GetWindowFlags(win);
    SDL_SetWindowFullscreen(win, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

/* Returns the largest 4:3 rectangle centred in an area of w x h. */
static SDL_Rect fit_4_3(int w, int h)
{
    SDL_Rect r;
    if (w * ASPECT_H > h * ASPECT_W) { r.h = h; r.w = h * ASPECT_W / ASPECT_H; }
    else                             { r.w = w; r.h = w * ASPECT_H / ASPECT_W; }
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

/* Converts an SDL scancode to the PC/XT set 1 scancode the keyboard
   controller would have sent, or 0 for keys the game never saw. */
static uint8_t xt_scancode(SDL_Scancode sc)
{
    static const uint8_t letters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, /* a-m */
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C  /* n-z */
    };
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return letters[sc - SDL_SCANCODE_A];
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (uint8_t)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc == SDL_SCANCODE_0) return 0x0B;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (uint8_t)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:       return 0x01;
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:     return 0x1C;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:        return 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:         return 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_KP_8:         return 0x48;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_KP_4:         return 0x4B;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_KP_6:         return 0x4D;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_KP_2:         return 0x50;
    default:                        return 0;
    }
}

/* Sends a scancode to the installed keyboard handler. */
static void kb_send(uint8_t code)
{
    if (kb_isr) kb_isr(code);
}

/* Presses a key now and releases it a few frames later, so that a game loop
   polling the key table sees it (touch taps, the web page's Esc button). */
static void kb_tap(uint8_t sc)
{
    if (pending_break) kb_send((uint8_t)(pending_break | 0x80));
    kb_send(sc);
    pending_break = sc;
    pending_break_frame = frame_count + 3;
}

/* Turns relative mouse mode on or off (desktop only). */
static void set_mouse_capture(int on)
{
#ifdef __EMSCRIPTEN__
    on = 0;   /* the web version follows the pointer / finger directly */
#endif
    if (on == mouse_captured) return;
    mouse_captured = on;
    mouse_accum = 0.0;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

/* Captures the mouse while the game plays and the window has focus. */
static void update_mouse_capture(void)
{
    int want = mouse_present_flag && !tap_key && win &&
               (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);
    set_mouse_capture(want);
}

/* Absolute pointer position -> driver x across the 4:3 picture. */
static void mouse_from_window(int wx)
{
    int ww, wh;
    SDL_Rect v;
    SDL_GetWindowSize(win, &ww, &wh);
    v = fit_4_3(ww, wh);
    if (v.w <= 0) return;
    mouse_x = mouse_hmin + (int)((long)(wx - v.x) * (mouse_hmax - mouse_hmin) / v.w);
    if (mouse_x < mouse_hmin) mouse_x = mouse_hmin;
    if (mouse_x > mouse_hmax) mouse_x = mouse_hmax;
}

/* Relative mouse motion -> driver x. Sweeping across the picture covers the
   whole horizontal range, like moving an absolute pointer would. */
static void mouse_from_motion(int xrel)
{
    int ww, wh, d;
    SDL_Rect v;
    SDL_GetWindowSize(win, &ww, &wh);
    v = fit_4_3(ww, wh);
    if (v.w <= 0) return;
    mouse_accum += (double)xrel * (mouse_hmax - mouse_hmin) * plat_mouse_sens / v.w;
    d = (int)mouse_accum;
    mouse_accum -= d;
    mouse_x += d;
    if (mouse_x < mouse_hmin) mouse_x = mouse_hmin;
    if (mouse_x > mouse_hmax) mouse_x = mouse_hmax;
}

/* Runs the timer handler for every 1/hz second that has passed. */
static void timer_poll(void)
{
    uint64_t now;
    int n = 0;
    if (!timer_isr || !timer_step) return;
    now = SDL_GetPerformanceCounter();
    if (timer_next == 0) timer_next = now + timer_step;
    while (now >= timer_next) {
        timer_next += timer_step;
        timer_isr();
        if (++n >= 25) {            /* after a stall, don't fast-forward */
            timer_next = now + timer_step;
            break;
        }
    }
}

#ifdef __EMSCRIPTEN__
/* Web version: lets the page press keys (the Esc button on touch screens).
   Takes a PC scancode. */
EMSCRIPTEN_KEEPALIVE void web_push_scancode(int sc)
{
    if (sc > 0 && sc < 0x80) kb_tap((uint8_t)sc);
}
#endif

/* Processes pending SDL events: quit, keys to the keyboard handler, mouse
   movement and buttons, and the timer. */
static void pump_events(void)
{
    SDL_Event e;
    if (pending_break && (int32_t)(frame_count - pending_break_frame) >= 0) {
        kb_send((uint8_t)(pending_break | 0x80));
        pending_break = 0;
    }
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            plat_shutdown();
            exit(0);
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Scancode s = e.key.keysym.scancode;
            uint8_t xt;
            if (e.type == SDL_KEYDOWN &&
                ((s == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) || s == SDL_SCANCODE_F11)) {
                if (!e.key.repeat) toggle_fullscreen();
                break;
            }
            if (e.key.repeat) break;     /* typematic repeat would only disturb the cheat code */
            xt = xt_scancode(s);
            if (xt) kb_send(e.type == SDL_KEYDOWN ? xt : (uint8_t)(xt | 0x80));
            break;
        }
        case SDL_MOUSEMOTION:
            if (mouse_captured) mouse_from_motion(e.motion.xrel);
            else mouse_from_window(e.motion.x);
            mouse_y = 100;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID && tap_key) {   /* touch: the awaited key */
                kb_tap(tap_key);
                break;
            }
            if (!mouse_captured) mouse_from_window(e.button.x);
            if (e.button.button == SDL_BUTTON_LEFT) { mouse_btn |= 1; mouse_click_latch = 1; }
            if (e.button.button == SDL_BUTTON_RIGHT) mouse_btn |= 2;
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) mouse_btn &= ~1;
            if (e.button.button == SDL_BUTTON_RIGHT) mouse_btn &= ~2;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                int sc;
                mouse_btn = 0;
                /* release everything, the keys might be let go elsewhere */
                for (sc = 1; sc < 0x80; sc++) kb_send((uint8_t)(sc | 0x80));
            }
            break;
        default: break;
        }
    }
    update_mouse_capture();
    timer_poll();
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */

/* Converts mode 13h video memory to RGB through the palette and updates the
   graphics texture. Text mode is shown as a black screen. */
static void render_gfx(void)
{
    int i;
    if (cur_mode == 0x13)
        for (i = 0; i < 64000; i++) pix_gfx[i] = pal_rgb[vga_vram[i]];
    else
        memset(pix_gfx, 0, sizeof pix_gfx);
    SDL_UpdateTexture(tex_gfx, NULL, pix_gfx, 320 * 4);
}

#ifdef RYSS_AUTOTEST
/* Scripted input + screenshots for automated testing (not in normal builds).
   IT_SCRIPT file lines: "<frame> key <scancode hex> <1 down|0 up>",
   "<frame> mouse <driver x> <buttons>", "<frame> shot <file.ppm>",
   "<frame> wshot <file.ppm>" (what the window shows),
   "<frame> winsize <w> <h>", "<frame> quit" */
static FILE *it_f;
static char  it_wshot[200];
static long  it_next = -1;
static char  it_line[256];
/* Reads the next frame-numbered command line from the test script. */
static void it_load_next(void)
{
    it_next = -1;
    while (it_f && fgets(it_line, sizeof it_line, it_f)) {
        if (sscanf(it_line, "%ld", &it_next) == 1) return;
    }
}
/* Saves what the renderer is about to show in the window as a PPM image. */
static void it_window_shot(const char *fn)
{
    int w, h;
    unsigned char *buf;
    FILE *o;
    SDL_GetRendererOutputSize(ren, &w, &h);
    buf = (unsigned char *)malloc((size_t)w * h * 3);
    if (!buf) return;
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGB24, buf, w * 3) == 0 && (o = fopen(fn, "wb")) != NULL) {
        fprintf(o, "P6\n%d %d\n255\n", w, h);
        fwrite(buf, 1, (size_t)w * h * 3, o);
        fclose(o);
    }
    free(buf);
}
/* Saves the current screen as a PPM image. */
static void it_shot(const char *fn)
{
    FILE *o = fopen(fn, "wb");
    int i;
    if (!o) return;
    fprintf(o, "P6\n320 200\n255\n");
    for (i = 0; i < 64000; i++) {
        uint32_t p = pix_gfx[i];
        fputc((p >> 16) & 255, o); fputc((p >> 8) & 255, o); fputc(p & 255, o);
    }
    fclose(o);
}
/* Runs all test script commands scheduled up to the current frame. */
static void it_run(void)
{
    static int inited;
    if (!inited) { const char *s = getenv("IT_SCRIPT"); inited = 1; if (s) it_f = fopen(s, "r"); it_load_next(); }
    while (it_next >= 0 && it_next <= (long)frame_count) {
        char cmd[32], a[200]; int x, y;
        unsigned sc;
        long fr;
        if (sscanf(it_line, "%ld %31s", &fr, cmd) == 2) {
            if (!strcmp(cmd, "key") && sscanf(it_line, "%ld %31s %x %d", &fr, cmd, &sc, &y) == 4) {
                kb_send((uint8_t)(y ? sc : (sc | 0x80)));
            } else if (!strcmp(cmd, "mouse") && sscanf(it_line, "%ld %31s %d %d", &fr, cmd, &x, &y) == 4) {
                mouse_x = x; mouse_btn = y;
            } else if (!strcmp(cmd, "shot") && sscanf(it_line, "%ld %31s %199s", &fr, cmd, a) == 3) {
                it_shot(a);
            } else if (!strcmp(cmd, "wshot") && sscanf(it_line, "%ld %31s %199s", &fr, cmd, a) == 3) {
                strcpy(it_wshot, a);
            } else if (!strcmp(cmd, "winsize") && sscanf(it_line, "%ld %31s %d %d", &fr, cmd, &x, &y) == 4) {
                SDL_SetWindowSize(win, x, y);
            } else if (!strcmp(cmd, "quit")) { plat_shutdown(); exit(0); }
        }
        it_load_next();
    }
}
#endif

/* Copies the 320x200 texture into the 4:3 area of the window ("sharp
   bilinear" scaling). The texture is first enlarged by whole numbers with
   nearest-pixel scaling, so every source pixel stays the same size, and only
   then smoothed to the exact window size. */
static void draw_scaled(SDL_Texture *src, int w, int h)
{
    int ow, oh, kx, ky;
    SDL_Rect dst;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    dst = fit_4_3(ow, oh);
    if (dst.w <= 0 || dst.h <= 0) return;
    kx = (dst.w + w - 1) / w;
    ky = (dst.h + h - 1) / h;
    if (dst.w == w * kx && dst.h == h * ky) {   /* exact multiple: nothing to smooth */
        SDL_RenderCopy(ren, src, NULL, &dst);
        return;
    }
    if (!tex_big || big_w != w * kx || big_h != h * ky) {
        if (tex_big) SDL_DestroyTexture(tex_big);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   /* linear */
        tex_big = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                    w * kx, h * ky);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        big_w = w * kx;
        big_h = h * ky;
    }
    if (!tex_big || SDL_SetRenderTarget(ren, tex_big) != 0) {
        SDL_RenderCopy(ren, src, NULL, &dst);       /* no render targets: plain scaling */
        return;
    }
    SDL_RenderCopy(ren, src, NULL, NULL);
    SDL_SetRenderTarget(ren, NULL);
    SDL_RenderCopy(ren, tex_big, NULL, &dst);
}

/* Renders the screen to the window. */
static void present(void)
{
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    render_gfx();
    draw_scaled(tex_gfx, 320, 200);
#ifdef RYSS_AUTOTEST
    if (it_wshot[0]) { it_window_shot(it_wshot); it_wshot[0] = 0; }
#endif
    SDL_RenderPresent(ren);
#ifdef RYSS_AUTOTEST
    it_run();
#endif
}

/* Handles events, shows a frame and waits for the next 70 Hz frame time. */
void vga_present(void)
{
    uint64_t now;
    uint64_t step = (uint64_t)((double)perf_freq / FRAME_HZ);
    pump_events();
    present();
    now = SDL_GetPerformanceCounter();
    if (next_frame == 0 || now > next_frame + step * 8) next_frame = now;  /* resync after stalls */
    next_frame += step;
#ifdef __EMSCRIPTEN__
    /* A browser only updates the page while our code isn't running, so give
       control back once every frame, for the time left until the next one. */
    now = SDL_GetPerformanceCounter();
    plat_sleep(now < next_frame ? (uint32_t)((next_frame - now) * 1000 / perf_freq) : 0);
#else
    for (;;) {
        now = SDL_GetPerformanceCounter();
        if (now >= next_frame) break;
        {
            uint64_t ms = (next_frame - now) * 1000 / perf_freq;
            if (ms > 1) SDL_Delay((uint32_t)(ms - 1));
        }
    }
#endif
    frame_count++;
    timer_poll();
}

/* ------------------------------------------------------------------ */

/* Initialises SDL and creates the window, renderer and texture. Returns 1 on success. */
int plat_init(const char *title, int fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                           (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 0; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 0; }
    tex_gfx = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    if (!tex_gfx) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return 0; }
    SDL_ShowCursor(SDL_DISABLE);
    perf_freq = SDL_GetPerformanceFrequency();
    vga_set_mode(0x03);
    return 1;
}

/* Closes audio, destroys the window and renderer and shuts SDL down. */
void plat_shutdown(void)
{
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
    if (mouse_captured) SDL_SetRelativeMouseMode(SDL_FALSE);
    mouse_captured = 0;
    if (tex_big) SDL_DestroyTexture(tex_big);
    tex_big = NULL;
    if (tex_gfx) SDL_DestroyTexture(tex_gfx);
    tex_gfx = NULL;
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    ren = NULL; win = NULL;
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* VGA                                                                 */

/* Switches between text mode (03h) and 320x200 graphics (13h), clearing the screen. */
void vga_set_mode(int mode)
{
    static const uint32_t ega_rgb[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    int i;
    cur_mode = mode;
    memset(vga_vram, 0, sizeof vga_vram);
    for (i = 0; i < 256; i++) pal_rgb[i] = (i < 16) ? ega_rgb[i] : 0;
}

/* Loads a 256-colour palette from 768 bytes of 6-bit VGA DAC values. */
void vga_set_palette(const uint8_t *p)
{
    int i;
    for (i = 0; i < 256; i++) {
        uint32_t r = p[i * 3] & 63, g = p[i * 3 + 1] & 63, b = p[i * 3 + 2] & 63;
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal_rgb[i] = (r << 16) | (g << 8) | b;
    }
}

/* ------------------------------------------------------------------ */
/* PIT timer and keyboard interrupts                                   */

/* Installs a timer handler that runs hz times per second. */
void timer_install(int hz, void (*isr)(void))
{
    if (!perf_freq) perf_freq = SDL_GetPerformanceFrequency();
    timer_isr = isr;
    timer_step = hz > 0 ? perf_freq / (uint64_t)hz : 0;
    timer_next = 0;
}

/* Removes the timer handler (the original restored the 18.2 Hz BIOS timer). */
void timer_remove(void) { timer_isr = NULL; }

/* Installs the keyboard interrupt handler. */
void keyboard_install(void (*isr)(uint8_t)) { kb_isr = isr; }

/* Removes the keyboard interrupt handler. */
void keyboard_remove(void) { kb_isr = NULL; }

/* Sets the key a touch tap stands for while the game waits for it. */
void plat_waiting_for_key(uint8_t scancode)
{
    tap_key = scancode;
    update_mouse_capture();
}

/* ------------------------------------------------------------------ */
/* mouse                                                               */

/* Resets the mouse driver: centres the pointer, default range for mode 13h.
   Returns 1 (there is always a pointing device; on touch screens a finger). */
int mouse_reset(void)
{
    mouse_present_flag = 1;
    mouse_hmin = 0;
    mouse_hmax = 639;
    mouse_x = 320;
    mouse_y = 100;
    mouse_btn = 0;
    mouse_click_latch = 0;
    update_mouse_capture();
    return 1;
}

/* Sets the horizontal range of the driver coordinates. */
void mouse_set_hrange(int min, int max)
{
    if (max < min) { int t = min; min = max; max = t; }
    mouse_hmin = min;
    mouse_hmax = max;
    if (mouse_x < min) mouse_x = min;
    if (mouse_x > max) mouse_x = max;
}

/* Sets the driver's position. The host pointer is left alone. */
void mouse_setpos(int x, int y)
{
    if (x < mouse_hmin) x = mouse_hmin;
    if (x > mouse_hmax) x = mouse_hmax;
    mouse_x = x;
    mouse_y = y;
}

/* Returns the driver position and buttons. A click that was pressed and
   released between two polls is reported once. */
void mouse_get(int *x, int *y, int *buttons)
{
    *x = mouse_x;
    *y = mouse_y;
    *buttons = mouse_btn | (mouse_click_latch ? 1 : 0);
    mouse_click_latch = 0;
}

/* The game no longer uses the mouse: show the normal pointer again. */
void mouse_release(void)
{
    mouse_present_flag = 0;
    update_mouse_capture();
}

/* ------------------------------------------------------------------ */
/* audio                                                               */

static plat_audio_cb a_cb;
static void *a_user;

/* SDL audio callback: hands the buffer to the registered mixer callback. */
static void SDLCALL sdl_audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    a_cb(stream, len, a_user);
}

/* Opens an 8-bit unsigned mono audio device that calls cb to fill buffers.
   Returns the sample rate, or 0 on failure. */
int plat_audio_open(int rate, plat_audio_cb cb, void *user)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 256;
    want.callback = sdl_audio_cb;
    a_cb = cb; a_user = user;
    /* no allowed changes: SDL converts to whatever the hardware uses */
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) { fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(audio_dev, 0);
    return have.freq;
}

/* Keeps the audio callback from running (call before touching mixer state). */
void plat_audio_lock(void)   { if (audio_dev) SDL_LockAudioDevice(audio_dev); }
/* Lets the audio callback run again. */
void plat_audio_unlock(void) { if (audio_dev) SDL_UnlockAudioDevice(audio_dev); }
