/* fourthwall.c - the lose set-piece's cinematics:
   (1) ST_FOURTHWALL: the parasite addresses the REAL player over their REAL desktop,
       then "zooms out of the monitor" and hands off to the 3D world (world_enter).
   (2) ST_GAMEOVER:   lives exhausted -> the real desktop is consumed, a final personal
       line types out, then the program exits.
   "Full horror, spare the data": the only system calls here are read-only identity
   lookups (username / computer name / local time) for display. Nothing is touched. */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* clamp an identity string to a sane display length so long usernames/hostnames can't
   blow out the fixed taunt buffers or run off-screen. returns a pointer into a rotating
   set of static buffers (safe for a few concurrent uses in one snprintf chain). */
static const char *clip(const char *s){
    #define CLIP_MAX 20
    static char buf[4][CLIP_MAX + 4];
    static int which = 0;
    char *out = buf[which]; which = (which + 1) & 3;
    int i = 0;
    for (; s && s[i] && i < CLIP_MAX; i++) out[i] = s[i];
    if (s && s[i]){ out[i++] = '.'; out[i++] = '.'; out[i++] = '.'; }
    out[i] = 0;
    return out;
}

static void grab_identity(App *a){
    DWORD n = sizeof(a->fw.user);
    if (!GetUserNameA(a->fw.user, &n) || !a->fw.user[0]) strcpy(a->fw.user, "user");
    n = sizeof(a->fw.host);
    if (!GetComputerNameA(a->fw.host, &n) || !a->fw.host[0]) strcpy(a->fw.host, "this machine");
    SYSTEMTIME st; GetLocalTime(&st);
    snprintf(a->fw.when, sizeof(a->fw.when), "%02d:%02d", st.wHour, st.wMinute);
}

/* ============================ fourth-wall reveal ============================ */

void fourthwall_enter(App *a){
    a->state = ST_FOURTHWALL;
    a->state_time = 0;
    a->fw.phase = 0;
    a->fw.t = 0;
    a->fw.bsod_pct = 0;
    a->fw.variant = rng_range(&a->rng, 0, 3);   /* a different cinematic each time */
    grab_identity(a);
    gfx_phosphor_reset(&a->fb);
    audio_silence(280);
    audio_sfx(SFX_BREATH, 0);
}

void fourthwall_update(App *a, double dt){
    a->fw.t += dt;
    a->fw.bsod_pct += dt * 0.018;               /* ~5.5s to fill to 100% */
    if (a->fw.bsod_pct > 100) a->fw.bsod_pct = 100;

    if (a->fw.variant == 3){                     /* datamosh meltdown: 3 phases */
        if (a->fw.phase == 0){ if (a->fw.t > 2800){ a->fw.phase=1; a->fw.t=0; audio_sfx(SFX_GLITCH,0); } }
        else if (a->fw.phase == 1){ if (a->fw.t > 2400){ a->fw.phase=2; a->fw.t=0; audio_sfx(SFX_SKULL,0); } }
        else { if (a->fw.t > 1100) world_enter(a); }
        return;
    }
    /* BSOD / Windows Update / kernel-panic: fill the meter, brief collapse, then maze */
    if (a->fw.phase == 0){
        if (a->fw.bsod_pct >= 100 && a->fw.t > 2600){ a->fw.phase=1; a->fw.t=0; audio_sfx(SFX_SKULL,0); }
    } else {
        if (a->fw.t > 1300) world_enter(a);
    }
}

/* a short corrupted collapse used to tear any of the static screens into the maze */
static void collapse_fx(App *a, double k){
    Framebuffer *fb = &a->fb;
    gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
    gfx_slice_tear(fb, &a->rng, 40 + (int)(k*50), 4 + (int)(k*5));
    gfx_rgb_split(fb, 3 + (int)(k*9));
    if ((rng_next(&a->rng) & 3) == 0) gfx_invert_band(fb, rng_range(&a->rng,0,fb->h-20), 20);
    gfx_brightness(fb, 90 - (int)(k*70));
}

/* ---- variant 0: Blue Screen of Death ---- */
static void render_bsod(App *a){
    Framebuffer *fb = &a->fb; int W = fb->w, H = fb->h, ch = fb->ch_h;
    fb_clear(fb, 0x000078D7u);                   /* Windows-10 BSOD blue */
    int x = W/6, y = H/6;
    fb_font_for(fb, fb->base_fh * 90 / 100);
    fb_text(fb, x, y, ":(", COL_WHITE);
    fb_font_base(fb);
    char l[160];
    fb_text(fb, x, y + ch*3, "Your PC ran into a problem and needs YOUR frame.", COL_WHITE);
    fb_text(fb, x, y + ch*4, "We're just swapping you to disk, and then we'll", COL_WHITE);
    fb_text(fb, x, y + ch*5, "schedule someone else.", COL_WHITE);
    snprintf(l, sizeof(l), "swapping you to disk: %d%% complete", (int)a->fw.bsod_pct);
    fb_text(fb, x, y + ch*7, l, COL_WHITE);
    int bw = W/3, by = y + ch*8;
    fb_frame(fb, x, by, bw, ch, COL_WHITE);
    fb_fill_rect(fb, x, by, (int)(bw * a->fw.bsod_pct/100.0), ch, COL_WHITE);
    fb_text(fb, x, y + ch*11, "If you call someone, tell them: YOU ARE THE BUG", COL_WHITE);
    snprintf(l, sizeof(l), "Stop code: DEADLOCK_DETECTED   host: %s   user: %s", clip(a->fw.host), clip(a->fw.user));
    fb_text(fb, x, y + ch*13, l, COL_WHITE);
    if (a->fw.phase >= 1) collapse_fx(a, a->fw.t/1300.0);
    gfx_scanlines(fb, 92);
}

