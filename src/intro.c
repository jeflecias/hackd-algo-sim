/* intro.c - glitch intro, fake infection boot log, skull reveal, welcome */
#include "app.h"
#include <string.h>
#include <stdio.h>

#define GLITCH_MS 2600.0
#define SKULL_MS  2400.0

static void enter_boot(App *a){
    uint64_t *r = &a->rng;
    term_clear(&a->term);
    Terminal *t = &a->term;
    term_queue(t, 60,  COL_GREEN, "[ booting parasite kernel module ............ ]");
    term_queue(t, 120, COL_GREEN, "[OK] mapping /dev/mem");
    term_queue(t, 90,  COL_GREEN, "[OK] hooking syscall table @ 0x%08X", rng_next(r));
    term_queue(t, 90,  COL_GREEN, "[OK] disabling kernel write-protect (CR0)");
    term_queue(t, 130, COL_AMBER, "[**] scanning ready queue for live processes...");
    for (int i = 0; i < 6; i++)
        term_queue(t, 70, COL_DGREEN, "    pid %-5d  inject payload ... done  rss=%dK",
                   rng_range(r,200,9999), rng_range(r,64,8192));
    term_queue(t, 120, COL_GREEN, "[OK] page table walk complete  frames=%d", rng_range(r,128,2048));
    /* randomized infection lines so each boot differs */
    {
        static const char *VAR[] = {
            "[**] harvesting browser cookies ....... %d found",
            "[**] keylogger hooked  WH_KEYBOARD_LL  tid=%d",
            "[**] webcam handle acquired  /dev/video%d",
            "[**] clipboard scraped  %d entries",
            "[**] mic stream opened  %d kHz",
            "[**] enumerating saved passwords ...... %d",
        };
        int a0 = rng_range(r,0,5), b0 = rng_range(r,0,5);
        term_queue(t, 95, COL_AMBER, VAR[a0], rng_range(r,3,991));
        if (b0 != a0) term_queue(t, 95, COL_AMBER, VAR[b0], rng_range(r,3,991));
    }
    term_queue(t, 100, COL_RED,   "[!!] DISABLING DEFENDER ............ bypassed");
    term_queue(t, 100, COL_RED,   "[!!] DISABLING FIREWALL ........... bypassed");
    term_queue(t, 130, COL_AMBER, "[**] encrypting /home/user ...");
    for (int i = 0; i <= 100; i += 12){
        char bar[64]; int n = i/4; int k;
        for (k = 0; k < n; k++) bar[k] = '#';
        for (; k < 25; k++) bar[k] = '.';
        bar[25] = 0;
        term_queue(t, 55, COL_RED, "    [%s] %3d%%  AES-256  block 0x%06X", bar, i, rng_next(r)&0xFFFFFF);
    }
    term_queue(t, 120, COL_GREEN, "[OK] exfil channel open -> %d.%d.%d.%d:%d",
               rng_range(r,10,250),rng_range(r,0,255),rng_range(r,0,255),rng_range(r,1,254),
               rng_range(r,1024,65000));
    term_queue(t, 110, COL_AMBER, "[**] installing persistence (cron + registry) ...");
    term_queue(t, 90,  COL_GREEN, "[OK] handing control to interactive shell");
    term_queue(t, 600, COL_RED,   " ");
}

static void enter_welcome(App *a){
    term_clear(&a->term);
    Terminal *t = &a->term;
    term_queue(t, 50,  COL_RED,   "  ____  _____ ____  ____  _     ___   ____ _  __");
    term_queue(t, 40,  COL_RED,   " |  _ \\| ____|  _ \\|  _ \\| |   / _ \\ / ___| |/ /");
    term_queue(t, 40,  COL_RED,   " | | | |  _| | |_) | | | | |  | | | | |   | ' / ");
    term_queue(t, 40,  COL_RED,   " | |_| | |___|  _ <| |_| | |__| |_| | |___| . \\ ");
    term_queue(t, 40,  COL_RED,   " |____/|_____|_| \\_\\____/|_____\\___/ \\____|_|\\_\\");
    term_queue(t, 250, COL_DGREEN," ::  your machine now runs an OS-concepts parasite  ::");
    term_queue(t, 200, COL_CYAN,  "");
    term_queue(t, 120, COL_CYAN,  "WELCOME, root. You are trapped inside a fake shell.");
    term_queue(t, 90,  COL_GREEN, "The only way to understand it is to RUN its algorithms.");
    term_queue(t, 90,  COL_GREEN, "Each command animates a lesson from Modules 4-7:");
    term_queue(t, 70,  COL_AMBER, "  sched  -> CPU scheduling   (fcfs sjf srtf rr ...)");
    term_queue(t, 70,  COL_AMBER, "  mem    -> memory mgmt      (firstfit bestfit paging ...)");
    term_queue(t, 70,  COL_AMBER, "  vmem   -> virtual memory   (fifo opt lru belady ...)");
    term_queue(t, 70,  COL_AMBER, "  disk   -> mass storage     (fcfs sstf scan look ...)");
    term_queue(t, 90,  COL_GREEN, "Edit the data with 'data <module>'. Read theory with 'man <algo>'.");
    term_queue(t, 120, COL_RED,   "WARNING: every 1-3 minutes the skull returns with a 30s test.");
    term_queue(t, 90,  COL_RED,   "         pass or fail, you'll be dropped back where you were.");
    term_queue(t, 150, COL_CYAN,  "Type 'help' to begin.  (ESC = panic exit)");
    term_queue(t, 200, COL_GREEN, "");
}

