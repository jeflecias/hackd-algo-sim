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
    grab_identity(a);
    gfx_phosphor_reset(&a->fb);
    audio_silence(280);
    audio_sfx(SFX_BREATH, 0);
}

void fourthwall_update(App *a, double dt){
    a->fw.t += dt;
    if (a->fw.phase == 0){
        if (a->fw.t > 2800){ a->fw.phase = 1; a->fw.t = 0; audio_sfx(SFX_GLITCH, 0); }
    } else if (a->fw.phase == 1){
        if (a->fw.t > 2600){ a->fw.phase = 2; a->fw.t = 0; audio_sfx(SFX_SKULL, 0); }
    } else {
        if (a->fw.t > 1200) world_enter(a);
    }
}

void fourthwall_render(App *a){
    Framebuffer *fb = &a->fb;

    if (a->fw.phase == 0){
        /* their real desktop, glitching, with the parasite talking to them */
        if (a->shot) fb_blit_shot(fb, a->shot); else fb_clear(fb, COL_BG);
        gfx_rgb_split(fb, 2 + (int)(a->fw.t / 600));
        if ((rng_next(&a->rng) & 3) == 0) gfx_slice_tear(fb, &a->rng, 30, 3);
        gfx_brightness(fb, 55);

        char l0[96], l1[96], l2[96];
        snprintf(l0, sizeof(l0), "I SEE YOU, %s.", a->fw.user);
        snprintf(l1, sizeof(l1), "this is %s.", a->fw.host);
        snprintf(l2, sizeof(l2), "it's %s. why are you still awake?", a->fw.when);
        const char *L[4] = { l0, l1, l2, "you failed. now you belong to the machine." };

        int shown = (int)(a->fw.t / 650) + 1; if (shown > 4) shown = 4;
        int y = fb->h/2 - fb->ch_h*4;
        for (int i = 0; i < shown; i++){
            if ((rng_next(&a->rng) & 7) == 0){
                char g[96]; fb_garble(g, L[i], &a->rng, 25);
                fb_text_center(fb, fb->w/2, y, g, COL_RED);
            } else {
                fb_text_center(fb, fb->w/2, y, L[i], COL_RED);
            }
            y += fb->ch_h*2;
        }
        gfx_scanlines(fb, 82);
        return;
    }

    if (a->fw.phase == 1){
        /* pull back: the desktop shrinks into a monitor on a desk in a dark room */
        fb_clear(fb, 0x00000000);
        double k = a->fw.t / 2600.0; if (k > 1) k = 1;
        double scale = 1.0 - 0.78*k;
        int dw = (int)(fb->w * scale), dh = (int)(fb->h * scale);
        int dx = (fb->w - dw) / 2;
        int dy = (int)((fb->h - dh) / 2 + k * fb->h * 0.18);   /* slides down as you stand */
        fb_blit_shot_rect(fb, a->shot, dx, dy, dw, dh);
        fb_frame(fb, dx-6, dy-6, dw+12, dh+12, 0x00242424);    /* monitor bezel */
        fb_fill_rect(fb, dx-6, dy+dh+6, dw+12, 12, 0x00141414); /* monitor chin */
        int desk = dy + dh + 48;
        if (desk < fb->h) fb_fill_rect(fb, 0, desk, fb->w, fb->h - desk, 0x00080808);
        gfx_vignette(fb);
        gfx_brightness(fb, 85 - (int)(k*60));
        gfx_scanlines(fb, 88);
        return;
    }

    /* cut to black */
    fb_clear(fb, 0x00000000);
    if ((rng_next(&a->rng) & 3) == 0) gfx_invert_band(fb, 0, fb->h);
    fb_text_center(fb, fb->w/2, fb->h/2, "ERROR", COL_RED);
    gfx_scanlines(fb, 80);
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
        snprintf(l, sizeof(l), "there's no more room for you here, %s.", a->fw.user);
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