/* ---- variant 1: fake Windows Update ---- */
static void render_update(App *a){
    Framebuffer *fb = &a->fb; int W = fb->w, H = fb->h, ch = fb->ch_h;
    fb_clear(fb, 0x00103A6Eu);
    int cx = W/2, cy = H/3, R = H/16;
    double ang = a->fw.t / 170.0;                 /* spinner */
    for (int i = 0; i < 8; i++){
        double aa = ang + i * (6.2831853/8.0);
        int dx = cx + (int)(cos(aa)*R), dy = cy + (int)(sin(aa)*R);
        int b = 70 + (i*22) % 185;
        fb_fill_rect(fb, dx-3, dy-3, 6, 6, RGB32(b,b,b));
    }
    int pct = (int)a->fw.bsod_pct;
    char l[160];
    snprintf(l, sizeof(l), "Working on updates  %d%%", pct);
    fb_text_center(fb, cx, cy + R + ch*2, l, COL_WHITE);
    const char *taunt = pct < 40 ? "Don't turn off your PC."
                      : pct < 70 ? "Don't turn off your PC."
                      : pct < 92 ? "Almost done writing you to disk."
                                 : "This won't hurt. much.";
    snprintf(l, sizeof(l), "%s", taunt);
    fb_text_center(fb, cx, cy + R + ch*4, l, COL_WHITE);
    if (pct >= 50){
        snprintf(l, sizeof(l), "paging %s out to /swap...", clip(a->fw.user));
        fb_text_center(fb, cx, cy + R + ch*6, l, COL_WHITE);
    }
    if (a->fw.phase >= 1) collapse_fx(a, a->fw.t/1300.0);
    gfx_scanlines(fb, 92);
}

/* ---- variant 2: kernel panic + format ---- */
static void render_panic(App *a){
    Framebuffer *fb = &a->fb; int ch = fb->ch_h;
    fb_clear(fb, 0x00000000);
    int x = 28, y = 24;
    char l[160];
    fb_text(fb, x, y,           "Kernel panic - not syncing: no free frame, must evict YOU", COL_WHITE); y += ch;
    snprintf(l,sizeof(l),       "CPU: 0 PID: %d Comm: %s Tainted: G   D",
             1000 + (int)(a->fw.bsod_pct*7) % 9000, clip(a->fw.user));
    fb_text(fb, x, y, l, COL_WHITE); y += ch;
    snprintf(l,sizeof(l),       "Hardware name: %s", clip(a->fw.host));
    fb_text(fb, x, y, l, COL_WHITE); y += ch*2;
    fb_text(fb, x, y,           "Call Trace:", COL_DGREEN); y += ch;
    fb_text(fb, x, y,           "  [<ffffffff8badf00d>] swap_out+0x66/0x66", COL_DGREEN); y += ch;
    fb_text(fb, x, y,           "  [<ffffffffdeadbeef>] schedule_out+0xff/0xff", COL_DGREEN); y += ch;
    fb_text(fb, x, y,           "  [<ffffffffc0000005>] no_free_frame+0x0/0x0", COL_DGREEN); y += ch;
    fb_text(fb, x, y,           "  [<00000000000000>] reap_zombie+0x0/0x0", COL_RED); y += ch*2;
    snprintf(l,sizeof(l),       "writing /dev/swap ... %d%%", (int)a->fw.bsod_pct);
    fb_text(fb, x, y, l, (int)a->fw.bsod_pct >= 100 ? COL_RED : COL_AMBER); y += ch;
    int bw = fb->w/3;
    fb_frame(fb, x, y, bw, ch, COL_DGREEN);
    fb_fill_rect(fb, x, y, (int)(bw * a->fw.bsod_pct/100.0), ch, COL_RED);
    if ((rng_next(&a->rng) & 31) == 0) gfx_slice_tear(fb, &a->rng, 16, 2);
    if (a->fw.phase >= 1) collapse_fx(a, a->fw.t/1300.0);
    gfx_scanlines(fb, 86);
}