static void render_scroll(App *a, int glitch){
    Framebuffer *fb = &a->fb;
    Terminal *t = &a->term;
    fb_clear(fb, COL_BG);
    int ch = fb->ch_h;
    int vis = (fb->h - 40) / ch;
    int start = t->count > vis ? t->count - vis : 0;
    int y = 20;
    for (int i = start; i < t->count; i++){
        int idx = (t->head + i) % TERM_SCROLLBK;
        fb_text(fb, 24, y, t->lines[idx].text, t->lines[idx].color);
        y += ch;
    }
    if (glitch && (rng_next(&a->rng) & 7) == 0)
        gfx_slice_tear(fb, &a->rng, 30, 2);
    gfx_scanlines(fb, 85);
}

void intro_update(App *a, double dt){
    static int prev = -999;
    if (a->state != prev){
        if (a->state == ST_BOOT_INFECT)   enter_boot(a);
        else if (a->state == ST_WELCOME)  enter_welcome(a);
        prev = a->state;
    }

    switch (a->state){
    case ST_GLITCH_INTRO:
        if ((rng_next(&a->rng) & 31) == 0) audio_sfx(SFX_GLITCH, 0.8f);  /* tearing static */
        if (a->state_time > GLITCH_MS){ a->state = ST_BOOT_INFECT; a->state_time = 0; }
        break;
    case ST_BOOT_INFECT:
        if (!term_busy(&a->term) && a->state_time > 800){
            a->state = ST_SKULL_REVEAL; a->state_time = 0;
            audio_sfx(SFX_SKULL, 0);
            gfx_phosphor_reset(&a->fb);
        }
        break;
    case ST_SKULL_REVEAL:
        if (a->state_time > SKULL_MS){ a->state = ST_WELCOME; a->state_time = 0; prev = -999; }
        break;
    case ST_WELCOME:
        if (!term_busy(&a->term) && a->state_time > 400){
            a->state = ST_TERMINAL; a->state_time = 0;
            jumpscare_schedule(a);
        }
        break;
    default: break;
    }
}

void intro_render(App *a){
    Framebuffer *fb = &a->fb;
    switch (a->state){
    case ST_GLITCH_INTRO: {
        double p = a->state_time / GLITCH_MS;  /* 0..1 intensity ramp */
        if (a->shot) fb_blit_shot(fb, a->shot);
        else fb_clear(fb, COL_BG);
        int amt = (int)(4 + p * 90);
        gfx_slice_tear(fb, &a->rng, amt, (int)(1 + p*8));
        gfx_rgb_split(fb, (int)(p * 14) + (rng_next(&a->rng)%3));
        if ((rng_next(&a->rng) % 5) < (int)(p*5))
            gfx_invert_band(fb, rng_range(&a->rng,0,fb->h-1), rng_range(&a->rng,8,60));
        gfx_noise_blocks(fb, &a->rng, (int)(p * 60), (int)(20 + p*120));
        gfx_scanlines(fb, 80);
        /* flashing message */
        if (((int)(a->state_time/120)) & 1){
            const char *msg = "S Y S T E M   C O M P R O M I S E D";
            int x = fb->w/2 - (int)strlen(msg)*fb->ch_w/2;
            fb_text(fb, x, fb->h/2, msg, COL_RED);
        }
        break; }
    case ST_BOOT_INFECT:
        render_scroll(a, 1);
        break;
    case ST_SKULL_REVEAL: {
        fb_clear(fb, COL_BG);
        int frame = (int)(a->state_time / 120);
        uint32_t col = ((frame & 1)) ? COL_RED : COL_WHITE;
        if (a->state_time < 200) col = COL_WHITE;
        skull_render(fb, fb->w/2, fb->h/2, frame, col);
        const char *laugh = "H A   H A   H A   H A";
        int x = fb->w/2 - (int)strlen(laugh)*fb->ch_w/2;
        fb_text(fb, x, fb->h/2 + skull_height_px(fb)/2 + fb->ch_h, laugh, COL_RED);
        if ((rng_next(&a->rng) & 3) == 0) gfx_slice_tear(fb, &a->rng, 24, 3);
        if ((rng_next(&a->rng) & 7) == 0) gfx_rgb_split(fb, 6);
        gfx_phosphor(fb, 68);   /* the skull smears across the screen */
        gfx_scanlines(fb, 82);
        gfx_vignette(fb);
        break; }
    case ST_WELCOME:
        render_scroll(a, 0);
        break;
    default: break;
    }
}
