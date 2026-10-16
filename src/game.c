/*
 * game.c - RYSSÄN KAUHU game logic, translated from the Watcom C code in
 * RYSS.EXE (code object at 0x10000, game code 0x10B98-0x129A6).
 *
 * Each function is named after what it does and tagged with the address of
 * the original function in the flat image made by tools/le_unpack.py. Global
 * variables keep their original data object addresses (0x30000-) in comments.
 * Timing is expressed, as in the original, in ticks of the 100 Hz timer.
 *
 * Text strings are the original Finnish ones, in CP437 as in the EXE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"
#include "data.h"
#include "gfx.h"
#include "sound.h"
#include "game.h"
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

/* PC/XT scancodes the game looks at in its key table */
enum {
    SC_ESC = 0x01, SC_E = 0x12, SC_ENTER = 0x1C, SC_CTRL = 0x1D,
    SC_K = 0x25, SC_SPACE = 0x39, SC_LEFT = 0x4B, SC_RIGHT = 0x4D
};

/* ---------------------------------------------------------------- */
/* Watcom C library rand()                                            */

static uint32_t rand_seed = 1;

/* rand() of the Watcom C library: 0..32767. */
static int w_rand(void)
{
    rand_seed = rand_seed * 1103515245u + 12345u;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

/* rand() % (hi - lo) + lo, with the unsigned division the original used. */
static uint32_t rand_range(uint32_t lo, uint32_t hi)
{
    uint32_t span = hi - lo;
    if (span == 0) return lo;
    return (uint32_t)w_rand() % span + lo;
}

/* ---------------------------------------------------------------- */
/* level table at 0x3036C (level 0 is never played)                   */

typedef struct {
    uint32_t count;                /* soldiers in the level      */
    uint32_t speed_min, speed_max; /* walking speed, 16.16/tick  */
    uint32_t walk_min, walk_max;   /* ticks between stops        */
    uint32_t spawn_min, spawn_max; /* ticks between spawns       */
    uint32_t ammo;
} Level;

static const Level levels[7] = {
    {  30, 13000, 33000, 200, 1000, 60, 150, 210 },
    {  45, 15000, 35000, 100,  800, 50, 100, 250 },
    {  60, 18000, 37000,  50,  600, 40,  70, 250 },
    {  80, 22000, 40000,  40,  400, 30,  50, 200 },
    { 100, 24000, 42000,  30,  200, 25,  40, 200 },
    { 130, 27000, 53000,  20,  100, 20,  35, 200 },
    { 160, 32000, 60000,  10,   50, 10,  25, 200 },
};
#define LAST_LEVEL 6

/* 0x30364: "SIVARI" as scancodes - fast fire and unlimited ammo */
static const uint8_t cheat_code[6] = { 0x1F, 0x17, 0x2F, 0x1E, 0x13, 0x17 };

/* ---------------------------------------------------------------- */
/* soldiers: 26 byte nodes in a doubly linked list sorted by distance */

enum { ST_WALK = 0, ST_SHOOT = 1, ST_DIE = 2 };

typedef struct {
    uint32_t dist;      /* +08 distance, 16.16                    */
    uint32_t speed;     /* +0C                                     */
    int16_t  angle;     /* +10 position, 1200..15199 (0x2000 = ahead) */
    uint16_t stopdist;  /* +12 where he stops for good             */
    uint16_t walk;      /* +14 ticks until the next stop           */
    uint8_t  state;     /* +16                                     */
    uint8_t  frame;     /* +17                                     */
    uint8_t  tick;      /* +18                                     */
    uint8_t  dead;      /* +19 corpse, drawn into the clean layer  */
} Soldier;

#define MAX_SOLDIERS 256

static Soldier  pool[MAX_SOLDIERS];
static Soldier *list[MAX_SOLDIERS]; /* list[0] = head (nearest) ... tail (farthest) */
static int      nlist;              /* head 0x30B00, tail 0x30B04 */

/* ---------------------------------------------------------------- */
/* globals (data object addresses of the original)                    */

static uint8_t  keys[128];      /* 30A50 keyboard ISR table       */
static uint32_t fire_delay;     /* 30A30 12, or 2 with the cheat  */
static uint32_t amb_timer;      /* 30A34 */
static uint32_t reload_timer;   /* 30A38 */
static uint32_t heal_timer;     /* 30A3C */
static uint32_t medkits;        /* 30A40 */
static uint8_t  mouse_present;  /* 30A48 */
static uint8_t  speed;          /* 30A49 timer ticks per interrupt, always 1 */
static uint8_t  gun_drawn;      /* 30A4C gun sprite on screen     */
static uint8_t  dmg_drawn;      /* 30A4D */
static uint8_t  flash_shown;    /* 30A4E */
static uint32_t spawn_timer;    /* 30AD8 */
static uint32_t spawn_min;      /* 30ADC */
static uint32_t spawn_max;      /* 30AE0 */
static uint32_t to_spawn;       /* 30AE4 */
static uint32_t remaining;      /* 30AE8 */
static uint8_t  flash_frame;    /* 30AF0 */
static uint16_t aim;            /* 30AF2 */
static uint16_t fire_cooldown;  /* 30AF4 */
static uint8_t  damage;         /* 30AF6 8 = dead */
static uint32_t ammo;           /* 30AF8 */
static uint32_t ammo_drawn;     /* 30AFC */
static volatile uint32_t ticks; /* 30B14 */
static uint8_t  cheat_pos;      /* 30B18 */
static uint8_t  firing;         /* 30B22 also read by the mixer   */
static uint8_t  corpse_added;   /* 30B23 */
static uint8_t  firing_prev;    /* 30B24 */
static uint8_t  had_ammo;       /* 30B25 */
static uint32_t walk_min;       /* 30354 */
static uint32_t walk_max;       /* 30358 */
static uint32_t speed_min;      /* 3035C */
static uint32_t speed_max;      /* 30360 */

/* ---------------------------------------------------------------- */
/* sound helpers                                                      */

/* 0x1041E with the audio device locked. */
static void play(int n)
{
    plat_audio_lock();
    sound_play(n);
    plat_audio_unlock();
}

/* Tells the mixer whether the trigger is held (it reads 0x30B22 directly). */
static void sync_gun_sound(void)
{
    plat_audio_lock();
    sound_gun_on = firing;
    plat_audio_unlock();
}

/* ---------------------------------------------------------------- */
/* interrupt handlers                                                 */

/* 0x10D9A: 100 Hz timer callback */
static void timer_isr(void)
{
    ticks += speed;
}

/* 0x10DBE: keyboard interrupt - key table and the cheat code */
static void keyboard_isr(uint8_t code)
{
    uint8_t sc = code & 0x7F;
    uint8_t down = (code & 0x80) == 0;
    keys[sc] = down;
    if (down) {
        if (cheat_code[cheat_pos] == sc) cheat_pos++;
        else cheat_pos = 0;
        if (cheat_pos == 6) {
            fire_delay = 2;
            cheat_pos = 0;
        }
    }
}

/* ---------------------------------------------------------------- */
/* soldiers                                                           */

/* 0x10BFC: gnome sort of the list by distance, nearest first */
static void sort_soldiers(void)
{
    int i, j;
    for (i = 1; i < nlist; i++) {
        Soldier *s = list[i];
        j = i;
        while (j > 0 && s->dist < list[j - 1]->dist) {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = s;
    }
}

/* 0x10C68: a new soldier appears 600-800 units away */
static void spawn_soldier(void)
{
    Soldier *s;
    if (nlist >= MAX_SOLDIERS) return;
    s = &pool[nlist];
    memset(s, 0, sizeof *s);
    s->dist = (uint32_t)(w_rand() % 200 + 600) << 16;
    s->stopdist = (uint16_t)(w_rand() % 60 + 200);
    s->speed = rand_range(speed_min, speed_max);
    s->walk = (uint16_t)rand_range(walk_min, walk_max);
    s->angle = (int16_t)(w_rand() % 14000 + 1200);
    list[nlist++] = s;
    sort_soldiers();
}

/* 0x10E4E: a soldier fires at the player */
static void enemy_fire(Soldier *s)
{
    int r = (int16_t)w_rand() % 5000 - 2500;
    int w = gd.tab_hitw[(s->dist >> 16) & 0xFFF];
    if (r > -w && r < w) {
        damage++;
        play(w_rand() % 2 + SND_HIT1);
    }
}

/* 0x10F52 */
static void reset_amb_timer(void)
{
    amb_timer = (uint32_t)(w_rand() % 200 + 200);
}

/* 0x10F7A: one 1/100 s step of the game */
static void tick(void)
{
    uint16_t mouse_aim = 0;
    int mouse_btn = 0;
    uint8_t fire, shot;
    int spread, i;

    /* random battlefield noises */
    if (amb_timer == 0) {
        reset_amb_timer();
    } else if (--amb_timer == 0) {
        reset_amb_timer();
        play(w_rand() % 4 + SND_AMBIENT1);
    }

    /* once per level a medic patches you up */
    if (damage >= 4 && medkits) {
        medkits--;
        play(SND_MEDIC);
        heal_timer = 500;
    }

    /* out of ammo: 50 more rounds 5 seconds later */
    if (!had_ammo && ammo) had_ammo = 1;
    if (had_ammo && !ammo) {
        had_ammo = 0;
        play(SND_RELOAD);
        reload_timer = 500;
    }
    if (heal_timer && --heal_timer == 0) damage -= 2;
    if (reload_timer && --reload_timer == 0) ammo += 50;

    /* INT 33h AX=03h */
    if (mouse_present) {
        int mx, my, mb;
        mouse_get(&mx, &my, &mb);
        mouse_aim = (uint16_t)((((uint32_t)(uint16_t)mx >> 1) * 14000u) / 640u + 1200u);
        aim = mouse_aim;
        mouse_btn = mb & 1;
    }

    fire = keys[SC_CTRL];
    if (mouse_present && !fire) fire = (uint8_t)mouse_btn;
    if (!ammo) fire = 0;
    firing = fire;
    sync_gun_sound();

    if (spawn_timer == 0) {
        if (to_spawn) {
            spawn_soldier();
            to_spawn--;
        }
        spawn_timer = rand_range(spawn_min, spawn_max);
    } else {
        spawn_timer--;
    }

    /* keyboard aiming */
    if (keys[SC_LEFT] && aim > 1200) aim -= 100;
    if (keys[SC_RIGHT] && aim < 15184) aim += 100;
    if (aim < 1200) aim = 1200;
    if (aim > 15184) aim = 15184;
    if (mouse_present && aim != mouse_aim)            /* INT 33h AX=04h */
        mouse_setpos((int)((((uint32_t)(aim - 1200) * 640u) / 14000u) * 2u), 0);

    spread = aim + w_rand() % 20 - 10;

    if (firing_prev != firing) {
        firing_prev = firing;
        fire_cooldown = 0;
    }

    shot = 0;
    if (fire_cooldown == 0) {
        if (flash_shown) {
            flash_frame = (uint8_t)((flash_frame + 1) % 2);
            flash_shown = 0;
        }
        fire_cooldown = (uint16_t)fire_delay;
        if (firing && ammo) {
            shot = 1;
            if (fire_delay == 12) ammo--;
        }
    } else {
        fire_cooldown--;
    }

    /* the soldiers, nearest first */
    for (i = 0; i < nlist; i++) {
        Soldier *s = list[i];
        if (s->dead) continue;

        s->tick++;
        if (s->tick >= (s->state == ST_DIE ? 8 : 15)) {
            s->tick = 0;
            s->frame++;
        }

        if (s->state == ST_WALK) {
            s->dist -= s->speed;
            if ((s->dist >> 16) < s->stopdist) {
                s->dist = (uint32_t)s->stopdist << 16;
                s->speed = 0;
            }
            if (s->frame >= 6) s->frame = 0;
            if (s->walk == 0 || (s->dist >> 16) == s->stopdist) {
                s->state = ST_SHOOT;
                s->frame = 0;
                s->tick = 0;
                s->walk = (uint16_t)rand_range(walk_min, walk_max);
            } else {
                s->walk--;
            }
        }

        if (s->state == ST_SHOOT) {
            if (s->frame == 2 && s->tick == 0) enemy_fire(s);
            if (s->frame >= 5) {
                s->state = ST_WALK;
                s->frame = 0;
                s->tick = 0;
            }
            if (s->frame == 4 && (s->dist >> 16) == s->stopdist) {
                /* at his final position: keep shooting */
                s->frame = 1;
                s->tick = 0xFF;
            }
        }

        if (s->state == ST_DIE && s->frame > 12) {
            s->frame = 12;
            s->dead = 1;
            corpse_added = 1;
            remaining--;
        }

        if (shot && s->state != ST_DIE) {
            int w = gd.tab_hitw[(s->dist >> 16) & 0xFFF];
            int x = s->angle;
            int a = (uint16_t)spread;
            if (a > x - w && a < x + w) {
                s->frame = 0;
                s->tick = 0;
                s->state = ST_DIE;
                shot = 0;
                play(w_rand() % 8 + SND_SCREAM1);
            }
        }
    }

    if (fire_delay != 12) ammo = 999;
}

/* ---------------------------------------------------------------- */
/* drawing                                                            */

/* 0x11565: projects a soldier and draws him scaled into the back buffer */
static void draw_soldier(const Soldier *s)
{
    int spr = s->frame, x, idx, c, d, scale;

    if (s->state == ST_SHOOT) {
        if (s->frame == 0 || s->frame == 4) spr = 6;
        if (s->frame == 1 || s->frame == 3) spr = 7;
        if (s->frame == 2) spr = 8;
    } else if (s->state == ST_DIE) {
        spr = s->frame + 11;
    }

    x = angle_to_x[s->angle & 0x3FFF] + 160;
    idx = ((uint16_t)(s->angle - 0x2000)) >> 4;
    c = (int16_t)gd.tab_cos[idx & 0xFFF];
    d = (int)((int32_t)(s->dist >> 16) * c) >> 8;
    if (d <= 0) return;
    scale = (uint16_t)(0x8000 / d);
    gfx_sprite_scaled(spr, scale, x, 100);
}

/* 0x1164B: a red cross for every hit taken */
static void draw_damage(void)
{
    int row, col;
    for (row = 0; row < 2; row++)
        for (col = 0; col < 4; col++)
            if (damage >= row * 4 + col + 1)
                gfx_sprite(27, col * 18 + 10, row * 18 + 170);
}

/* 0x116AB: ammo counter, drawn straight to the screen */
static void draw_ammo(void)
{
    const int pos = 160 * SCR_W + 224;
    uint32_t h = ammo / 100, t = ammo / 10 - h * 10;
    gfx_copy_rect(vga_vram, scr_clean, pos, 48, 16);
    if (h) gfx_char(vga_vram, pos, 16, (int)('0' + h), 2);
    if (h || t) gfx_char(vga_vram, pos + 16, 16, (int)('0' + t), 2);
    gfx_char(vga_vram, pos + 32, 16, (int)('0' + ammo % 10), 2);
}

/* 0x10EC0: the game speed display - with speed 1 it only restores the area */
static void draw_speed(void)
{
    gfx_copy_rect(vga_vram, scr_clean, 180 * SCR_W + 232, 32, 16);
    if (speed != 1) {
        gfx_char(vga_vram, 180 * SCR_W + 232, 16, '0' + speed, 1);
        gfx_char(vga_vram, 180 * SCR_W + 248, 16, 'x', 1);
    }
}

/* 0x117A5: draws a frame, copying only what changed to the screen */
static void render(void)
{
    int i, spr;

    sort_soldiers();
    if (dirty_bottom >= 136) {
        dirty_gun = 1;
        dirty_hud = 1;
    }

    if (corpse_added) {
        /* rebuild the clean layer: background + all corpses */
        gfx_copy_screen(scr_back, scr_bg);
        for (i = nlist - 1; i >= 0; i--)
            if (list[i]->dead) draw_soldier(list[i]);
        gfx_copy_screen(scr_clean, scr_back);
        dirty_gun = 1;
        dirty_hud = 1;
        corpse_added = 0;
        gun_drawn = 0xFF;
        dmg_drawn = 0xFF;
    } else {
        gfx_flip_dirty(scr_back, scr_clean);
        dirty_gun = 0;
        dirty_hud = 0;
    }

    dirty_top = 80;
    dirty_bottom = 120;

    /* live soldiers, farthest first */
    for (i = nlist - 1; i >= 0; i--)
        if (!list[i]->dead) draw_soldier(list[i]);

    /* cross-hair */
    gfx_sprite(28, angle_to_x[aim & 0x3FFF] + 160, 100);

    /* the machine gun, with muzzle flash and recoil when firing */
    spr = 24;
    if (firing && ammo) spr = 25 + flash_frame;
    if (spr != gun_drawn) {
        gun_drawn = (uint8_t)spr;
        dirty_gun = 1;
        gfx_sprite(spr, 160, spr == 25 ? 169 : 168);
        flash_shown = 1;
    } else if (dirty_bottom >= 136) {
        gfx_sprite(spr, 160, 168);
    }

    if (damage != dmg_drawn) {
        dirty_hud = 1;
        dmg_drawn = damage;
        draw_damage();
    }

    if (ammo != ammo_drawn) {
        draw_ammo();
        ammo_drawn = ammo;
    }

    gfx_flip_dirty(vga_vram, scr_back);
}

/* ---------------------------------------------------------------- */
/* levels                                                             */

/* 0x114D5: background into all buffers */
static void reset_screens(void)
{
    gfx_copy_screen(scr_clean, scr_bg);
    gfx_copy_screen(vga_vram, scr_clean);
    gfx_copy_screen(scr_back, scr_clean);
    dirty_top = 100;
    dirty_bottom = 101;
    gun_drawn = 0xFF;
    dirty_gun = 1;
    dirty_hud = 1;
    dmg_drawn = 0xFF;
    ammo_drawn = 0xFFFF;
    corpse_added = 1;
}

/* 0x1198C */
static void start_level(int lv)
{
    const Level *L = &levels[lv];

    reset_screens();
    gfx_char(vga_vram, 168 * SCR_W + 288, 32, '0' + lv, 2);
    draw_speed();

    nlist = 0;                     /* the original frees the list here */
    aim = 0x2000;
    to_spawn = L->count;
    speed_min = L->speed_min;
    speed_max = L->speed_max;
    walk_min = L->walk_min;
    walk_max = L->walk_max;
    spawn_min = L->spawn_min;
    spawn_max = L->spawn_max;
    ammo = L->ammo;
    ammo_drawn = 0xFFFF;
    ticks = 0;
    spawn_timer = 0;
    damage = 0;
    heal_timer = 0;
    reload_timer = 0;
    amb_timer = 0;
    medkits = 1;
    remaining = to_spawn;
}

/* Waits until one of the given keys is down (the original spun on printf("")).
   tap is the key a finger tap stands for on touch screens. */
static uint8_t wait_keys(const uint8_t *sc, int n, uint8_t tap)
{
    int i;
    plat_waiting_for_key(tap);
    for (;;) {
        for (i = 0; i < n; i++)
            if (keys[sc[i]]) {
                plat_waiting_for_key(0);
                return sc[i];
            }
        vga_present();
    }
}

/* 0x11AC3: the game */
static void play_game(void)
{
    static const uint8_t keys_yes_no[3] = { SC_ESC, SC_K, SC_E };
    static const uint8_t keys_next[2] = { SC_ESC, SC_SPACE };
    int level;

    /* INT 33h: reset, hide, range 0..1280, centre, double the speed */
    mouse_present = 0;
    aim = 0x2000;
    if (mouse_reset()) {
        mouse_present = 1;
        mouse_set_hrange(0, 0x500);
        mouse_setpos(0x280, 0);
    }

new_game:
    level = 1;
    fire_delay = 12;
    speed = 1;

next_level:
    start_level(level);
    play(SND_LEVELSTART);

    while (!keys[SC_ESC]) {
        while (ticks) {
            tick();
            ticks--;
        }
        render();
        vga_present();
        if (damage >= 8 || remaining == 0) break;
    }
    firing = 0;
    sync_gun_sound();

    if (!keys[SC_ESC]) {
        if (damage >= 8) {
            play(SND_DEATH);
            gfx_string(vga_vram, 40 * SCR_W + 60, 16, "KUOLIT", 1);
            gfx_string(vga_vram, 60 * SCR_W + 15, 16, "Uusi peli (k/e)?", 1);
            if (wait_keys(keys_yes_no, 3, SC_K) == SC_K) goto new_game;
        } else if (level < LAST_LEVEL) {
            gfx_string(vga_vram, 40 * SCR_W + 40, 16, "KENTT\x8E L\x8EPI!", 1);
            gfx_string(vga_vram, 60 * SCR_W + 30, 16, "Paina v\x84lily\x94nti\x84", 1);
            level++;
            if (wait_keys(keys_next, 2, SC_SPACE) == SC_SPACE) goto next_level;
        } else {
            gfx_string(vga_vram, 40 * SCR_W + 50, 16, "VOITIT!!!", 1);
            gfx_string(vga_vram, 60 * SCR_W + 15, 16, "Uusi peli (k/e)?", 1);
            if (wait_keys(keys_yes_no, 3, SC_K) == SC_K) goto new_game;
        }
    }

    /* INT 33h AX=1Ah: mouse speed back to what it was */
    if (mouse_present) mouse_release();
}

/* ---------------------------------------------------------------- */

/* Loads graph.dat; part of main() (0x11ECD). */
int game_load_assets(char *err, int errlen)
{
    if (data_load(err, errlen) != 0) return 0;
    memcpy(scr_bg, gd.background, SCR_SIZE);
    return 1;
}

/* 0x127E4: main() */
void game_main(void)
{
    uint8_t enter = SC_ENTER;

    timer_install(100, timer_isr);   /* 0x12F07 + 0x12FAA: PIT at 100 Hz   */
    sound_init();                    /* 0x10B75                            */
    gfx_init_tables();               /* 0x11DED                            */
    vga_set_mode(0x13);

    /* the original called rand() (sum of the date and time bytes) times */
    rand_seed = (uint32_t)time(NULL);
#ifdef RYSS_AUTOTEST
    if (getenv("IT_SEED")) rand_seed = (uint32_t)atol(getenv("IT_SEED"));
#endif
    ticks = 0;
    speed = 1;
    keyboard_install(keyboard_isr);
    vga_set_palette(gd.palette);

    /* credits, straight into video memory */
    gfx_string(vga_vram, 0 * SCR_W + 50, 32, "RYSS\x8EN", 2);
    gfx_string(vga_vram, 32 * SCR_W + 66, 32, "KAUHU", 2);
    gfx_string(vga_vram, 80 * SCR_W + 70, 16, "GRAFIIKKA", 1);
    gfx_string(vga_vram, 96 * SCR_W + 25, 16, "Tuomas Aikioniemi", 1);
    gfx_string(vga_vram, 128 * SCR_W + 95, 16, "\x8E\x8ENET", 1);
    gfx_string(vga_vram, 144 * SCR_W + 17, 16, "Aleksi Ala-Mononen", 1);
    gfx_string(vga_vram, 184 * SCR_W + 60, 16, "Paina enter", 1);

#ifdef __EMSCRIPTEN__
    /* Web version: the page shows a start button over the title (browsers
       allow sound only after the user has clicked something). It counts as Enter. */
    EM_ASM({ if (Module.onTitleShown) Module.onTitleShown(); });
    plat_waiting_for_key(SC_ENTER);
    while (!keys[SC_ENTER] && !EM_ASM_INT({ return Module.startClicked ? 1 : 0; }))
        vga_present();
    plat_waiting_for_key(0);
#else
    wait_keys(&enter, 1, SC_ENTER);
#endif
    (void)enter;

    play_game();

    /* back to DOS: keyboard and timer vectors, text mode */
    keyboard_remove();
    sound_shutdown();
    vga_set_mode(0x03);
    timer_remove();
}