/* ---- variant 3: datamosh meltdown over the real desktop ---- */
static void render_meltdown(App *a){
    Framebuffer *fb = &a->fb;
    if (a->fw.phase == 0){
        if (a->shot) fb_blit_shot(fb, a->shot); else fb_clear(fb, COL_BG);
        gfx_rgb_split(fb, 2 + (int)(a->fw.t / 600));
        if ((rng_next(&a->rng) & 3) == 0) gfx_slice_tear(fb, &a->rng, 30, 3);
        gfx_brightness(fb, 55);
        char l0[96], l1[96], l2[96];
        snprintf(l0, sizeof(l0), "I SEE YOU, %s.", clip(a->fw.user));
        snprintf(l1, sizeof(l1), "this is %s.", clip(a->fw.host));
        snprintf(l2, sizeof(l2), "it's %s. why are you still resident?", a->fw.when);
        const char *L[4] = { l0, l1, l2, "you yielded. you're paged out to disk now." };
        int shown = (int)(a->fw.t / 650) + 1; if (shown > 4) shown = 4;
        int y = fb->h/2 - fb->ch_h*4;
        for (int i = 0; i < shown; i++){
            if ((rng_next(&a->rng) & 7) == 0){ char g[96]; fb_garble(g, L[i], &a->rng, 25);
                fb_text_center(fb, fb->w/2, y, g, COL_RED); }
            else fb_text_center(fb, fb->w/2, y, L[i], COL_RED);
            y += fb->ch_h*2;
        }
        gfx_scanlines(fb, 82);
        return;
    }
    if (a->fw.phase == 1){
        if (a->shot) fb_blit_shot(fb, a->shot); else fb_clear(fb, 0x00000000);
        double k = a->fw.t / 2400.0; if (k > 1) k = 1;
        gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
        gfx_slice_tear(fb, &a->rng, 50 + (int)(k*60), 6 + (int)(k*6));
        gfx_rgb_split(fb, 6 + (int)(k*10));
        gfx_brightness(fb, 80 - (int)(k*70));
        gfx_scanlines(fb, 80);
        return;
    }
    fb_clear(fb, 0x00000000);
    if ((rng_next(&a->rng) & 3) == 0) gfx_invert_band(fb, 0, fb->h);
    fb_text_center(fb, fb->w/2, fb->h/2, "ERROR", COL_RED);
    gfx_scanlines(fb, 80);
}

void fourthwall_render(App *a){
    switch (a->fw.variant){
        case 0:  render_bsod(a);     break;
        case 1:  render_update(a);   break;
        case 2:  render_panic(a);    break;
        default: render_meltdown(a); break;
    }
}

/* ============================== corrupted exit ============================== */

void gameover_enter(App *a){
    a->state = ST_GAMEOVER;
    a->state_time = 0;
    a->fw.phase = 0;
    a->fw.t = 0;
    if (!a->fw.user[0]) grab_identity(a);
    gfx_phosphor_reset(&a->fb);
    audio_silence(300);
    audio_sfx(SFX_SKULL, 0);
}

void gameover_update(App *a, double dt){
    a->fw.t += dt;
    if (a->fw.phase == 0){
        if (a->fw.t > 3000){ a->fw.phase = 1; a->fw.t = 0; }
    } else if (a->fw.phase == 1){
        if (a->fw.t > 2500){ a->fw.phase = 2; a->fw.t = 0; }
    } else {
        if (a->fw.t > 2600){ a->state = ST_QUIT; PostQuitMessage(0); }
    }
}

void gameover_render(App *a){
    Framebuffer *fb = &a->fb;

    if (a->fw.phase == 0){
        if (a->shot) fb_blit_shot(fb, a->shot); else fb_clear(fb, COL_BG);
        gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
        gfx_slice_tear(fb, &a->rng, 60, 6);
        gfx_rgb_split(fb, 4 + (int)(a->fw.t / 300));
        gfx_brightness(fb, 70);
        char l[160], g[160];
        snprintf(l, sizeof(l), "no more room for you here, %s -- paged out. the next one finds you.", clip(a->fw.user));
        fb_garble(g, l, &a->rng, 15);
        fb_text_center(fb, fb->w/2, fb->h/2, g, COL_RED);
        gfx_scanlines(fb, 80);
        return;
    }

    if (a->fw.phase == 1){
        if (a->shot) fb_blit_shot(fb, a->shot); else fb_clear(fb, 0x00000000);
        gfx_datamosh(fb, &a->rng, 0, 0, fb->w, fb->h);
        gfx_slice_tear(fb, &a->rng, 80, 8);
        gfx_rgb_split(fb, 8);
        int b = 70 - (int)(a->fw.t / 2500.0 * 70); if (b < 0) b = 0;
        gfx_brightness(fb, b);            /* fade to black */
        gfx_scanlines(fb, 78);
        return;
    }

    /* black + the last word, then exit */
    fb_clear(fb, 0x00000000);
    if ((rng_next(&a->rng) & 7) == 0) gfx_invert_band(fb, fb->h/2 - 10, 20);
    fb_text_center(fb, fb->w/2, fb->h/2, "DEADLOCK.", COL_RED);
    gfx_scanlines(fb, 80);
}
